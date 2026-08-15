// SPDX-License-Identifier: MIT
// Copyright (c) 2025 QCurl Project

#include "QCNetworkReply.h"
#include "QCNetworkRequestScheduler.h"
#include "private/QCNetworkRequestSchedulerPrivate_p.h"

#include <QMutexLocker>
#include <QPointer>
#include <QThread>

namespace QCurl {

QCNetworkRequestScheduler::CommandResult QCNetworkRequestScheduler::deferPendingRequest(
    QCNetworkReply *reply)
{
    if (!reply) {
        return CommandResult::NullReply;
    }

    if (QThread::currentThread() != thread()) {
        return CommandResult::WrongThread;
    }
    Internal::assertSchedulerOwnerThread(this, "QCNetworkRequestScheduler::deferPendingRequest");
    if (reply->thread() != thread()) {
        return CommandResult::ThreadAffinityMismatch;
    }

    const Internal::ReplyKey key = Internal::replyKey(reply);
    CommandResult result         = CommandResult::NotTracked;

    {
        QMutexLocker locker(&m_impl->mutex);

        const auto stateIt = m_impl->replyStates.constFind(key);
        if (stateIt == m_impl->replyStates.cend()) {
            return CommandResult::NotTracked;
        }
        if (stateIt.value() != Internal::ScheduledState::Pending) {
            return CommandResult::InvalidState;
        }

        // defer 只重排调度队列，不触碰 reply 的传输状态。
        if (m_impl->queues.deferPending(key)) {
            m_impl->replyStates[key] = Internal::ScheduledState::Deferred;
            m_impl->stats.setPendingRequests(m_impl->queues.pendingCount());
            result = CommandResult::Applied;
        }
    }

    if (result == CommandResult::Applied) {
        processQueue();
    }

    return result;
}

QCNetworkRequestScheduler::CommandResult QCNetworkRequestScheduler::undeferRequest(
    QCNetworkReply *reply)
{
    if (!reply) {
        return CommandResult::NullReply;
    }

    if (QThread::currentThread() != thread()) {
        return CommandResult::WrongThread;
    }
    Internal::assertSchedulerOwnerThread(this, "QCNetworkRequestScheduler::undeferRequest");
    if (reply->thread() != thread()) {
        return CommandResult::ThreadAffinityMismatch;
    }

    const Internal::ReplyKey key = Internal::replyKey(reply);
    Internal::SchedulerQueues::QueuedRequest request;
    {
        QMutexLocker locker(&m_impl->mutex);
        const auto stateIt = m_impl->replyStates.constFind(key);
        if (stateIt == m_impl->replyStates.cend()) {
            return CommandResult::NotTracked;
        }
        if (stateIt.value() != Internal::ScheduledState::Deferred) {
            return CommandResult::InvalidState;
        }
        if (!m_impl->queues.resumeDeferred(key, &request)) {
            return CommandResult::InvalidState;
        }
        m_impl->replyStates[key] = Internal::ScheduledState::Pending;
        m_impl->stats.setPendingRequests(m_impl->queues.pendingCount());
    }

    QPointer<QCNetworkRequestScheduler> safeScheduler(this);
    QPointer<QCNetworkReply> safeReply(reply);
    Q_EMIT safeScheduler->requestQueued(safeReply.data(),
                                        request.snapshot.lane,
                                        request.snapshot.hostKey,
                                        request.snapshot.priority);
    if (!safeScheduler || !safeReply) {
        return CommandResult::Applied;
    }
    safeScheduler->processQueue();
    return CommandResult::Applied;
}

QCNetworkRequestScheduler::CommandResult QCNetworkRequestScheduler::cancelRequest(
    QCNetworkReply *reply)
{
    if (!reply) {
        return CommandResult::NullReply;
    }

    if (QThread::currentThread() != thread()) {
        return CommandResult::WrongThread;
    }
    Internal::assertSchedulerOwnerThread(this, "QCNetworkRequestScheduler::cancelRequest");
    if (reply->thread() != thread()) {
        return CommandResult::ThreadAffinityMismatch;
    }

    const Internal::ReplyKey key = Internal::replyKey(reply);
    Internal::FinalizeResult result;

    {
        QMutexLocker locker(&m_impl->mutex);
        result = m_impl->finalizeReplyLocked(key, Internal::FinalizeTrigger::ExplicitCancel);
    }

    if (!result.wasTracked) {
        return CommandResult::NotTracked;
    }

    QPointer<QCNetworkRequestScheduler> safeScheduler(this);
    QPointer<QCNetworkReply> safeReply(reply);
    Internal::invokeReplyCancel(safeReply.data());
    if (!safeScheduler || !safeReply) {
        return CommandResult::Applied;
    }
    if (result.emitCancelled) {
        Q_EMIT safeScheduler->requestCancelled(safeReply.data(),
                                               result.snapshot.lane,
                                               result.snapshot.hostKey);
        if (!safeScheduler || !safeReply) {
            return CommandResult::Applied;
        }
    }

    if (result.shouldKickQueue) {
        safeScheduler->processQueue();
    }
    return CommandResult::Applied;
}

bool QCNetworkRequestScheduler::removeFromQueue(QCNetworkReply *reply)
{
    if (!reply) {
        return false;
    }

    if (QThread::currentThread() != thread()) {
        return Internal::rejectOffOwnerThreadValue(this,
                                                   false,
                                                   "QCNetworkRequestScheduler::removeFromQueue");
    }
    Internal::assertSchedulerOwnerThread(this, "QCNetworkRequestScheduler::removeFromQueue");

    QMutexLocker locker(&m_impl->mutex);
    return m_impl->queues.takePending(Internal::replyKey(reply));
}

} // namespace QCurl
