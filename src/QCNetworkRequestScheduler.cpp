// SPDX-License-Identifier: MIT
// Copyright (c) 2025 QCurl Project

#include "QCNetworkRequestScheduler.h"

#include "QCNetworkReply.h"

#include <QDateTime>
#include <QDebug>
#include <QHash>
#include <QMap>
#include <QMetaObject>
#include <QMutex>
#include <QMutexLocker>
#include <QPointer>
#include <QQueue>
#include <QString>
#include <QTimer>

namespace QCurl {

struct QCNetworkRequestScheduler::Impl
{
    struct QueuedRequest
    {
        QPointer<QCNetworkReply> reply;    ///< 响应对象（使用 QPointer 防止悬空指针）
        QCNetworkRequestPriority priority; ///< 请求优先级
        QDateTime queueTime;               ///< 加入队列的时间
        QString host;                      ///< 主机名
    };

    mutable QMutex mutex; ///< 保护所有成员变量的互斥锁
    QCNetworkRequestScheduler::Config config;

    // 优先级队列
    QMap<QCNetworkRequestPriority, QQueue<QueuedRequest>> pendingQueue;

    // 运行中的请求
    QList<QPointer<QCNetworkReply>> runningRequests;
    QHash<QString, int> hostConnectionCount; ///< 主机连接计数

    // 延后调度的请求（非传输级 pause）
    QList<QPointer<QCNetworkReply>> deferredRequests;

    // 统计信息
    QCNetworkRequestScheduler::Statistics stats;
    QHash<QCNetworkReply *, QDateTime> requestStartTimes; ///< 请求开始时间
    QHash<QCNetworkReply *, QString> replyHosts;          ///< Reply 对应的主机名

