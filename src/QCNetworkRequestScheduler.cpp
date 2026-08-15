// SPDX-License-Identifier: MIT
// Copyright (c) 2025 QCurl Project

#include "QCNetworkRequestScheduler.h"

#include "QCNetworkReply.h"
#include "private/QCNetworkRequestSchedulerPrivate_p.h"

#include <QDateTime>
#include <QMutexLocker>
#include <QPointer>
#include <QThread>

namespace QCurl {

namespace {

struct SchedulerQueueBatch
{
    QList<QPointer<QCNetworkReply>> toStart;
    bool shouldEmitQueueEmpty         = false;
    bool shouldEmitBandwidthThrottled = false;
    qint64 throttledBytesPerSec       = 0;
};

template<typename SchedulerImpl>
void trackScheduledReply(SchedulerImpl *impl,
                         QCNetworkReply *reply,
                         Internal::ReplyKey key,
                         const Internal::ReplySnapshot &snapshot)
{
    QMutexLocker locker(&impl->mutex);

    // 空串也作为 default lane 进入轮转状态，保证后续 DRR/reservation 逻辑一致。
    impl->queues.ensureLane(snapshot.lane);

    const Internal::SchedulerRequestId requestId = impl->nextRequestId++;
    impl->replySnapshots.insert(key, snapshot);
    impl->replyStates.insert(key, Internal::ScheduledState::Pending);
    impl->queues.enqueuePending({requestId, key, reply, snapshot, QDateTime::currentDateTime()});
    impl->stats.setPendingRequests(impl->queues.pendingCount());
}

template<typename SchedulerImpl>
SchedulerQueueBatch takeSchedulerQueueBatch(SchedulerImpl *impl)
{
    SchedulerQueueBatch batch;
    QMutexLocker locker(&impl->mutex);

    while (true) {
        if (impl->queues.runningCount() >= impl->config.maxConcurrentRequests()) {
            break;
        }

        if (impl->config.enableThrottling() && impl->config.maxBandwidthBytesPerSec() > 0
            && impl->bytesTransferredInWindow >= impl->config.maxBandwidthBytesPerSec()) {
            batch.shouldEmitBandwidthThrottled = true;
            batch.throttledBytesPerSec         = impl->bytesTransferredInWindow;
            break;
        }

        // 调度顺序固定为：per-host reservation → lane global reservation → DRR best-effort。
        Internal::SchedulerQueues::QueuedRequest request;
        if (!impl->queues.takeNextPending(impl->config, &request)) {
            break;
        }

        impl->stats.setPendingRequests(impl->queues.pendingCount());
        impl->queues.markRunning(request);
        impl->replyStates[request.key] = Internal::ScheduledState::Running;
        impl->requestStartTimes.insert(request.key, QDateTime::currentDateTime());
        impl->stats.setRunningRequests(impl->queues.runningCount());
        batch.toStart.append(request.reply);
    }

    batch.shouldEmitQueueEmpty = !impl->queues.hasPendingRequests();
    return batch;
}

QList<Internal::ReplyKey> collectLaneReplyKeys(
    const Internal::SchedulerQueues &queues,
    const QHash<Internal::ReplyKey, Internal::ReplySnapshot> &replySnapshots,
    const QString &lane,
    QCNetworkRequestScheduler::CancelLaneScope scope)
{
    QList<Internal::ReplyKey> keys;
    const auto collectKeysFromQueue =
        [&](const QList<Internal::SchedulerQueues::QueuedRequest> &queue) {
            for (const auto &request : queue) {
                if (!request.reply || request.snapshot.lane != lane || keys.contains(request.key)) {
                    continue;
                }
                keys.append(request.key);
            }
        };

    collectKeysFromQueue(queues.pendingRequests());
    collectKeysFromQueue(queues.deferredRequests());
    if (scope == QCNetworkRequestScheduler::CancelLaneScope::PendingAndRunning) {
        for (auto *reply : queues.runningRequests()) {
            const Internal::ReplyKey key           = Internal::replyKey(reply);
            const Internal::ReplySnapshot snapshot = replySnapshots.value(key);
            if (reply && snapshot.lane == lane && !keys.contains(key)) {
                keys.append(key);
            }
        }
    }
    return keys;
}

bool cancelRepliesAndNotify(QCNetworkRequestScheduler *scheduler,
                            const QList<QPointer<QCNetworkReply>> &replies,
                            const QList<Internal::ReplySnapshot> &snapshots)
{
    for (const auto &safeReply : replies) {
        if (safeReply) {
            Internal::invokeReplyCancel(safeReply.data());
        }
    }

    QPointer<QCNetworkRequestScheduler> safeScheduler(scheduler);
    for (int i = 0; i < replies.size(); ++i) {
        const auto &safeReply                   = replies.at(i);
        const Internal::ReplySnapshot &snapshot = snapshots.at(i);
        if (!safeScheduler || !safeReply) {
            return false;
        }
        Q_EMIT safeScheduler->requestCancelled(safeReply.data(), snapshot.lane, snapshot.hostKey);
        if (!safeScheduler || !safeReply) {
            return false;
        }
    }
    return safeScheduler;
}

} // namespace

QCNetworkRequestScheduler::CommandResult QCNetworkRequestScheduler::scheduleReply(
    QCNetworkReply *reply, const QCNetworkLaneKey &lane, QCNetworkRequestPriority priority)
{
    if (!reply) {
        return CommandResult::NullReply;
    }

    if (QThread::currentThread() != thread()) {
        return CommandResult::WrongThread;
    }
    Internal::assertSchedulerOwnerThread(this, "QCNetworkRequestScheduler::scheduleReply");
    if (reply->thread() != thread()) {
        return CommandResult::ThreadAffinityMismatch;
    }
    if (!lane.isValid() || !Internal::priorityOrder().contains(priority)) {
        return CommandResult::InvalidArgument;
    }

    const Internal::ReplyKey key = Internal::replyKey(reply);
    {
        QMutexLocker locker(&m_impl->mutex);
        if (m_impl->replyStates.contains(key)) {
            return CommandResult::NoChange;
        }
    }
    const QString laneKey                  = lane.isValid() ? lane.name() : QString();
    const Internal::ReplySnapshot snapshot = {laneKey,
                                              Internal::buildHostKey(reply->url()),
                                              priority};
    trackScheduledReply(m_impl.data(), reply, key, snapshot);

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

    QPointer<QCNetworkRequestScheduler> safeScheduler(this);
    Q_EMIT requestQueued(reply, snapshot.lane, snapshot.hostKey, snapshot.priority);
    if (!safeScheduler || !safeReply) {
        return CommandResult::Applied;
    }
    safeScheduler->processQueue();
    return CommandResult::Applied;
}

void QCNetworkRequestScheduler::processQueue()
{
    if (QThread::currentThread() != thread()) {
        Internal::invokeOnSchedulerOwnerThread(
            this, [this]() { processQueue(); }, "QCNetworkRequestScheduler::processQueue");
        return;
    }
    Internal::assertSchedulerOwnerThread(this, "QCNetworkRequestScheduler::processQueue");

    const SchedulerQueueBatch batch = takeSchedulerQueueBatch(m_impl.data());

    QPointer<QCNetworkRequestScheduler> safeScheduler(this);
    if (batch.shouldEmitBandwidthThrottled) {
        Q_EMIT bandwidthThrottled(batch.throttledBytesPerSec);
        if (!safeScheduler) {
            return;
        }
    }

    for (const auto &safeReply : batch.toStart) {
        if (!safeScheduler || !safeReply) {
            return;
        }
        safeScheduler->startRequest(safeReply.data());
        if (!safeScheduler || !safeReply) {
            return;
        }
    }

    if (batch.shouldEmitQueueEmpty) {
        Q_EMIT queueEmpty();
        if (!safeScheduler) {
            return;
        }
    }
}

void QCNetworkRequestScheduler::startRequest(QCNetworkReply *reply)
{
    if (QThread::currentThread() != thread()) {
        qWarning() << "QCNetworkRequestScheduler::startRequest: owner thread required";
        return;
    }
    Internal::assertSchedulerOwnerThread(this, "QCNetworkRequestScheduler::startRequest");
    if (!reply) {
        return;
    }
    Q_ASSERT_X(reply->thread() == thread(),
               "QCNetworkRequestScheduler::startRequest",
               "reply must live on the scheduler owner thread");

    const Internal::ReplyKey key                  = Internal::replyKey(reply);
    const Internal::SchedulerStartContext context = m_impl->prepareStartContext(this, reply, key);
    m_impl->dispatchReplyExecution(this, reply, key, context);
}

QCNetworkRequestScheduler::CommandResult QCNetworkRequestScheduler::cancelAllRequests()
{
    if (QThread::currentThread() != thread()) {
        return CommandResult::WrongThread;
    }
    Internal::assertSchedulerOwnerThread(this, "QCNetworkRequestScheduler::cancelAllRequests");

    QList<QPointer<QCNetworkReply>> replies;
    QList<Internal::ReplySnapshot> snapshots;
    QList<Internal::ReplyKey> keysToCancel;

    {
        QMutexLocker locker(&m_impl->mutex);

        // 先冻结待取消 key，再统一 finalize，避免边遍历边改动 queue/running 容器。
        keysToCancel = m_impl->replyStates.keys();

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

    if (replies.isEmpty()) {
        return CommandResult::NoChange;
    }

    if (!cancelRepliesAndNotify(this, replies, snapshots)) {
        return CommandResult::Applied;
    }

    QPointer<QCNetworkRequestScheduler> safeScheduler(this);
    if (!safeScheduler) {
        return CommandResult::Applied;
    }
    Q_EMIT safeScheduler->queueEmpty();
    if (!safeScheduler) {
        return CommandResult::Applied;
    }
    return CommandResult::Applied;
}

QCNetworkRequestScheduler::CommandResult QCNetworkRequestScheduler::cancelLaneRequests(
    const QString &lane, CancelLaneScope scope, int *cancelledRequests)
{
    if (cancelledRequests) {
        *cancelledRequests = 0;
    }
    if (QThread::currentThread() != thread()) {
        return CommandResult::WrongThread;
    }
    Internal::assertSchedulerOwnerThread(this, "QCNetworkRequestScheduler::cancelLaneRequests");
    if (scope != CancelLaneScope::PendingOnly && scope != CancelLaneScope::PendingAndRunning) {
        return CommandResult::InvalidArgument;
    }

    const QString laneKey = Internal::normalizedLane(lane);
    QList<QPointer<QCNetworkReply>> replies;
    QList<Internal::ReplySnapshot> snapshots;
    bool shouldKickQueue = false;
    QList<Internal::ReplyKey> keysToCancel;

    {
        QMutexLocker locker(&m_impl->mutex);

        // 先收集目标 lane 的 key，再统一 finalize，避免 queue 迭代过程中失效。
        keysToCancel = collectLaneReplyKeys(m_impl->queues, m_impl->replySnapshots, laneKey, scope);

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

    const int cancelledCount = replies.size();
    if (cancelledRequests) {
        *cancelledRequests = cancelledCount;
    }
    if (cancelledCount == 0) {
        return CommandResult::NoChange;
    }
    if (!cancelRepliesAndNotify(this, replies, snapshots)) {
        return CommandResult::Applied;
    }

    if (shouldKickQueue) {
        QPointer<QCNetworkRequestScheduler> safeScheduler(this);
        if (safeScheduler) {
            safeScheduler->processQueue();
        }
    }

    return CommandResult::Applied;
}

QCNetworkRequestScheduler::CommandResult QCNetworkRequestScheduler::changePriority(
    QCNetworkReply *reply, QCNetworkRequestPriority newPriority)
{
    if (!reply) {
        return CommandResult::NullReply;
    }

    if (QThread::currentThread() != thread()) {
        return CommandResult::WrongThread;
    }
    Internal::assertSchedulerOwnerThread(this, "QCNetworkRequestScheduler::changePriority");
    if (reply->thread() != thread()) {
        return CommandResult::ThreadAffinityMismatch;
    }
    if (!Internal::priorityOrder().contains(newPriority)) {
        return CommandResult::InvalidArgument;
    }

    const Internal::ReplyKey key = Internal::replyKey(reply);
    Internal::ReplySnapshot snapshot;
    {
        QMutexLocker locker(&m_impl->mutex);
        const auto stateIt = m_impl->replyStates.constFind(key);
        if (stateIt == m_impl->replyStates.cend()) {
            return CommandResult::NotTracked;
        }
        if (stateIt.value() != Internal::ScheduledState::Pending) {
            return CommandResult::InvalidState;
        }
        if (m_impl->replySnapshots.value(key).priority == newPriority) {
            return CommandResult::NoChange;
        }
        if (!m_impl->queues.updatePendingPriority(key, newPriority, &snapshot)) {
            return CommandResult::InvalidState;
        }

        m_impl->replySnapshots[key].priority = newPriority;
    }

    QPointer<QCNetworkRequestScheduler> safeScheduler(this);
    QPointer<QCNetworkReply> safeReply(reply);
    Q_EMIT requestQueued(reply, snapshot.lane, snapshot.hostKey, snapshot.priority);
    if (!safeScheduler || !safeReply) {
        return CommandResult::Applied;
    }
    safeScheduler->processQueue();
    return CommandResult::Applied;
}

} // namespace QCurl
