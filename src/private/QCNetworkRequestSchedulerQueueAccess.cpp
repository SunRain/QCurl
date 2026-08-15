/**
 * @file
 * @brief QCNetworkRequestScheduler queue access and transition implementation.
 */

#include "private/QCNetworkRequestSchedulerQueue_p.h"

namespace QCurl {

namespace Internal {

bool SchedulerQueues::takeNextPending(const QCNetworkRequestScheduler::Config &config,
                                      QueuedRequest *out)
{
    if (!out) {
        return false;
    }
    const int index = selectNextIndex(config);
    if (index < 0) {
        return false;
    }
    *out = m_pendingRequests.takeAt(index);
    return true;
}

bool SchedulerQueues::updatePendingPriority(ReplyKey key,
                                            QCNetworkRequestPriority priority,
                                            ReplySnapshot *snapshot)
{
    const int index = findQueuedRequestIndex(m_pendingRequests, [key](const QueuedRequest &request) {
        return request.key == key;
    });
    if (index < 0) {
        return false;
    }
    m_pendingRequests[index].snapshot.priority = priority;
    if (snapshot) {
        *snapshot = m_pendingRequests.at(index).snapshot;
    }
    return true;
}

} // namespace Internal

} // namespace QCurl
