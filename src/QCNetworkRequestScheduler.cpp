// SPDX-License-Identifier: MIT
// Copyright (c) 2025 QCurl Project

#include "QCNetworkRequestScheduler.h"

#include "QCNetworkReply.h"
#include "private/QCNetworkRequestSchedulerPrivate_p.h"

#include <QMutexLocker>
#include <QPointer>
#include <QThread>
#include <QDateTime>

namespace QCurl {

void QCNetworkRequestScheduler::scheduleReply(QCNetworkReply *reply,
                                              const QString &lane,
                                              QCNetworkRequestPriority priority)
{
    if (!reply) {
        return;
    }

    if (QThread::currentThread() != thread()) {
        QPointer<QCNetworkReply> safeReply(reply);
        Internal::invokeOnSchedulerOwnerThread(
            this,
            [this, safeReply, lane, priority]() {
                if (safeReply) {
                    scheduleReply(safeReply.data(), lane, priority);
                }
            },
            "QCNetworkRequestScheduler::scheduleReply");
        return;
    }
    Internal::assertSchedulerOwnerThread(this, "QCNetworkRequestScheduler::scheduleReply");
    Q_ASSERT_X(reply->thread() == thread(),
               "QCNetworkRequestScheduler::scheduleReply",
               "reply must live on the scheduler owner thread");

    const Internal::ReplyKey key = Internal::replyKey(reply);
    const QString laneKey = Internal::normalizedLane(lane);
    const Internal::ReplySnapshot snapshot = {laneKey, Internal::buildHostKey(reply->url()), priority};

    {
        QMutexLocker locker(&m_impl->mutex);

        // 空串也作为 default lane 进入轮转状态，保证后续 DRR/reservation 逻辑一致。
        m_impl->queues.ensureLane(laneKey);

        m_impl->replySnapshots.insert(key, snapshot);
        m_impl->replyStates.insert(key, Internal::ScheduledState::Pending);
        m_impl->queues.pendingRequests.append({key, reply, snapshot, QDateTime::currentDateTime()});
        m_impl->stats.setPendingRequests(m_impl->queues.pendingRequests.size());
    }

    connect(reply, &QObject::destroyed, this, &QCNetworkRequestScheduler::onReplyDestroyed);

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

    emit requestQueued(reply, snapshot.lane, snapshot.hostKey, snapshot.priority);
    processQueue();
}

void QCNetworkRequestScheduler::processQueue()
{
    if (QThread::currentThread() != thread()) {
        Internal::invokeOnSchedulerOwnerThread(
            this, [this]() { processQueue(); }, "QCNetworkRequestScheduler::processQueue");
        return;
    }
    Internal::assertSchedulerOwnerThread(this, "QCNetworkRequestScheduler::processQueue");

    QList<QCNetworkReply *> toStart;
    bool shouldEmitQueueEmpty         = false;
    bool shouldEmitBandwidthThrottled = false;
    qint64 throttledBytesPerSec       = 0;

    {
        QMutexLocker locker(&m_impl->mutex);

        while (true) {
            if (m_impl->queues.runningRequests.size() >= m_impl->config.maxConcurrentRequests()) {
                break;
            }

            if (m_impl->config.enableThrottling()
                && m_impl->config.maxBandwidthBytesPerSec() > 0
                && m_impl->bytesTransferredInWindow >= m_impl->config.maxBandwidthBytesPerSec()) {
                shouldEmitBandwidthThrottled = true;
                throttledBytesPerSec         = m_impl->bytesTransferredInWindow;
                break;
            }

            // 调度顺序固定为：per-host reservation → lane global reservation → DRR best-effort。
            const int nextIndex = m_impl->queues.selectNextIndex(m_impl->config);
            if (nextIndex < 0) {
                break;
            }

            const Internal::SchedulerQueues::QueuedRequest request
                = m_impl->queues.pendingRequests.takeAt(nextIndex);
            m_impl->stats.setPendingRequests(m_impl->queues.pendingRequests.size());
            m_impl->queues.markRunning(request);
            m_impl->replyStates[request.key] = Internal::ScheduledState::Running;
            m_impl->requestStartTimes.insert(request.key, QDateTime::currentDateTime());
            m_impl->stats.setRunningRequests(m_impl->queues.runningRequests.size());

            toStart.append(request.reply);
        }

        shouldEmitQueueEmpty = m_impl->queues.pendingRequests.isEmpty();
    }

    if (shouldEmitBandwidthThrottled) {
        emit bandwidthThrottled(throttledBytesPerSec);
    }

    for (auto *reply : std::as_const(toStart)) {
        startRequest(reply);
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

    if (QThread::currentThread() != thread()) {
        QPointer<QCNetworkReply> safeReply(reply);
        Internal::invokeOnSchedulerOwnerThread(
            this,
            [this, safeReply]() {
                if (safeReply) {
                    startRequest(safeReply.data());
                }
            },
            "QCNetworkRequestScheduler::startRequest");
        return;
    }
    Internal::assertSchedulerOwnerThread(this, "QCNetworkRequestScheduler::startRequest");
    Q_ASSERT_X(reply->thread() == thread(),
               "QCNetworkRequestScheduler::startRequest",
               "reply must live on the scheduler owner thread");

    const Internal::ReplyKey key = Internal::replyKey(reply);
    const Internal::SchedulerStartContext context = m_impl->prepareStartContext(this, reply, key);
    m_impl->dispatchReplyExecution(this, reply, key, context);
}

void QCNetworkRequestScheduler::cancelAllRequests()
{
    if (QThread::currentThread() != thread()) {
        Internal::invokeOnSchedulerOwnerThread(
            this,
            [this]() { cancelAllRequests(); },
            "QCNetworkRequestScheduler::cancelAllRequests");
        return;
    }
    Internal::assertSchedulerOwnerThread(this, "QCNetworkRequestScheduler::cancelAllRequests");

    QList<QCNetworkReply *> replies;
    QList<Internal::ReplySnapshot> snapshots;
    QList<Internal::ReplyKey> keysToCancel;

    {
        QMutexLocker locker(&m_impl->mutex);

        // 先冻结待取消 key，再统一 finalize，避免边遍历边改动 queue/running 容器。
        for (const auto &request : std::as_const(m_impl->queues.pendingRequests)) {
            if (request.reply && !keysToCancel.contains(request.key)) {
                keysToCancel.append(request.key);
            }
        }
        for (const auto &request : std::as_const(m_impl->queues.deferredRequests)) {
            if (request.reply && !keysToCancel.contains(request.key)) {
                keysToCancel.append(request.key);
            }
        }
        for (auto *reply : std::as_const(m_impl->queues.runningRequests)) {
            const Internal::ReplyKey key = Internal::replyKey(reply);
            if (reply && !keysToCancel.contains(key)) {
                keysToCancel.append(key);
            }
        }

        for (Internal::ReplyKey key : std::as_const(keysToCancel)) {
            const Internal::FinalizeResult result
                = m_impl->finalizeReplyLocked(key, Internal::FinalizeTrigger::ExplicitCancel);
            if (result.wasTracked) {
                replies.append(Internal::replyFromKey(key));
                snapshots.append(result.snapshot);
            }
        }

        m_impl->bytesTransferredInWindow = 0;
        // 运行时调度状态已经被 reply 实体承接，不再保留过期 cursor/deficit 现场。
        m_impl->queues.resetRuntimeState();
    }

    for (int i = 0; i < replies.size(); ++i) {
        auto *reply = replies.at(i);
        const Internal::ReplySnapshot &snapshot = snapshots.at(i);
        Internal::invokeReplyCancel(reply);
        emit requestCancelled(reply, snapshot.lane, snapshot.hostKey);
    }

    emit queueEmpty();
}

int QCNetworkRequestScheduler::cancelLaneRequests(const QString &lane, CancelLaneScope scope)
{
    if (QThread::currentThread() != thread()) {
        return Internal::rejectOffOwnerThreadValue(this, 0, "QCNetworkRequestScheduler::cancelLaneRequests");
    }
    Internal::assertSchedulerOwnerThread(this, "QCNetworkRequestScheduler::cancelLaneRequests");

    const QString laneKey = Internal::normalizedLane(lane);
    QList<QCNetworkReply *> replies;
    QList<Internal::ReplySnapshot> snapshots;
    bool shouldKickQueue = false;
    QList<Internal::ReplyKey> keysToCancel;

    {
        QMutexLocker locker(&m_impl->mutex);

        // 先收集目标 lane 的 key，再统一 finalize，避免 queue 迭代过程中失效。
        auto collectKeysFromQueue = [&](const QList<Internal::SchedulerQueues::QueuedRequest> &queue) {
            for (const auto &request : queue) {
                if (!request.reply || request.snapshot.lane != laneKey
                    || keysToCancel.contains(request.key)) {
                    continue;
                }
                keysToCancel.append(request.key);
            }
        };

        collectKeysFromQueue(m_impl->queues.pendingRequests);
        collectKeysFromQueue(m_impl->queues.deferredRequests);

        if (scope == CancelLaneScope::PendingAndRunning) {
            for (auto *reply : std::as_const(m_impl->queues.runningRequests)) {
                const Internal::ReplyKey key = Internal::replyKey(reply);
                const Internal::ReplySnapshot snapshot = m_impl->replySnapshots.value(key);
                if (reply && snapshot.lane == laneKey && !keysToCancel.contains(key)) {
                    keysToCancel.append(key);
                }
            }
        }

        for (Internal::ReplyKey key : std::as_const(keysToCancel)) {
            const Internal::FinalizeResult result
                = m_impl->finalizeReplyLocked(key, Internal::FinalizeTrigger::ExplicitCancel);
            if (result.wasTracked) {
                replies.append(Internal::replyFromKey(key));
                snapshots.append(result.snapshot);
                shouldKickQueue = shouldKickQueue || result.shouldKickQueue;
            }
        }
    }

    for (int i = 0; i < replies.size(); ++i) {
        auto *reply = replies.at(i);
        const Internal::ReplySnapshot &snapshot = snapshots.at(i);
        Internal::invokeReplyCancel(reply);
        emit requestCancelled(reply, snapshot.lane, snapshot.hostKey);
    }

    if (shouldKickQueue) {
        processQueue();
    }

    return replies.size();
}

void QCNetworkRequestScheduler::changePriority(QCNetworkReply *reply,
                                               QCNetworkRequestPriority newPriority)
{
    if (!reply) {
        return;
    }

    if (QThread::currentThread() != thread()) {
        QPointer<QCNetworkReply> safeReply(reply);
        Internal::invokeOnSchedulerOwnerThread(
            this,
            [this, safeReply, newPriority]() {
                if (safeReply) {
                    changePriority(safeReply.data(), newPriority);
                }
            },
            "QCNetworkRequestScheduler::changePriority");
        return;
    }
    Internal::assertSchedulerOwnerThread(this, "QCNetworkRequestScheduler::changePriority");

    const Internal::ReplyKey key = Internal::replyKey(reply);
    Internal::ReplySnapshot snapshot;
    bool requeued = false;
    {
        QMutexLocker locker(&m_impl->mutex);
        const int index = Internal::SchedulerQueues::findQueuedRequestIndex(
            m_impl->queues.pendingRequests,
            [key](const Internal::SchedulerQueues::QueuedRequest &request) {
                return request.key == key;
            });
        if (index < 0) {
            return;
        }

        m_impl->queues.pendingRequests[index].snapshot.priority = newPriority;
        m_impl->replySnapshots[key].priority             = newPriority;
        snapshot                                         = m_impl->queues.pendingRequests.at(index).snapshot;
        requeued                                         = true;
    }

    if (requeued) {
        emit requestQueued(reply, snapshot.lane, snapshot.hostKey, snapshot.priority);
        processQueue();
    }
}


} // namespace QCurl
