/**
 * @file
 * @brief Private state helpers for QCNetworkRequestScheduler.
 */

#include "private/QCNetworkRequestSchedulerPrivate_p.h"

#include "QCNetworkReply.h"
#include "private/LaneRuntimePruner_p.h"

#include <QMutexLocker>

namespace QCurl {

void QCNetworkRequestScheduler::Impl::connectProgressTracking(QCNetworkRequestScheduler *scheduler,
                                                              QCNetworkReply *reply,
                                                              Internal::ReplyKey key)
{
    disconnectProgressTracking(key);
    auto &progressState = replyProgressStates[key];

    progressState.downloadConnection = QObject::connect(
        reply,
        &QCNetworkReply::downloadProgress,
        scheduler,
        [this, key](qint64 bytesReceived, qint64 /*bytesTotal*/) {
            QMutexLocker locker(&mutex);
            auto it = replyProgressStates.find(key);
            if (it == replyProgressStates.end()) {
                return;
            }

            const qint64 delta = qMax<qint64>(0, bytesReceived - it->lastBytesReceived);
            it->lastBytesReceived = bytesReceived;
            bytesTransferredInWindow += delta;
        },
        Qt::QueuedConnection);

    progressState.uploadConnection = QObject::connect(
        reply,
        &QCNetworkReply::uploadProgress,
        scheduler,
        [this, key](qint64 bytesSent, qint64 /*bytesTotal*/) {
            QMutexLocker locker(&mutex);
            auto it = replyProgressStates.find(key);
            if (it == replyProgressStates.end()) {
                return;
            }

            const qint64 delta = qMax<qint64>(0, bytesSent - it->lastBytesSent);
            it->lastBytesSent = bytesSent;
            bytesTransferredInWindow += delta;
        },
        Qt::QueuedConnection);
}

void QCNetworkRequestScheduler::Impl::disconnectProgressTracking(Internal::ReplyKey key)
{
    auto it = replyProgressStates.find(key);
    if (it == replyProgressStates.end()) {
        return;
    }

    QObject::disconnect(it->downloadConnection);
    QObject::disconnect(it->uploadConnection);
    replyProgressStates.erase(it);
}

void QCNetworkRequestScheduler::Impl::clearReplyTracking(Internal::ReplyKey key)
{
    disconnectProgressTracking(key);
    requestStartTimes.remove(key);
    replyStates.remove(key);
    replySnapshots.remove(key);
    cancelledReplies.remove(key);
    startTickets.remove(key);
}

bool QCNetworkRequestScheduler::Impl::isStartTicketValidLocked(Internal::ReplyKey key,
                                                               quint64 ticket) const
{
    if (!key || cancelledReplies.contains(key)) {
        return false;
    }

    const auto stateIt = replyStates.constFind(key);
    if (stateIt == replyStates.constEnd() || stateIt.value() != Internal::ScheduledState::Running) {
        return false;
    }

    const auto ticketIt = startTickets.constFind(key);
    return ticketIt != startTickets.constEnd() && ticketIt.value() == ticket;
}

Internal::FinalizeResult QCNetworkRequestScheduler::Impl::finalizeReplyLocked(
    Internal::ReplyKey key,
    Internal::FinalizeTrigger trigger,
    const Internal::ReplyOutcome &outcome)
{
    Internal::FinalizeResult result;
    if (!key) {
        return result;
    }

    if (!replySnapshots.contains(key) && !replyStates.contains(key) && !cancelledReplies.contains(key)) {
        return result;
    }

    result.wasTracked = true;
    result.snapshot   = replySnapshots.value(key);

    if (trigger == Internal::FinalizeTrigger::FinishedSignal && cancelledReplies.remove(key)) {
        disconnectProgressTracking(key);
        return result;
    }

    const Internal::ScheduledState state = replyStates.value(key, Internal::ScheduledState::Pending);

    if (state == Internal::ScheduledState::Pending) {
        if (Internal::SchedulerQueues::takeQueuedRequest(queues.pendingRequests, key)) {
            stats.setPendingRequests(queues.pendingRequests.size());
            result.shouldKickQueue = trigger != Internal::FinalizeTrigger::ExplicitCancel;
        }
    } else if (state == Internal::ScheduledState::Deferred) {
        if (Internal::SchedulerQueues::takeQueuedRequest(queues.deferredRequests, key)) {
            result.shouldKickQueue = trigger == Internal::FinalizeTrigger::Destroyed;
        }
    } else if (queues.removeRunning(Internal::replyFromKey(key), result.snapshot)) {
        stats.setRunningRequests(queues.runningRequests.size());
        result.shouldKickQueue = true;
    }

    if (trigger == Internal::FinalizeTrigger::ExplicitCancel) {
        disconnectProgressTracking(key);
        requestStartTimes.remove(key);
        replyStates.remove(key);
        replySnapshots.remove(key);
        startTickets.remove(key);
        cancelledReplies.insert(key);
        stats.setCancelledRequests(stats.cancelledRequests() + 1);
        result.emitCancelled = true;
        return result;
    }

    if (trigger == Internal::FinalizeTrigger::FinishedSignal) {
        if (outcome.cancelled) {
            stats.setCancelledRequests(stats.cancelledRequests() + 1);
            result.emitCancelled = true;
        } else if (state == Internal::ScheduledState::Running) {
            const QDateTime finishTime = QDateTime::currentDateTime();
            const QDateTime startTime  = requestStartTimes.value(key);
            const qint64 duration      = startTime.isValid() ? startTime.msecsTo(finishTime) : 0;

            stats.setCompletedRequests(stats.completedRequests() + 1);
            if (stats.completedRequests() == 1) {
                stats.setAvgResponseTime(duration);
            } else {
                const double rollingAverage
                    = (stats.avgResponseTime() * (stats.completedRequests() - 1) + duration)
                      / stats.completedRequests();
                stats.setAvgResponseTime(rollingAverage);
            }
            stats.setTotalBytesReceived(stats.totalBytesReceived() + outcome.bytesReceived);
            result.emitFinished = true;
        }
    }

    clearReplyTracking(key);
    Internal::LaneRuntimePruner{}.pruneIdleTemporaryLanes(&queues);
    return result;
}

} // namespace QCurl
