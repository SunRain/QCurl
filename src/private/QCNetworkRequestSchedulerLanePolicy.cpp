/**
 * @file
 * @brief QCNetworkRequestScheduler lane selection policy implementation.
 */

#include "private/QCNetworkRequestSchedulerQueue_p.h"

#include <QSet>

#include <algorithm>

namespace QCurl {

namespace Internal {

namespace {

void reserveLaneHosts(const QList<SchedulerQueues::QueuedRequest> &pendingRequests,
                      const QString &lane,
                      ReplyKey excludeKey,
                      QSet<QString> *hosts)
{
    for (const auto &request : pendingRequests) {
        if (!request.reply || request.key == excludeKey || request.snapshot.lane != lane) {
            continue;
        }
        hosts->insert(request.snapshot.hostKey);
    }
}

} // namespace

QStringList SchedulerQueues::rotatedLaneHosts(const QString &lane) const
{
    // 同一 lane 内按 host 轮转，避免 per-host 限流让队首 host 长时间阻塞其他 host。
    QStringList hosts;
    QSet<QString> seenHosts;

    for (const auto &request : pendingRequests) {
        if (!request.reply || request.snapshot.lane != lane) {
            continue;
        }
        if (seenHosts.contains(request.snapshot.hostKey)) {
            continue;
        }
        hosts.append(request.snapshot.hostKey);
        seenHosts.insert(request.snapshot.hostKey);
    }

    const QString lastStartedHost = laneLastStartedHost.value(lane);
    if (hosts.size() <= 1 || lastStartedHost.isEmpty()) {
        return hosts;
    }

    const int lastIndex = hosts.indexOf(lastStartedHost);
    if (lastIndex < 0) {
        return hosts;
    }

    QStringList rotated;
    rotated.reserve(hosts.size());
    for (int i = 1; i <= hosts.size(); ++i) {
        rotated.append(hosts.at((lastIndex + i) % hosts.size()));
    }
    return rotated;
}

bool SchedulerQueues::hasRunnablePendingExcluding(
    const QString &lane,
    ReplyKey excludeKey,
    const QHash<QString, int> &hostCounts,
    const QCNetworkRequestScheduler::Config &config) const
{
    for (const auto &request : pendingRequests) {
        if (!request.reply || request.key == excludeKey || request.snapshot.lane != lane) {
            continue;
        }
        if (hostCounts.value(request.snapshot.hostKey, 0) < config.maxRequestsPerHost()) {
            return true;
        }
    }
    return false;
}

bool SchedulerQueues::hasRunnablePendingForHostExcluding(
    const QString &lane,
    const QString &hostKey,
    ReplyKey excludeKey,
    const QHash<QString, int> &hostCounts,
    const QCNetworkRequestScheduler::Config &config) const
{
    for (const auto &request : pendingRequests) {
        if (!request.reply || request.key == excludeKey) {
            continue;
        }
        if (request.snapshot.lane != lane || request.snapshot.hostKey != hostKey) {
            continue;
        }
        if (hostCounts.value(hostKey, 0) < config.maxRequestsPerHost()) {
            return true;
        }
    }
    return false;
}

bool SchedulerQueues::wouldViolateReservation(
    const QueuedRequest &candidate,
    const QCNetworkRequestScheduler::Config &config) const
{
    // best-effort 选路前先“试占”一个槽位，避免偷走 reservation 仍需保底的容量。
    QHash<QString, int> hostCounts = hostConnectionCount;
    hostCounts[candidate.snapshot.hostKey]++;
    if (hostCounts.value(candidate.snapshot.hostKey, 0) > config.maxRequestsPerHost()) {
        return true;
    }

    QHash<QString, int> laneCounts = runningLaneCount;
    laneCounts[candidate.snapshot.lane]++;

    QHash<QString, QHash<QString, int>> laneHostCounts = runningLaneHostCount;
    incrementNestedCounter(laneHostCounts, candidate.snapshot.lane, candidate.snapshot.hostKey);

    const int freeGlobalAfter = config.maxConcurrentRequests() - (runningRequests.size() + 1);
    int globalDemand          = 0;
    QHash<QString, int> hostDemand;

    for (const auto &lane : laneOrder) {
        const auto laneCfg = laneConfigFor(lane);
        if (laneCfg.reservedGlobal() > 0
            && hasRunnablePendingExcluding(lane, candidate.key, hostCounts, config)) {
            globalDemand += std::max(0, laneCfg.reservedGlobal() - laneCounts.value(lane, 0));
        }

        if (laneCfg.reservedPerHost() <= 0) {
            continue;
        }

        QSet<QString> hosts;
        reserveLaneHosts(pendingRequests, lane, candidate.key, &hosts);

        for (const auto &hostKey : hosts) {
            if (!hasRunnablePendingForHostExcluding(
                    lane, hostKey, candidate.key, hostCounts, config)) {
                continue;
            }
            hostDemand[hostKey]
                += std::max(0,
                            laneCfg.reservedPerHost()
                                - nestedCounter(laneHostCounts, lane, hostKey));
        }
    }

    if (globalDemand > freeGlobalAfter) {
        return true;
    }

    for (auto it = hostDemand.cbegin(); it != hostDemand.cend(); ++it) {
        const int freeHostAfter = config.maxRequestsPerHost() - hostCounts.value(it.key(), 0);
        if (it.value() > freeHostAfter) {
            return true;
        }
    }

    return false;
}

int SchedulerQueues::selectReservationHostIndex(const QCNetworkRequestScheduler::Config &config)
{
    QStringList activeLanes;
    for (const auto &lane : laneOrder) {
        const auto laneCfg = laneConfigFor(lane);
        if (laneCfg.reservedPerHost() <= 0) {
            continue;
        }

        const bool active = findQueuedRequestIndex(
            pendingRequests,
            [&](const QueuedRequest &request) {
                return request.reply && request.snapshot.lane == lane
                       && hostConnectionCount.value(request.snapshot.hostKey, 0)
                              < config.maxRequestsPerHost()
                       && nestedCounter(runningLaneHostCount, lane, request.snapshot.hostKey)
                              < laneCfg.reservedPerHost();
            })
            >= 0;

        if (active) {
            activeLanes.append(lane);
        }
    }

    if (activeLanes.isEmpty()) {
        return -1;
    }

    const int laneCount = laneOrder.size();
    for (int attempt = 0; attempt < laneCount; ++attempt) {
        const QString &lane = laneOrder.at((hostReservationCursor + attempt) % laneCount);
        if (!activeLanes.contains(lane)) {
            continue;
        }

        const auto laneCfg = laneConfigFor(lane);
        const int index    = candidateIndexForLane(
            lane,
            [&](const QueuedRequest &request) {
                return hostConnectionCount.value(request.snapshot.hostKey, 0)
                           < config.maxRequestsPerHost()
                       && nestedCounter(runningLaneHostCount, lane, request.snapshot.hostKey)
                              < laneCfg.reservedPerHost();
            });
        if (index >= 0) {
            hostReservationCursor = (laneOrder.indexOf(lane) + 1) % laneCount;
            return index;
        }
    }

    return -1;
}

int SchedulerQueues::selectReservationGlobalIndex(const QCNetworkRequestScheduler::Config &config)
{
    QStringList activeLanes;
    for (const auto &lane : laneOrder) {
        const auto laneCfg = laneConfigFor(lane);
        if (laneCfg.reservedGlobal() <= 0) {
            continue;
        }

        if (runningLaneCount.value(lane, 0) >= laneCfg.reservedGlobal()) {
            continue;
        }

        const bool active = findQueuedRequestIndex(
            pendingRequests,
            [&](const QueuedRequest &request) {
                return request.reply && request.snapshot.lane == lane
                       && hostConnectionCount.value(request.snapshot.hostKey, 0)
                              < config.maxRequestsPerHost();
            })
            >= 0;
        if (active) {
            activeLanes.append(lane);
        }
    }

    if (activeLanes.isEmpty()) {
        return -1;
    }

    const int laneCount = laneOrder.size();
    for (int attempt = 0; attempt < laneCount; ++attempt) {
        const QString &lane = laneOrder.at((globalReservationCursor + attempt) % laneCount);
        if (!activeLanes.contains(lane)) {
            continue;
        }

        const int index = candidateIndexForLane(
            lane,
            [&](const QueuedRequest &request) {
                return hostConnectionCount.value(request.snapshot.hostKey, 0)
                       < config.maxRequestsPerHost();
            });
        if (index >= 0) {
            globalReservationCursor = (laneOrder.indexOf(lane) + 1) % laneCount;
            return index;
        }
    }

    return -1;
}

int SchedulerQueues::selectBestEffortIndex(const QCNetworkRequestScheduler::Config &config)
{
    if (laneOrder.isEmpty()) {
        return -1;
    }

    const int laneCount = laneOrder.size();
    for (int attempt = 0; attempt < laneCount; ++attempt) {
        const int laneIndex = (bestEffortCursor + attempt) % laneCount;
        const QString &lane = laneOrder.at(laneIndex);

        const bool laneHasPending = findQueuedRequestIndex(
            pendingRequests,
            [&](const QueuedRequest &request) {
                return request.reply && request.snapshot.lane == lane;
            })
            >= 0;
        if (!laneHasPending) {
            continue;
        }

        const auto laneCfg = laneConfigFor(lane);
        if (laneDeficit.value(lane, 0) < 1) {
            laneDeficit[lane] += std::max(1, laneCfg.weight() * std::max(1, laneCfg.quantum()));
        }

        if (laneDeficit.value(lane, 0) < 1) {
            continue;
        }

        const int index = candidateIndexForLane(
            lane,
            [&](const QueuedRequest &request) {
                return hostConnectionCount.value(request.snapshot.hostKey, 0)
                           < config.maxRequestsPerHost()
                       && !wouldViolateReservation(request, config);
            });
        if (index >= 0) {
            laneDeficit[lane] -= 1;
            bestEffortCursor
                = (laneDeficit.value(lane, 0) < 1) ? (laneIndex + 1) % laneCount : laneIndex;
            return index;
        }

        laneDeficit[lane] = 0;
    }

    return -1;
}

} // namespace Internal

} // namespace QCurl
