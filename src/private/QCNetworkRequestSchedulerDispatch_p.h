/**
 * @file
 * @brief Owner-thread dispatch helpers for QCNetworkRequestScheduler.
 */

#ifndef QCNETWORKREQUESTSCHEDULERDISPATCH_P_H
#define QCNETWORKREQUESTSCHEDULERDISPATCH_P_H

#include "private/QCNetworkRequestSchedulerQueue_p.h"

namespace QCurl {

namespace Internal {

struct SchedulerStartContext
{
    ReplySnapshot snapshot;
    quint64 startTicket = 0;
};

} // namespace Internal

} // namespace QCurl

#endif // QCNETWORKREQUESTSCHEDULERDISPATCH_P_H
