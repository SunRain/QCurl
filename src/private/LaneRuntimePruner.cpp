// SPDX-License-Identifier: MIT
// Copyright (c) 2025 QCurl Project

#include "private/LaneRuntimePruner_p.h"

#include "private/QCNetworkRequestSchedulerQueue_p.h"

#include <QSet>

namespace QCurl {

namespace Internal {

namespace {

void collectQueuedLanes(const QList<SchedulerQueues::QueuedRequest> &requests,
                        QSet<QString> *activeLanes)
{
    for (const auto &request : requests) {
        if (request.reply) {
            activeLanes->insert(request.snapshot.lane);
        }
    }
}

} // namespace

void LaneRuntimePruner::pruneIdleTemporaryLanes(SchedulerQueues *queues) const
{
    if (!queues) {
        return;
    }

    QSet<QString> activeLanes;
    collectQueuedLanes(queues->pendingRequests, &activeLanes);
    collectQueuedLanes(queues->deferredRequests, &activeLanes);
    for (auto it = queues->runningLaneCount.cbegin(); it != queues->runningLaneCount.cend(); ++it) {
        if (it.value() > 0) {
            activeLanes.insert(it.key());
        }
    }

    QStringList retainedLanes;
    retainedLanes.reserve(queues->laneOrder.size());
    for (const QString &lane : std::as_const(queues->laneOrder)) {
        if (queues->laneConfigs.contains(lane) || activeLanes.contains(lane)) {
            retainedLanes.append(lane);
            continue;
        }
        queues->laneDeficit.remove(lane);
        queues->laneLastStartedHost.remove(lane);
    }
    queues->laneOrder = retainedLanes;
}

} // namespace Internal

} // namespace QCurl
