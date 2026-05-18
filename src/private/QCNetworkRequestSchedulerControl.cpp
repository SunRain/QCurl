// SPDX-License-Identifier: MIT
// Copyright (c) 2025 QCurl Project

#include "QCNetworkRequestScheduler.h"

#include "QCNetworkReply.h"
#include "private/QCNetworkRequestSchedulerPrivate_p.h"

#include <QMutexLocker>
#include <QPointer>
#include <QThread>

namespace QCurl {

bool QCNetworkRequestScheduler::deferPendingRequest(QCNetworkReply *reply)
{
    if (!reply) {
        return false;
    }

    if (QThread::currentThread() != thread()) {
        return Internal::rejectOffOwnerThreadValue(this, false, "QCNetworkRequestScheduler::deferPendingRequest");
    }
    Internal::assertSchedulerOwnerThread(this, "QCNetworkRequestScheduler::deferPendingRequest");

    const Internal::ReplyKey key = Internal::replyKey(reply);
    bool deferred = false;

    {
        QMutexLocker locker(&m_impl->mutex);

        // defer 只重排调度队列，不触碰 reply 的传输状态。
        Internal::SchedulerQueues::QueuedRequest request;
        if (Internal::SchedulerQueues::takeQueuedRequest(
                m_impl->queues.pendingRequests, key, &request)) {
            m_impl->queues.deferredRequests.append(request);
            m_impl->replyStates[key] = Internal::ScheduledState::Deferred;
            m_impl->stats.setPendingRequests(m_impl->queues.pendingRequests.size());
            deferred = true;
        }
    }

    if (deferred) {
        processQueue();
    }

    return deferred;
}

void QCNetworkRequestScheduler::undeferRequest(QCNetworkReply *reply)
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
                    undeferRequest(safeReply.data());
                }
            },
            "QCNetworkRequestScheduler::undeferRequest");
        return;
    }
    Internal::assertSchedulerOwnerThread(this, "QCNetworkRequestScheduler::undeferRequest");

    const Internal::ReplyKey key = Internal::replyKey(reply);
    Internal::SchedulerQueues::QueuedRequest request;
    {
        QMutexLocker locker(&m_impl->mutex);
        if (!Internal::SchedulerQueues::takeQueuedRequest(
                m_impl->queues.deferredRequests, key, &request)) {
            return;
        }
        request.queueTime = QDateTime::currentDateTime();
        m_impl->queues.pendingRequests.append(request);
        m_impl->replyStates[key] = Internal::ScheduledState::Pending;
        m_impl->stats.setPendingRequests(m_impl->queues.pendingRequests.size());
    }

    emit requestQueued(
        reply, request.snapshot.lane, request.snapshot.hostKey, request.snapshot.priority);
    processQueue();
}

void QCNetworkRequestScheduler::cancelRequest(QCNetworkReply *reply)
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
                    cancelRequest(safeReply.data());
                }
            },
            "QCNetworkRequestScheduler::cancelRequest");
        return;
    }
    Internal::assertSchedulerOwnerThread(this, "QCNetworkRequestScheduler::cancelRequest");

    const Internal::ReplyKey key = Internal::replyKey(reply);
    Internal::FinalizeResult result;

    {
        QMutexLocker locker(&m_impl->mutex);
        result = m_impl->finalizeReplyLocked(key, Internal::FinalizeTrigger::ExplicitCancel);
    }

    if (!result.wasTracked) {
        return;
    }

    Internal::invokeReplyCancel(reply);
    if (result.emitCancelled) {
        emit requestCancelled(reply, result.snapshot.lane, result.snapshot.hostKey);
    }

    if (result.shouldKickQueue) {
        processQueue();
    }
}

bool QCNetworkRequestScheduler::removeFromQueue(QCNetworkReply *reply)
{
    if (!reply) {
        return false;
    }

    if (QThread::currentThread() != thread()) {
        return Internal::rejectOffOwnerThreadValue(this, false, "QCNetworkRequestScheduler::removeFromQueue");
    }
    Internal::assertSchedulerOwnerThread(this, "QCNetworkRequestScheduler::removeFromQueue");

    QMutexLocker locker(&m_impl->mutex);
    return Internal::SchedulerQueues::takeQueuedRequest(
        m_impl->queues.pendingRequests, Internal::replyKey(reply));
}

} // namespace QCurl
