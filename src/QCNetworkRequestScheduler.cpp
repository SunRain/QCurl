// SPDX-License-Identifier: MIT
// Copyright (c) 2025 QCurl Project

#include "QCNetworkRequestScheduler.h"

#include <QDebug>
#include <QMetaObject>

namespace QCurl {

// 单例实现
QCNetworkRequestScheduler *QCNetworkRequestScheduler::instance()
{
    // 线程内单例：每个线程拥有独立的调度器，避免跨线程共享状态/回调重入
    static thread_local QCNetworkRequestScheduler instance;
    return &instance;
}

QCNetworkRequestScheduler::QCNetworkRequestScheduler(QObject *parent)
    : QObject(parent)
    , m_throttleTimer(new QTimer(this))
    , m_bytesTransferredInWindow(0)
{
    // 每秒重置带宽窗口
    m_throttleTimer->setInterval(1000);
    connect(m_throttleTimer,
            &QTimer::timeout,
            this,
            &QCNetworkRequestScheduler::updateBandwidthStats);
    m_throttleTimer->start();
}

QCNetworkRequestScheduler::~QCNetworkRequestScheduler()
{
    cancelAllRequests();
}

void QCNetworkRequestScheduler::setConfig(const Config &config)
{
    {
        QMutexLocker locker(&m_mutex);
        m_config = config;
    }

    // 配置改变后尝试处理队列（避免持锁触发信号回调/重入）
    processQueue();
}

QCNetworkRequestScheduler::Config QCNetworkRequestScheduler::config() const
{
    QMutexLocker locker(&m_mutex);
    return m_config;
}

QCNetworkReply *QCNetworkRequestScheduler::scheduleRequest(const QCNetworkRequest &request,
                                                           HttpMethod method,
                                                           QCNetworkRequestPriority priority,
                                                           const QByteArray &body)
{
    return scheduleRequest(request, method, priority, body, nullptr);
}

QCNetworkReply *QCNetworkRequestScheduler::scheduleRequest(const QCNetworkRequest &request,
                                                           HttpMethod method,
                                                           QCNetworkRequestPriority priority,
                                                           const QByteArray &body,
                                                           QObject *replyParent)
{
    // 允许调用方显式指定 replyParent（通常为 QCNetworkAccessManager），用于回溯
    // cache/mock/middleware。 注意：QObject 父子关系要求 thread affinity 一致，跨线程调用方应在
    // owner 线程创建/调度。
    QMutexLocker locker(&m_mutex);

    // 创建 Reply 对象（但不立即执行）
    auto *reply = new QCNetworkReply(request, method, ExecutionMode::Async, body, replyParent);

    // 兜底：用户可能在 finished 槽内直接 delete reply（而非 deleteLater），导致 queued 的 finished
    // 回调无法执行。 这里监听 destroyed 以回收并发槽位与主机计数，避免队列卡死。
    connect(reply, &QObject::destroyed, this, &QCNetworkRequestScheduler::onReplyDestroyed);

    // 连接 finished 信号（QueuedConnection：避免用户在 finished 槽内直接 delete reply
    // 导致重入/死锁）
    QPointer<QCNetworkReply> safeReply(reply);
    connect(
        reply,
        &QCNetworkReply::finished,
        this,
        [this, safeReply]() {
            if (safeReply) {
                onRequestFinished(safeReply.data());
            }
        },
        Qt::QueuedConnection);

    // 创建队列项
    QueuedRequest queuedReq;
    queuedReq.reply     = reply;
    queuedReq.priority  = priority;
    queuedReq.queueTime = QDateTime::currentDateTime();
    queuedReq.host      = request.url().host();

    bool shouldEmitQueued = false;
    if (priority == QCNetworkRequestPriority::Critical) {
        // Critical：跳过队列直接执行（不受并发/主机限制约束）
        m_runningRequests.append(reply);
        m_stats.runningRequests = m_runningRequests.size();
        m_hostConnectionCount[queuedReq.host]++;
        m_requestStartTimes[reply] = QDateTime::currentDateTime();
        m_replyHosts[reply]        = queuedReq.host;
    } else {
        // 加入优先级队列
        m_pendingQueue[priority].enqueue(queuedReq);
        m_stats.pendingRequests++;
        shouldEmitQueued = true;
    }

    locker.unlock();

    if (shouldEmitQueued) {
        emit requestQueued(reply, priority);
        processQueue();
        return reply;
    }

    // Critical：立即启动（避免持锁触发 execute/cancel 的同步信号回调）
    startRequest(queuedReq);

    return reply;
}

void QCNetworkRequestScheduler::processQueue()
{
    QList<QueuedRequest> toStart;
    bool shouldEmitQueueEmpty         = false;
    bool shouldEmitBandwidthThrottled = false;
    qint64 throttledBytesPerSec       = 0;

    // 按优先级从高到低处理（注意：不包括 Critical，因为它会直接执行）
    QList<QCNetworkRequestPriority> priorities = {QCNetworkRequestPriority::VeryHigh,
                                                  QCNetworkRequestPriority::High,
                                                  QCNetworkRequestPriority::Normal,
                                                  QCNetworkRequestPriority::Low,
                                                  QCNetworkRequestPriority::VeryLow};

    {
        QMutexLocker locker(&m_mutex);

        // 兜底清理：running/deferred/pending 中可能存在已销毁的 reply（QPointer 变 null）
        auto prunePointerList = [](QList<QPointer<QCNetworkReply>> &list) {
            for (int i = list.size() - 1; i >= 0; --i) {
                if (!list.at(i)) {
                    list.removeAt(i);
                }
            }
        };
        prunePointerList(m_runningRequests);
        prunePointerList(m_deferredRequests);
        m_stats.runningRequests = m_runningRequests.size();

        for (auto priority : priorities) {
            auto &queue = m_pendingQueue[priority];

            while (!queue.isEmpty()) {
                const QueuedRequest &req = queue.head();

                if (!req.reply) {
                    queue.dequeue();
                    if (m_stats.pendingRequests > 0) {
                        m_stats.pendingRequests--;
                    }
                    continue;
                }

                // 检查并发/主机/带宽限制（锁内只做判定，不发射信号）
                if (m_stats.runningRequests >= m_config.maxConcurrentRequests) {
                    break;
                }
                const int hostCount = m_hostConnectionCount.value(req.host, 0);
                if (hostCount >= m_config.maxRequestsPerHost) {
                    break;
                }
                if (m_config.enableThrottling && m_config.maxBandwidthBytesPerSec > 0) {
                    if (m_bytesTransferredInWindow >= m_config.maxBandwidthBytesPerSec) {
                        shouldEmitBandwidthThrottled = true;
                        throttledBytesPerSec         = m_bytesTransferredInWindow;
                        break;
                    }
                }

                // 出队并预占并发槽位（锁内更新状态，锁外真正启动）
                QueuedRequest queuedReq = queue.dequeue();
                if (m_stats.pendingRequests > 0) {
                    m_stats.pendingRequests--;
                }

                QCNetworkReply *reply = queuedReq.reply.data();
                m_runningRequests.append(queuedReq.reply);
                m_stats.runningRequests = m_runningRequests.size();
                m_hostConnectionCount[queuedReq.host]++;
                m_requestStartTimes[reply] = QDateTime::currentDateTime();
                m_replyHosts[reply]        = queuedReq.host;

                toStart.append(queuedReq);
            }
        }

        // 检查队列是否为空（pending 为空即可，不要求 running 为空）
        bool isEmpty = true;
        for (const auto &queue : m_pendingQueue) {
            if (!queue.isEmpty()) {
                isEmpty = false;
                break;
            }
        }

        if (isEmpty && m_stats.pendingRequests > 0) {
            m_stats.pendingRequests = 0;
        }
        if (isEmpty) {
            shouldEmitQueueEmpty = true;
        }
    }

    if (shouldEmitBandwidthThrottled) {
        emit bandwidthThrottled(throttledBytesPerSec);
    }

    for (const auto &req : toStart) {
        startRequest(req);
    }

    if (shouldEmitQueueEmpty) {
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
    if (m_stats.runningRequests >= m_config.maxConcurrentRequests) {
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

    // 连接进度信号以追踪带宽使用
    connect(req.reply,
            &QCNetworkReply::downloadProgress,
            this,
            [this](qint64 bytesReceived, qint64 /*bytesTotal*/) {
                QMutexLocker locker(&m_mutex);
                m_bytesTransferredInWindow += bytesReceived;
            });

    connect(req.reply,
            &QCNetworkReply::uploadProgress,
            this,
            [this](qint64 bytesSent, qint64 /*bytesTotal*/) {
                QMutexLocker locker(&m_mutex);
                m_bytesTransferredInWindow += bytesSent;
            });

    // 发射信号
    emit requestStarted(req.reply);

    // 启动请求（排队到事件循环，避免在 scheduleRequest 返回前同步发射 finished）
    QPointer<QCNetworkReply> safeReply(req.reply);
    QMetaObject::invokeMethod(
        req.reply.data(),
        [safeReply]() {
            if (safeReply) {
                safeReply->execute();
            }
        },
        Qt::QueuedConnection);
}

void QCNetworkRequestScheduler::deferRequest(QCNetworkReply *reply)
{
    if (!reply) {
        return;
    }

    bool shouldCancel = false;

    // 检查是否在运行列表中
    {
        QMutexLocker locker(&m_mutex);

        if (m_runningRequests.contains(reply)) {
            // 延后执行中的请求：为释放并发槽位，当前实现选择终止执行并转入 deferred
            // 注意：这不是传输级暂停，undefer 后一般会从头重新请求
            shouldCancel = true;

            m_runningRequests.removeOne(reply);
            m_stats.runningRequests = m_runningRequests.size();

            // 更新主机连接计数
            if (m_replyHosts.contains(reply)) {
                const QString host = m_replyHosts[reply];
                if (m_hostConnectionCount.contains(host)) {
                    m_hostConnectionCount[host]--;
                    if (m_hostConnectionCount[host] <= 0) {
                        m_hostConnectionCount.remove(host);
                    }
                }
            }

            m_deferredRequests.append(reply);
        } else if (removeFromQueue(reply)) {
            m_deferredRequests.append(reply);
            if (m_stats.pendingRequests > 0) {
                m_stats.pendingRequests--;
            }
        }
    }

    if (shouldCancel) {
        QPointer<QCNetworkReply> safeReply(reply);
        QMetaObject::invokeMethod(
            reply,
            [safeReply]() {
                if (safeReply) {
                    safeReply->cancel();
                }
            },
            Qt::QueuedConnection);
        processQueue();
    }
}

void QCNetworkRequestScheduler::undeferRequest(QCNetworkReply *reply)
{
    if (!reply) {
        return;
    }

    // 重新加入队列（使用 Normal 优先级，因为无法从 reply 获取原优先级）
    QCNetworkRequestPriority priority = QCNetworkRequestPriority::Normal;
    bool requeued                     = false;

    {
        QMutexLocker locker(&m_mutex);

        if (!m_deferredRequests.contains(reply)) {
            return;
        }

        m_deferredRequests.removeOne(reply);

        QueuedRequest req;
        req.reply     = reply;
        req.priority  = priority;
        req.queueTime = QDateTime::currentDateTime();
        req.host      = reply->url().host(); // 从 reply 的 url() 获取

        m_pendingQueue[priority].enqueue(req);
        m_stats.pendingRequests++;
        requeued = true;
    }

    if (requeued) {
        emit requestQueued(reply, priority);
        processQueue();
    }
}

void QCNetworkRequestScheduler::cancelRequest(QCNetworkReply *reply)
{
    if (!reply) {
        return;
    }

    bool wasRunning          = false;
    bool shouldProcess       = false;
    bool shouldEmitCancelled = false;

    {
        QMutexLocker locker(&m_mutex);

        if (m_runningRequests.contains(reply)) {
            wasRunning = true;
            m_runningRequests.removeOne(reply);
            m_stats.runningRequests = m_runningRequests.size();
            m_stats.cancelledRequests++;
            shouldProcess       = true;
            shouldEmitCancelled = true;

            if (m_replyHosts.contains(reply)) {
                const QString host = m_replyHosts[reply];
                if (m_hostConnectionCount.contains(host)) {
                    m_hostConnectionCount[host]--;
                    if (m_hostConnectionCount[host] <= 0) {
                        m_hostConnectionCount.remove(host);
                    }
                }
                m_replyHosts.remove(reply);
            }
        } else if (m_deferredRequests.contains(reply)) {
            m_deferredRequests.removeOne(reply);
            m_stats.cancelledRequests++;
            shouldEmitCancelled = true;
        } else if (removeFromQueue(reply)) {
            if (m_stats.pendingRequests > 0) {
                m_stats.pendingRequests--;
            }
            m_stats.cancelledRequests++;
            shouldEmitCancelled = true;
        }
    }

    if (shouldEmitCancelled) {
        QPointer<QCNetworkReply> safeReply(reply);
        QMetaObject::invokeMethod(
            reply,
            [safeReply]() {
                if (safeReply) {
                    safeReply->cancel();
                }
            },
            Qt::QueuedConnection);
        emit requestCancelled(reply);
    }

    if (wasRunning && shouldProcess) {
        processQueue();
    }
}

void QCNetworkRequestScheduler::cancelAllRequests()
{
    QList<QPointer<QCNetworkReply>> toCancel;

    {
        QMutexLocker locker(&m_mutex);

        for (const auto &reply : m_runningRequests) {
            if (reply) {
                toCancel.append(reply);
                m_stats.cancelledRequests++;
            }
        }
        m_runningRequests.clear();
        m_stats.runningRequests = 0;
        m_hostConnectionCount.clear();
        m_replyHosts.clear();
        m_requestStartTimes.clear();

        for (const auto &reply : m_deferredRequests) {
            if (reply) {
                toCancel.append(reply);
                m_stats.cancelledRequests++;
            }
        }
        m_deferredRequests.clear();

        for (auto &queue : m_pendingQueue) {
            while (!queue.isEmpty()) {
                QueuedRequest req = queue.dequeue();
                if (req.reply) {
                    toCancel.append(req.reply);
                    m_stats.cancelledRequests++;
                }
            }
        }
        m_stats.pendingRequests = 0;
    }

    for (const auto &replyPtr : toCancel) {
        if (!replyPtr) {
            continue;
        }
        QPointer<QCNetworkReply> safeReply(replyPtr);
        QMetaObject::invokeMethod(
            replyPtr.data(),
            [safeReply]() {
                if (safeReply) {
                    safeReply->cancel();
                }
            },
            Qt::QueuedConnection);
        emit requestCancelled(replyPtr.data());
    }

    emit queueEmpty();
}

void QCNetworkRequestScheduler::changePriority(QCNetworkReply *reply,
                                               QCNetworkRequestPriority newPriority)
{
    if (!reply) {
        return;
    }

    QMutexLocker locker(&m_mutex);

    // 只能调整队列中的请求
    if (!removeFromQueue(reply)) {
        return; // 请求不在队列中
    }

    // 重新加入队列（使用新优先级）
    QueuedRequest req;
    req.reply     = reply;
    req.priority  = newPriority;
    req.queueTime = QDateTime::currentDateTime();
    req.host      = reply->url().host(); // 从 reply 的 url() 获取

    m_pendingQueue[newPriority].enqueue(req);

    emit requestQueued(reply, newPriority);

    processQueue();
}

QCNetworkRequestScheduler::Statistics QCNetworkRequestScheduler::statistics() const
{
    QMutexLocker locker(&m_mutex);
    return m_stats;
}

QList<QCNetworkReply *> QCNetworkRequestScheduler::pendingRequests() const
{
    QMutexLocker locker(&m_mutex);

    QList<QCNetworkReply *> result;
    for (const auto &queue : m_pendingQueue) {
        for (const auto &req : queue) {
            if (req.reply) {
                result.append(req.reply);
            }
        }
    }

    return result;
}

QList<QCNetworkReply *> QCNetworkRequestScheduler::runningRequests() const
{
    QMutexLocker locker(&m_mutex);

    QList<QCNetworkReply *> result;
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

    const qint64 bytesReceived = reply->bytesReceived();
    const QDateTime finishTime = QDateTime::currentDateTime();

    {
        QMutexLocker locker(&m_mutex);

        m_runningRequests.removeOne(reply);
        m_stats.runningRequests = m_runningRequests.size();
        m_stats.completedRequests++;

        if (m_replyHosts.contains(reply)) {
            const QString host = m_replyHosts[reply];
            if (m_hostConnectionCount.contains(host)) {
                m_hostConnectionCount[host]--;
                if (m_hostConnectionCount[host] <= 0) {
                    m_hostConnectionCount.remove(host);
                }
            }
            m_replyHosts.remove(reply);
        }

        if (m_requestStartTimes.contains(reply)) {
            const QDateTime startTime = m_requestStartTimes[reply];
            const qint64 duration     = startTime.msecsTo(finishTime);

            if (m_stats.completedRequests == 1) {
                m_stats.avgResponseTime = duration;
            } else {
                m_stats.avgResponseTime = (m_stats.avgResponseTime * (m_stats.completedRequests - 1)
                                           + duration)
                                          / m_stats.completedRequests;
            }

            m_requestStartTimes.remove(reply);
        }

        m_stats.totalBytesReceived += bytesReceived;
    }

    emit requestFinished(reply);
    processQueue();
}

void QCNetworkRequestScheduler::updateBandwidthStats()
{
    bool shouldProcess = false;
    {
        QMutexLocker locker(&m_mutex);
        m_bytesTransferredInWindow = 0;
        shouldProcess              = (m_stats.pendingRequests > 0);
    }

    if (shouldProcess) {
        processQueue();
    }
}

void QCNetworkRequestScheduler::onReplyDestroyed(QObject *obj)
{
    auto *reply          = static_cast<QCNetworkReply *>(obj);
    bool shouldKickQueue = false;

    {
        QMutexLocker locker(&m_mutex);

        // running/deferred：清理空指针，释放并发槽位
        auto prunePointerList = [](QList<QPointer<QCNetworkReply>> &list) {
            for (int i = list.size() - 1; i >= 0; --i) {
                if (!list.at(i)) {
                    list.removeAt(i);
                }
            }
        };

        const int runningBefore  = m_runningRequests.size();
        const int deferredBefore = m_deferredRequests.size();
        prunePointerList(m_runningRequests);
        prunePointerList(m_deferredRequests);
        if (m_runningRequests.size() != runningBefore
            || m_deferredRequests.size() != deferredBefore) {
            m_stats.runningRequests = m_runningRequests.size();
            shouldKickQueue         = true;
        }

        // pending：清理空指针，修正 pendingRequests（避免 queueEmpty 永远不触发）
        for (auto &queue : m_pendingQueue) {
            while (!queue.isEmpty() && !queue.head().reply) {
                queue.dequeue();
                if (m_stats.pendingRequests > 0) {
                    m_stats.pendingRequests--;
                }
                shouldKickQueue = true;
            }
        }

        // host 计数兜底：若 finished 未能回收，这里必须释放 host 槽位
        if (m_replyHosts.contains(reply)) {
            const QString host = m_replyHosts[reply];
            if (m_hostConnectionCount.contains(host)) {
                m_hostConnectionCount[host]--;
                if (m_hostConnectionCount[host] <= 0) {
                    m_hostConnectionCount.remove(host);
                }
            }
            m_replyHosts.remove(reply);
            shouldKickQueue = true;
        }

        m_requestStartTimes.remove(reply);
    }

    if (shouldKickQueue) {
        QMetaObject::invokeMethod(this, [this]() { processQueue(); }, Qt::QueuedConnection);
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
