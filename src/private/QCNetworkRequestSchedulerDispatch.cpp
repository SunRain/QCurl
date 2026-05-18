/**
 * @file
 * @brief Owner-thread dispatch helpers for QCNetworkRequestScheduler.
 */

#include "private/QCNetworkRequestSchedulerPrivate_p.h"

#include "QCNetworkReply.h"

#include <QMutexLocker>
#include <QPointer>

namespace QCurl {

Internal::SchedulerStartContext QCNetworkRequestScheduler::Impl::prepareStartContext(
    QCNetworkRequestScheduler *scheduler,
    QCNetworkReply *reply,
    Internal::ReplyKey key)
{
    Internal::SchedulerStartContext context;
    QMutexLocker locker(&mutex);
    connectProgressTracking(scheduler, reply, key);
    context.snapshot = replySnapshots.value(key);
    context.startTicket = nextStartTicket++;
    startTickets.insert(key, context.startTicket);
    return context;
}

void QCNetworkRequestScheduler::Impl::dispatchReplyExecution(
    QCNetworkRequestScheduler *scheduler,
    QCNetworkReply *reply,
    Internal::ReplyKey key,
    const Internal::SchedulerStartContext &context)
{
    QPointer<QCNetworkRequestScheduler> safeScheduler(scheduler);
    QPointer<QCNetworkReply> safeReply(reply);

    QMetaObject::invokeMethod(
        reply,
        [safeScheduler, safeReply, snapshot = context.snapshot, key, ticket = context.startTicket]() {
            if (!safeScheduler || !safeReply) {
                return;
            }

            auto isStartStillValid = [&]() -> bool {
                QMutexLocker locker(&safeScheduler->m_impl->mutex);
                return safeScheduler->m_impl->isStartTicketValidLocked(key, ticket);
            };

            if (!isStartStillValid()) {
                return;
            }

            if (safeReply->state() == ReplyState::Cancelled || safeReply->isFinished()) {
                return;
            }

            Q_EMIT safeScheduler->requestAboutToStart(safeReply.data(), snapshot.lane, snapshot.hostKey);

            if (!safeScheduler || !safeReply || !isStartStillValid()) {
                return;
            }

            if (safeReply->state() == ReplyState::Cancelled || safeReply->isFinished()) {
                return;
            }

            safeReply->execute();

            if (!safeScheduler || !safeReply || !isStartStillValid()) {
                return;
            }

            Q_EMIT safeScheduler->requestStarted(safeReply.data(), snapshot.lane, snapshot.hostKey);
        },
        Qt::QueuedConnection);
}

bool QCNetworkRequestScheduler::Impl::resetBandwidthWindow()
{
    QMutexLocker locker(&mutex);
    bytesTransferredInWindow = 0;
    return !queues.pendingRequests.isEmpty();
}

} // namespace QCurl
