/**
 * @file
 * @brief Private state for QCNetworkRequestScheduler implementation slices.
 */

#ifndef QCNETWORKREQUESTSCHEDULERPRIVATE_P_H
#define QCNETWORKREQUESTSCHEDULERPRIVATE_P_H

#include "QCNetworkRequestScheduler.h"
#include "private/QCNetworkRequestSchedulerDispatch_p.h"
#include "private/QCNetworkRequestSchedulerQueue_p.h"

#include <QDateTime>
#include <QHash>
#include <QMutex>
#include <QSet>

class QTimer;

namespace QCurl {

struct QCNetworkRequestScheduler::Impl
{
    mutable QMutex mutex;
    QCNetworkRequestScheduler::Config config;
    Internal::SchedulerQueues queues;
    QCNetworkRequestScheduler::Statistics stats;
    QHash<Internal::ReplyKey, QDateTime> requestStartTimes;
    QHash<Internal::ReplyKey, Internal::ReplySnapshot> replySnapshots;
    QHash<Internal::ReplyKey, Internal::ScheduledState> replyStates;
    QHash<Internal::ReplyKey, Internal::ReplyProgressState> replyProgressStates;
    QSet<Internal::ReplyKey> cancelledReplies;
    quint64 nextRequestId = 1;
    quint64 nextStartTicket = 1;
    QHash<Internal::ReplyKey, quint64> startTickets;
    QTimer *throttleTimer           = nullptr;
    qint64 bytesTransferredInWindow = 0;

    Internal::SchedulerStartContext prepareStartContext(QCNetworkRequestScheduler *scheduler,
                                                          QCNetworkReply *reply,
                                                          Internal::ReplyKey key);
    void dispatchReplyExecution(QCNetworkRequestScheduler *scheduler,
                                QCNetworkReply *reply,
                                Internal::ReplyKey key,
                                const Internal::SchedulerStartContext &context);
    [[nodiscard]] bool resetBandwidthWindow();
    void connectProgressTracking(QCNetworkRequestScheduler *scheduler,
                                 QCNetworkReply *reply,
                                 Internal::ReplyKey key);
    void disconnectProgressTracking(Internal::ReplyKey key);
    void clearReplyTracking(Internal::ReplyKey key);
    [[nodiscard]] bool isStartTicketValidLocked(Internal::ReplyKey key, quint64 ticket) const;
    Internal::FinalizeResult finalizeReplyLocked(Internal::ReplyKey key,
                                                Internal::FinalizeTrigger trigger,
                                                const Internal::ReplyOutcome &outcome = {});
};

} // namespace QCurl

#endif // QCNETWORKREQUESTSCHEDULERPRIVATE_P_H
