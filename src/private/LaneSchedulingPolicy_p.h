/**
 * @file
 * @brief Declares the pure lane scheduling selection policy.
 */

#ifndef LANESCHEDULINGPOLICY_P_H
#define LANESCHEDULINGPOLICY_P_H

#include "private/QCNetworkRequestSchedulerQueue_p.h"

#include <QtGlobal>

namespace QCurl {

namespace Internal {

class Q_DECL_HIDDEN LaneSchedulingPolicy
{
public:
    [[nodiscard]] SchedulerRequestId selectNextRequestId(
        SchedulerQueues &queues,
        const QCNetworkRequestScheduler::Config &config) const;
};

} // namespace Internal

} // namespace QCurl

#endif // LANESCHEDULINGPOLICY_P_H
