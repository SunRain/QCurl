// SPDX-License-Identifier: MIT
// Copyright (c) 2025 QCurl Project

#include "QCNetworkRequestScheduler.h"
#include <QDebug>

namespace QCurl {

// 单例实现
QCNetworkRequestScheduler* QCNetworkRequestScheduler::instance()
{
    static QCNetworkRequestScheduler instance;
    return &instance;
}

QCNetworkRequestScheduler::QCNetworkRequestScheduler(QObject *parent)
    : QObject(parent)
    , m_throttleTimer(new QTimer(this))
    , m_bytesTransferredInWindow(0)
{
    // 每秒重置带宽窗口
    m_throttleTimer->setInterval(1000);
    connect(m_throttleTimer, &QTimer::timeout, this, &QCNetworkRequestScheduler::updateBandwidthStats);
    m_throttleTimer->start();
}

QCNetworkRequestScheduler::~QCNetworkRequestScheduler()
{
    cancelAllRequests();
}

void QCNetworkRequestScheduler::setConfig(const Config &config)
{
    QMutexLocker locker(&m_mutex);
    m_config = config;
    
    // 配置改变后尝试处理队列
    processQueue();
}

QCNetworkRequestScheduler::Config QCNetworkRequestScheduler::config() const
{
    QMutexLocker locker(&m_mutex);
    return m_config;
}

QCNetworkReply* QCNetworkRequestScheduler::scheduleRequest(
    const QCNetworkRequest &request,
    HttpMethod method,
    QCNetworkRequestPriority priority,
    const QByteArray &body)
{
    QMutexLocker locker(&m_mutex);
    
    // 创建 Reply 对象（但不立即执行）
    auto *reply = new QCNetworkReply(request, method, 
                                     ExecutionMode::Async, 
                                     body);
    
    // 连接 finished 信号
    connect(reply, &QCNetworkReply::finished, this, 
            [this, reply]() { onRequestFinished(reply); });
    
    // 创建队列项
    QueuedRequest queuedReq;
    queuedReq.reply = reply;
    queuedReq.priority = priority;
    queuedReq.queueTime = QDateTime::currentDateTime();
    queuedReq.host = request.url().host();
    
    // 如果是 Critical 优先级，跳过队列直接执行
    if (priority == QCNetworkRequestPriority::Critical) {
        startRequest(queuedReq);
    } else {
        // 加入优先级队列
        m_pendingQueue[priority].enqueue(queuedReq);
        m_stats.pendingRequests++;
        emit requestQueued(reply, priority);
        
        // 尝试处理队列
        processQueue();
    }
    
    return reply;
}

void QCNetworkRequestScheduler::processQueue()
{
    // 按优先级从高到低处理（注意：不包括 Critical，因为它会直接执行）
    QList<QCNetworkRequestPriority> priorities = {
        QCNetworkRequestPriority::VeryHigh,
        QCNetworkRequestPriority::High,
        QCNetworkRequestPriority::Normal,
        QCNetworkRequestPriority::Low,
        QCNetworkRequestPriority::VeryLow
    };
    
    for (auto priority : priorities) {
        auto &queue = m_pendingQueue[priority];
        
        while (!queue.isEmpty()) {
            const QueuedRequest &req = queue.head();
            
            // 检查 reply 是否仍然有效
            if (!req.reply) {
                queue.dequeue();
                m_stats.pendingRequests--;
                continue;
            }
            
            // 检查是否可以启动
            if (!canStartRequest(req)) {
                break;  // 当前优先级无法启动，检查下一个优先级
            }
            
            // 启动请求
            QueuedRequest queuedReq = queue.dequeue();
            m_stats.pendingRequests--;
            startRequest(queuedReq);
        }
    }
    
    // 检查队列是否为空
    bool isEmpty = true;
    for (const auto &queue : m_pendingQueue) {
        if (!queue.isEmpty()) {
            isEmpty = false;
            break;
        }
    }
    
    if (isEmpty && m_stats.pendingRequests > 0) {
        // 修正统计
        m_stats.pendingRequests = 0;
    }
    
    if (isEmpty) {
        emit queueEmpty();
    }
}

bool QCNetworkRequestScheduler::canStartRequest(const QueuedRequest &req) const
{
    // 检查 reply 是否仍然有效
    if (!req.reply) {
        return false;
    }
    
    // 检查全局并发限制
    if (m_runningRequests.size() >= m_config.maxConcurrentRequests) {
        return false;
    }
    
    // 检查每个主机的并发限制
    int hostCount = m_hostConnectionCount.value(req.host, 0);
    if (hostCount >= m_config.maxRequestsPerHost) {
        return false;
    }
    
    // 检查带宽限制（如果启用）
    if (m_config.enableThrottling && m_config.maxBandwidthBytesPerSec > 0) {
        if (m_bytesTransferredInWindow >= m_config.maxBandwidthBytesPerSec) {
            emit const_cast<QCNetworkRequestScheduler*>(this)->bandwidthThrottled(m_bytesTransferredInWindow);
            return false;
        }
    }
    
    return true;
}

void QCNetworkRequestScheduler::startRequest(const QueuedRequest &req)
{
    if (!req.reply) {
        return;
    }
    
    // 添加到运行列表
    m_runningRequests.append(req.reply);
    m_stats.runningRequests = m_runningRequests.size();
    
    // 更新主机连接计数
    m_hostConnectionCount[req.host]++;
    
    // 记录开始时间（用于计算平均响应时间）
    m_requestStartTimes[req.reply.data()] = QDateTime::currentDateTime();
    
    // 记录主机名
    m_replyHosts[req.reply.data()] = req.host;
    
    // 连接进度信号以追踪带宽使用
    connect(req.reply, &QCNetworkReply::downloadProgress, this,
            [this](qint64 bytesReceived, qint64 /*bytesTotal*/) {
        QMutexLocker locker(&m_mutex);
        m_bytesTransferredInWindow += bytesReceived;
    });
    
    connect(req.reply, &QCNetworkReply::uploadProgress, this,
            [this](qint64 bytesSent, qint64 /*bytesTotal*/) {
        QMutexLocker locker(&m_mutex);
        m_bytesTransferredInWindow += bytesSent;
    });
    
    // 发射信号
    emit requestStarted(req.reply);
    
    // 启动请求
    req.reply->execute();
}

void QCNetworkRequestScheduler::deferRequest(QCNetworkReply *reply)
{
    if (!reply) {
        return;
    }
    
    QMutexLocker locker(&m_mutex);
    
    // 检查是否在运行列表中
    if (m_runningRequests.contains(reply)) {
        // 延后执行中的请求：为释放并发槽位，当前实现选择终止执行并转入 deferred
        // 注意：这不是传输级暂停，undefer 后一般会从头重新请求
        reply->cancel();  // 使用 cancel 停止请求
        
        m_runningRequests.removeOne(reply);
        m_stats.runningRequests = m_runningRequests.size();
        
        // 更新主机连接计数
        if (m_replyHosts.contains(reply)) {
            QString host = m_replyHosts[reply];
            if (m_hostConnectionCount.contains(host)) {
                m_hostConnectionCount[host]--;
                if (m_hostConnectionCount[host] <= 0) {
                    m_hostConnectionCount.remove(host);
                }
            }
        }
        
        // 添加到 deferred 列表
        m_deferredRequests.append(reply);
        
        // 处理队列（释放的槽位可以给新请求）
        processQueue();
        return;
    }
    
    // 检查是否在队列中
    if (removeFromQueue(reply)) {
        m_deferredRequests.append(reply);
        m_stats.pendingRequests--;
    }
}

void QCNetworkRequestScheduler::undeferRequest(QCNetworkReply *reply)
{
    if (!reply) {
        return;
    }
    
    QMutexLocker locker(&m_mutex);
    
    if (!m_deferredRequests.contains(reply)) {
        return;
    }
    
    m_deferredRequests.removeOne(reply);
    
    // 重新加入队列（使用 Normal 优先级，因为无法从 reply 获取原优先级）
    QCNetworkRequestPriority priority = QCNetworkRequestPriority::Normal;
    
    QueuedRequest req;
    req.reply = reply;
    req.priority = priority;
    req.queueTime = QDateTime::currentDateTime();
    req.host = reply->url().host();  // 从 reply 的 url() 获取
    
    m_pendingQueue[priority].enqueue(req);
    m_stats.pendingRequests++;
    
    emit requestQueued(reply, priority);
    
    processQueue();
}

void QCNetworkRequestScheduler::cancelRequest(QCNetworkReply *reply)
{
    if (!reply) {
        return;
    }
    
    QMutexLocker locker(&m_mutex);
    
    // 从运行列表中移除
    if (m_runningRequests.contains(reply)) {
        reply->cancel();
        m_runningRequests.removeOne(reply);
        m_stats.runningRequests = m_runningRequests.size();
        m_stats.cancelledRequests++;
        
        // 更新主机连接计数
        if (m_replyHosts.contains(reply)) {
            QString host = m_replyHosts[reply];
            if (m_hostConnectionCount.contains(host)) {
                m_hostConnectionCount[host]--;
                if (m_hostConnectionCount[host] <= 0) {
                    m_hostConnectionCount.remove(host);
                }
            }
            m_replyHosts.remove(reply);
        }
        
        emit requestCancelled(reply);
        processQueue();
        return;
    }
    
    // 从 deferred 列表中移除
    if (m_deferredRequests.contains(reply)) {
        m_deferredRequests.removeOne(reply);
        m_stats.cancelledRequests++;
        emit requestCancelled(reply);
        return;
    }
    
    // 从队列中移除
    if (removeFromQueue(reply)) {
        m_stats.pendingRequests--;
        m_stats.cancelledRequests++;
        reply->cancel();
        emit requestCancelled(reply);
    }
}

void QCNetworkRequestScheduler::cancelAllRequests()
{
    QMutexLocker locker(&m_mutex);
    
    // 取消所有运行中的请求
    for (auto reply : m_runningRequests) {
        if (reply) {
            reply->cancel();
            m_stats.cancelledRequests++;
            emit requestCancelled(reply);
        }
    }
    m_runningRequests.clear();
    m_stats.runningRequests = 0;
    m_hostConnectionCount.clear();
    
    // 取消所有 deferred 的请求
    for (auto reply : m_deferredRequests) {
        if (reply) {
            m_stats.cancelledRequests++;
            emit requestCancelled(reply);
        }
    }
    m_deferredRequests.clear();
    
    // 取消所有队列中的请求
    for (auto &queue : m_pendingQueue) {
        while (!queue.isEmpty()) {
            QueuedRequest req = queue.dequeue();
            if (req.reply) {
                m_stats.cancelledRequests++;
                emit requestCancelled(req.reply);
            }
        }
    }
    m_stats.pendingRequests = 0;
    
    emit queueEmpty();
}

void QCNetworkRequestScheduler::changePriority(QCNetworkReply *reply, QCNetworkRequestPriority newPriority)
{
    if (!reply) {
        return;
    }
    
    QMutexLocker locker(&m_mutex);
    
    // 只能调整队列中的请求
    if (!removeFromQueue(reply)) {
        return;  // 请求不在队列中
    }
    
    // 重新加入队列（使用新优先级）
    QueuedRequest req;
    req.reply = reply;
    req.priority = newPriority;
    req.queueTime = QDateTime::currentDateTime();
    req.host = reply->url().host();  // 从 reply 的 url() 获取
    
    m_pendingQueue[newPriority].enqueue(req);
    
    emit requestQueued(reply, newPriority);
    
    processQueue();
}

QCNetworkRequestScheduler::Statistics QCNetworkRequestScheduler::statistics() const
{
    QMutexLocker locker(&m_mutex);
    return m_stats;
}

QList<QCNetworkReply*> QCNetworkRequestScheduler::pendingRequests() const
{
    QMutexLocker locker(&m_mutex);
    
    QList<QCNetworkReply*> result;
    for (const auto &queue : m_pendingQueue) {
        for (const auto &req : queue) {
            if (req.reply) {
                result.append(req.reply);
            }
        }
    }
    
    return result;
}

QList<QCNetworkReply*> QCNetworkRequestScheduler::runningRequests() const
{
    QMutexLocker locker(&m_mutex);
    
    QList<QCNetworkReply*> result;
    for (auto reply : m_runningRequests) {
        if (reply) {
            result.append(reply);
        }
    }
    
    return result;
}

void QCNetworkRequestScheduler::onRequestFinished(QCNetworkReply *reply)
{
    if (!reply) {
        return;
    }
    
    QMutexLocker locker(&m_mutex);
    
    // 从运行列表中移除
    m_runningRequests.removeOne(reply);
    m_stats.runningRequests = m_runningRequests.size();
    m_stats.completedRequests++;
    
    // 更新主机连接计数
    if (m_replyHosts.contains(reply)) {
        QString host = m_replyHosts[reply];
        if (m_hostConnectionCount.contains(host)) {
            m_hostConnectionCount[host]--;
            if (m_hostConnectionCount[host] <= 0) {
                m_hostConnectionCount.remove(host);
            }
        }
        m_replyHosts.remove(reply);
    }
    
    // 更新统计信息
    if (m_requestStartTimes.contains(reply)) {
        QDateTime startTime = m_requestStartTimes[reply];
        qint64 duration = startTime.msecsTo(QDateTime::currentDateTime());
        
        // 更新平均响应时间
        if (m_stats.completedRequests == 1) {
            m_stats.avgResponseTime = duration;
        } else {
            m_stats.avgResponseTime = (m_stats.avgResponseTime * (m_stats.completedRequests - 1) + duration) 
                                      / m_stats.completedRequests;
        }
        
        m_requestStartTimes.remove(reply);
    }
    
    // 更新总流量
    m_stats.totalBytesReceived += reply->bytesReceived();
    
    // 发射信号
    emit requestFinished(reply);
    
    // 处理队列（释放的槽位可以给新请求）
    processQueue();
}

void QCNetworkRequestScheduler::updateBandwidthStats()
{
    QMutexLocker locker(&m_mutex);
    
    // 重置带宽窗口
    m_bytesTransferredInWindow = 0;
    
    // 如果有等待的请求，尝试处理队列
    if (m_stats.pendingRequests > 0) {
        processQueue();
    }
}

bool QCNetworkRequestScheduler::removeFromQueue(QCNetworkReply *reply)
{
    if (!reply) {
        return false;
    }
    
    for (auto &queue : m_pendingQueue) {
        for (int i = 0; i < queue.size(); ++i) {
            if (queue[i].reply == reply) {
                queue.removeAt(i);
                return true;
            }
        }
    }
    
    return false;
}

} // namespace QCurl
