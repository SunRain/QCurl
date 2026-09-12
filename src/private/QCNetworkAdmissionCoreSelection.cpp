// SPDX-License-Identifier: MIT
// Copyright (c) 2025 QCurl Project

#include "QCNetworkAdmissionCore_p.h"

#include <algorithm>

namespace QCurl::Internal {

QStringList AdmissionCore::rotatedLaneHosts(const QCNetworkLaneKey &lane) const
{
    QStringList hosts;
    for (const auto &request : m_requests) {
        if (request.state == ScheduledState::Pending && request.snapshot.lane == lane
            && !hosts.contains(request.snapshot.origin)) {
            hosts.append(request.snapshot.origin);
        }
    }
    const int lastIndex = hosts.indexOf(m_laneLastStartedHost.value(lane.name()));
    if (lastIndex < 0 || hosts.size() <= 1) {
        return hosts;
    }
    QStringList rotated;
    for (int offset = 1; offset <= hosts.size(); ++offset) {
        rotated.append(hosts.at((lastIndex + offset) % hosts.size()));
    }
    return rotated;
}

int AdmissionCore::candidateIndexForLane(const QCNetworkLaneKey &lane, int reservedPerHost) const
{
    const auto hosts = rotatedLaneHosts(lane);
    for (const auto priority : priorityOrder()) {
        for (const auto &host : hosts) {
            if (m_hostConnectionCount.value(host) >= m_policy.maxRequestsPerHost()
                || (reservedPerHost > 0
                    && m_runningLaneHostCount.value(lane.name()).value(host) >= reservedPerHost)) {
                continue;
            }
            for (int index = 0; index < m_requests.size(); ++index) {
                const auto &request = m_requests.at(index);
                if (request.state == ScheduledState::Pending && request.snapshot.lane == lane
                    && request.snapshot.priority == priority && request.snapshot.origin == host) {
                    return index;
                }
            }
        }
    }
    return -1;
}

int AdmissionCore::selectReservationHostIndex()
{
    const auto lanes = m_policy.registeredLanes();
    for (int attempt = 0; attempt < lanes.size(); ++attempt) {
        const int laneIndex = (m_hostReservationCursor + attempt) % lanes.size();
        const auto &lane    = lanes.at(laneIndex);
        const int reserved  = laneConfig(lane).reservedPerHost();
        if (reserved <= 0) {
            continue;
        }
        const int index = candidateIndexForLane(lane, reserved);
        if (index >= 0) {
            m_hostReservationCursor = (laneIndex + 1) % lanes.size();
            return index;
        }
    }
    return -1;
}

int AdmissionCore::selectReservationGlobalIndex()
{
    const auto lanes = m_policy.registeredLanes();
    for (int attempt = 0; attempt < lanes.size(); ++attempt) {
        const int laneIndex = (m_globalReservationCursor + attempt) % lanes.size();
        const auto &lane    = lanes.at(laneIndex);
        if (m_runningLaneCount.value(lane.name()) >= laneConfig(lane).reservedGlobal()) {
            continue;
        }
        const int index = candidateIndexForLane(lane);
        if (index >= 0) {
            m_globalReservationCursor = (laneIndex + 1) % lanes.size();
            return index;
        }
    }
    return -1;
}

int AdmissionCore::selectBestEffortIndex()
{
    const auto lanes = m_policy.registeredLanes();
    for (int attempt = 0; attempt < lanes.size(); ++attempt) {
        const int laneIndex   = (m_bestEffortCursor + attempt) % lanes.size();
        const auto &lane      = lanes.at(laneIndex);
        const QString name    = lane.name();
        const bool hasPending = std::any_of(m_requests.cbegin(),
                                            m_requests.cend(),
                                            [&](const ScheduledRequest &request) {
                                                return request.state == ScheduledState::Pending
                                                       && request.snapshot.lane == lane;
                                            });
        if (!hasPending) {
            continue;
        }
        if (m_laneCredit.value(name) < 1) {
            m_laneCredit[name] = laneConfig(lane).weight();
        }
        // 前两阶段均无候选时，best-effort 占槽不会创造 reservation 缺口；只检查硬上限。
        const int index = candidateIndexForLane(lane);
        if (index >= 0) {
            --m_laneCredit[name];
            m_bestEffortCursor = m_laneCredit.value(name) == 0 ? (laneIndex + 1) % lanes.size()
                                                               : laneIndex;
            return index;
        }
        m_laneCredit[name] = 0;
    }
    return -1;
}

} // namespace QCurl::Internal