    // 带宽控制
    QTimer *throttleTimer           = nullptr; ///< 带宽重置定时器
    qint64 bytesTransferredInWindow = 0;       ///< 当前窗口内传输的字节数
};

// 单例实现
QCNetworkRequestScheduler *QCNetworkRequestScheduler::instance()
{
    // 线程内单例：每个线程拥有独立的调度器，避免跨线程共享状态/回调重入
    static thread_local QCNetworkRequestScheduler instance;
    return &instance;
}

QCNetworkRequestScheduler::QCNetworkRequestScheduler(QObject *parent)
    : QObject(parent)
    , m_impl(new Impl)
{
    // 每秒重置带宽窗口
    m_impl->throttleTimer = new QTimer(this);
    m_impl->throttleTimer->setInterval(1000);
    connect(m_impl->throttleTimer,
            &QTimer::timeout,
            this,
            &QCNetworkRequestScheduler::updateBandwidthStats);
    m_impl->throttleTimer->start();
}

QCNetworkRequestScheduler::~QCNetworkRequestScheduler()
{
    cancelAllRequests();
}

void QCNetworkRequestScheduler::setConfig(const Config &config)
{
    {
        QMutexLocker locker(&m_impl->mutex);
        m_impl->config = config;
    }

    // 配置改变后尝试处理队列（避免持锁触发信号回调/重入）
    processQueue();
}

QCNetworkRequestScheduler::Config QCNetworkRequestScheduler::config() const
{
    QMutexLocker locker(&m_impl->mutex);
    return m_impl->config;
}

void QCNetworkRequestScheduler::scheduleReply(QCNetworkReply *reply,
                                              QCNetworkRequestPriority priority)
{
    if (!reply) {
        return;
    }

    QMutexLocker locker(&m_impl->mutex);

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
    Impl::QueuedRequest queuedReq;
    queuedReq.reply     = reply;
    queuedReq.priority  = priority;
    queuedReq.queueTime = QDateTime::currentDateTime();
    queuedReq.host      = reply->url().host();

    bool shouldEmitQueued = false;
    if (priority == QCNetworkRequestPriority::Critical) {
        // Critical：跳过队列直接执行（不受并发/主机限制约束）
        m_impl->runningRequests.append(reply);
        m_impl->stats.runningRequests = m_impl->runningRequests.size();
        m_impl->hostConnectionCount[queuedReq.host]++;
        m_impl->requestStartTimes[reply] = QDateTime::currentDateTime();
        m_impl->replyHosts[reply]        = queuedReq.host;
    } else {
        // 加入优先级队列
        m_impl->pendingQueue[priority].enqueue(queuedReq);
        m_impl->stats.pendingRequests++;
        shouldEmitQueued = true;
    }

    locker.unlock();

    if (shouldEmitQueued) {
        emit requestQueued(reply, priority);
        processQueue();
        return;
    }

    // Critical：立即启动（避免持锁触发 execute/cancel 的同步信号回调）
    startRequest(reply);
}

void QCNetworkRequestScheduler::processQueue()
{
    QList<QPointer<QCNetworkReply>> toStart;
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
        QMutexLocker locker(&m_impl->mutex);

        // 兜底清理：running/deferred/pending 中可能存在已销毁的 reply（QPointer 变 null）
        auto prunePointerList = [](QList<QPointer<QCNetworkReply>> &list) {
            for (int i = list.size() - 1; i >= 0; --i) {
                if (!list.at(i)) {
                    list.removeAt(i);
                }
            }
        };
        prunePointerList(m_impl->runningRequests);
        prunePointerList(m_impl->deferredRequests);
        m_impl->stats.runningRequests = m_impl->runningRequests.size();

        for (auto priority : priorities) {
            auto &queue = m_impl->pendingQueue[priority];

            while (!queue.isEmpty()) {
                const Impl::QueuedRequest &req = queue.head();

                if (!req.reply) {
                    queue.dequeue();
                    if (m_impl->stats.pendingRequests > 0) {
                        m_impl->stats.pendingRequests--;
                    }
                    continue;
                }

                // 检查并发/主机/带宽限制（锁内只做判定，不发射信号）
                if (m_impl->stats.runningRequests >= m_impl->config.maxConcurrentRequests) {
                    break;
                }
                const int hostCount = m_impl->hostConnectionCount.value(req.host, 0);
                if (hostCount >= m_impl->config.maxRequestsPerHost) {
                    break;
                }
                if (m_impl->config.enableThrottling && m_impl->config.maxBandwidthBytesPerSec > 0) {
                    if (m_impl->bytesTransferredInWindow >= m_impl->config.maxBandwidthBytesPerSec) {
                        shouldEmitBandwidthThrottled = true;
                        throttledBytesPerSec         = m_impl->bytesTransferredInWindow;
                        break;
                    }
                }

                // 出队并预占并发槽位（锁内更新状态，锁外真正启动）
                Impl::QueuedRequest queuedReq = queue.dequeue();
                if (m_impl->stats.pendingRequests > 0) {
                    m_impl->stats.pendingRequests--;
                }

                QCNetworkReply *reply = queuedReq.reply.data();
                m_impl->runningRequests.append(queuedReq.reply);
                m_impl->stats.runningRequests = m_impl->runningRequests.size();
                m_impl->hostConnectionCount[queuedReq.host]++;
                m_impl->requestStartTimes[reply] = QDateTime::currentDateTime();
                m_impl->replyHosts[reply]        = queuedReq.host;

                toStart.append(queuedReq.reply);
            }
        }

        // 检查队列是否为空（pending 为空即可，不要求 running 为空）
        bool isEmpty = true;
        for (const auto &queue : m_impl->pendingQueue) {
            if (!queue.isEmpty()) {
                isEmpty = false;
                break;
            }
        }

        if (isEmpty && m_impl->stats.pendingRequests > 0) {
            m_impl->stats.pendingRequests = 0;
        }
        if (isEmpty) {
            shouldEmitQueueEmpty = true;
        }
    }

    if (shouldEmitBandwidthThrottled) {
        emit bandwidthThrottled(throttledBytesPerSec);
    }

    for (const auto &replyPtr : toStart) {
        if (!replyPtr) {
            continue;
        }
        startRequest(replyPtr.data());
    }

    if (shouldEmitQueueEmpty) {
        emit queueEmpty();
    }
}

