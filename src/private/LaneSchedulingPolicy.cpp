// SPDX-License-Identifier: MIT
// Copyright (c) 2025 QCurl Project

#include "private/LaneSchedulingPolicy_p.h"

#include "private/QCNetworkRequestSchedulerQueue_p.h"

namespace QCurl {

namespace Internal {

SchedulerRequestId LaneSchedulingPolicy::selectNextRequestId(
    SchedulerQueues &queues,
    const QCNetworkRequestScheduler::Config &config) const
{
    int nextIndex = queues.selectReservationHostIndex(config);
    if (nextIndex < 0) {
        nextIndex = queues.selectReservationGlobalIndex(config);
    }
    if (nextIndex < 0) {
        nextIndex = queues.selectBestEffortIndex(config);
    }
    if (nextIndex < 0) {
        return 0;
    }
    return queues.pendingRequests.at(nextIndex).requestId;
}

} // namespace Internal

} // namespace QCurl
