/**
 * @file
 * @brief Declares cleanup for temporary scheduler lane runtime state.
 */

#ifndef LANERUNTIMEPRUNER_P_H
#define LANERUNTIMEPRUNER_P_H

#include <QtGlobal>

namespace QCurl {

namespace Internal {

class SchedulerQueues;

class Q_DECL_HIDDEN LaneRuntimePruner
{
public:
    void pruneIdleTemporaryLanes(SchedulerQueues *queues) const;
};

} // namespace Internal

} // namespace QCurl

#endif // LANERUNTIMEPRUNER_P_H