void QCNetworkRequestScheduler::startRequest(QCNetworkReply *reply)
{
    if (!reply) {
        return;
    }

    // 连接进度信号以追踪带宽使用
    connect(reply,
            &QCNetworkReply::downloadProgress,
            this,
            [this](qint64 bytesReceived, qint64 /*bytesTotal*/) {
                QMutexLocker locker(&m_impl->mutex);
                m_impl->bytesTransferredInWindow += bytesReceived;
            });

    connect(reply,
            &QCNetworkReply::uploadProgress,
            this,
            [this](qint64 bytesSent, qint64 /*bytesTotal*/) {
                QMutexLocker locker(&m_impl->mutex);
                m_impl->bytesTransferredInWindow += bytesSent;
            });

    // 发射信号
    emit requestStarted(reply);

    // 启动请求（排队到事件循环，避免在 scheduleRequest 返回前同步发射 finished）
    QPointer<QCNetworkReply> safeReply(reply);
    QMetaObject::invokeMethod(
        reply,
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
        QMutexLocker locker(&m_impl->mutex);

        if (m_impl->runningRequests.contains(reply)) {
            // 延后执行中的请求：为释放并发槽位，当前实现选择终止执行并转入 deferred
            // 注意：这不是传输级暂停，undefer 后一般会从头重新请求
            shouldCancel = true;

            m_impl->runningRequests.removeOne(reply);
            m_impl->stats.runningRequests = m_impl->runningRequests.size();

            // 更新主机连接计数
            if (m_impl->replyHosts.contains(reply)) {
                const QString host = m_impl->replyHosts[reply];
                if (m_impl->hostConnectionCount.contains(host)) {
                    m_impl->hostConnectionCount[host]--;
                    if (m_impl->hostConnectionCount[host] <= 0) {
                        m_impl->hostConnectionCount.remove(host);
                    }
                }
            }

            m_impl->deferredRequests.append(reply);
        } else if (removeFromQueue(reply)) {
            m_impl->deferredRequests.append(reply);
            if (m_impl->stats.pendingRequests > 0) {
                m_impl->stats.pendingRequests--;
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
        QMutexLocker locker(&m_impl->mutex);

        if (!m_impl->deferredRequests.contains(reply)) {
            return;
        }

        m_impl->deferredRequests.removeOne(reply);

        Impl::QueuedRequest req;
        req.reply     = reply;
        req.priority  = priority;
        req.queueTime = QDateTime::currentDateTime();
        req.host      = reply->url().host(); // 从 reply 的 url() 获取

        m_impl->pendingQueue[priority].enqueue(req);
        m_impl->stats.pendingRequests++;
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
        QMutexLocker locker(&m_impl->mutex);

        if (m_impl->runningRequests.contains(reply)) {
            wasRunning = true;
            m_impl->runningRequests.removeOne(reply);
            m_impl->stats.runningRequests = m_impl->runningRequests.size();
            m_impl->stats.cancelledRequests++;
            shouldProcess       = true;
            shouldEmitCancelled = true;

            if (m_impl->replyHosts.contains(reply)) {
                const QString host = m_impl->replyHosts[reply];
                if (m_impl->hostConnectionCount.contains(host)) {
                    m_impl->hostConnectionCount[host]--;
                    if (m_impl->hostConnectionCount[host] <= 0) {
                        m_impl->hostConnectionCount.remove(host);
                    }
                }
                m_impl->replyHosts.remove(reply);
            }
        } else if (m_impl->deferredRequests.contains(reply)) {
            m_impl->deferredRequests.removeOne(reply);
            m_impl->stats.cancelledRequests++;
            shouldEmitCancelled = true;
        } else if (removeFromQueue(reply)) {
            if (m_impl->stats.pendingRequests > 0) {
                m_impl->stats.pendingRequests--;
            }
            m_impl->stats.cancelledRequests++;
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
        QMutexLocker locker(&m_impl->mutex);

        for (const auto &reply : m_impl->runningRequests) {
            if (reply) {
                toCancel.append(reply);
                m_impl->stats.cancelledRequests++;
            }
        }
        m_impl->runningRequests.clear();
        m_impl->stats.runningRequests = 0;
        m_impl->hostConnectionCount.clear();
        m_impl->replyHosts.clear();
        m_impl->requestStartTimes.clear();

        for (const auto &reply : m_impl->deferredRequests) {
            if (reply) {
                toCancel.append(reply);
                m_impl->stats.cancelledRequests++;
            }
        }
        m_impl->deferredRequests.clear();

        for (auto &queue : m_impl->pendingQueue) {
            while (!queue.isEmpty()) {
                Impl::QueuedRequest req = queue.dequeue();
                if (req.reply) {
                    toCancel.append(req.reply);
                    m_impl->stats.cancelledRequests++;
                }
            }
        }
        m_impl->stats.pendingRequests = 0;
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

    bool requeued = false;
    {
        QMutexLocker locker(&m_impl->mutex);

        // 只能调整队列中的请求
        if (!removeFromQueue(reply)) {
            return; // 请求不在队列中
        }

        // 重新加入队列（使用新优先级）
        Impl::QueuedRequest req;
        req.reply     = reply;
        req.priority  = newPriority;
        req.queueTime = QDateTime::currentDateTime();
        req.host      = reply->url().host(); // 从 reply 的 url() 获取

        m_impl->pendingQueue[newPriority].enqueue(req);
        requeued = true;
    }

    if (requeued) {
        emit requestQueued(reply, newPriority);
        processQueue();
    }
}

QCNetworkRequestScheduler::Statistics QCNetworkRequestScheduler::statistics() const
{
    QMutexLocker locker(&m_impl->mutex);
    return m_impl->stats;
}

QList<QCNetworkReply *> QCNetworkRequestScheduler::pendingRequests() const
{
    QMutexLocker locker(&m_impl->mutex);

    QList<QCNetworkReply *> result;
    for (const auto &queue : m_impl->pendingQueue) {
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
    QMutexLocker locker(&m_impl->mutex);

    QList<QCNetworkReply *> result;
    for (auto reply : m_impl->runningRequests) {
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
        QMutexLocker locker(&m_impl->mutex);

        m_impl->runningRequests.removeOne(reply);
        m_impl->stats.runningRequests = m_impl->runningRequests.size();
        m_impl->stats.completedRequests++;

        if (m_impl->replyHosts.contains(reply)) {
            const QString host = m_impl->replyHosts[reply];
            if (m_impl->hostConnectionCount.contains(host)) {
                m_impl->hostConnectionCount[host]--;
                if (m_impl->hostConnectionCount[host] <= 0) {
                    m_impl->hostConnectionCount.remove(host);
                }
            }
            m_impl->replyHosts.remove(reply);
        }

        if (m_impl->requestStartTimes.contains(reply)) {
            const QDateTime startTime = m_impl->requestStartTimes[reply];
            const qint64 duration     = startTime.msecsTo(finishTime);

            if (m_impl->stats.completedRequests == 1) {
                m_impl->stats.avgResponseTime = duration;
            } else {
                m_impl->stats.avgResponseTime
                    = (m_impl->stats.avgResponseTime * (m_impl->stats.completedRequests - 1)
                                           + duration)
                      / m_impl->stats.completedRequests;
            }

            m_impl->requestStartTimes.remove(reply);
        }

        m_impl->stats.totalBytesReceived += bytesReceived;
    }

    emit requestFinished(reply);
    processQueue();
}

void QCNetworkRequestScheduler::updateBandwidthStats()
{
    bool shouldProcess = false;
    {
        QMutexLocker locker(&m_impl->mutex);
        m_impl->bytesTransferredInWindow = 0;
        shouldProcess                    = (m_impl->stats.pendingRequests > 0);
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
        QMutexLocker locker(&m_impl->mutex);

        // running/deferred：清理空指针，释放并发槽位
        auto prunePointerList = [](QList<QPointer<QCNetworkReply>> &list) {
            for (int i = list.size() - 1; i >= 0; --i) {
                if (!list.at(i)) {
                    list.removeAt(i);
                }
            }
        };

        const int runningBefore  = m_impl->runningRequests.size();
        const int deferredBefore = m_impl->deferredRequests.size();
        prunePointerList(m_impl->runningRequests);
        prunePointerList(m_impl->deferredRequests);
        if (m_impl->runningRequests.size() != runningBefore
            || m_impl->deferredRequests.size() != deferredBefore) {
            m_impl->stats.runningRequests = m_impl->runningRequests.size();
            shouldKickQueue         = true;
        }

        // pending：清理空指针，修正 pendingRequests（避免 queueEmpty 永远不触发）
        for (auto &queue : m_impl->pendingQueue) {
            while (!queue.isEmpty() && !queue.head().reply) {
                queue.dequeue();
                if (m_impl->stats.pendingRequests > 0) {
                    m_impl->stats.pendingRequests--;
                }
                shouldKickQueue = true;
            }
        }

        // host 计数兜底：若 finished 未能回收，这里必须释放 host 槽位
        if (m_impl->replyHosts.contains(reply)) {
            const QString host = m_impl->replyHosts[reply];
            if (m_impl->hostConnectionCount.contains(host)) {
                m_impl->hostConnectionCount[host]--;
                if (m_impl->hostConnectionCount[host] <= 0) {
                    m_impl->hostConnectionCount.remove(host);
                }
            }
            m_impl->replyHosts.remove(reply);
            shouldKickQueue = true;
        }

        m_impl->requestStartTimes.remove(reply);
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

    for (auto &queue : m_impl->pendingQueue) {
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
