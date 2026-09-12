// SPDX-License-Identifier: MIT
// Copyright (c) 2025 QCurl Project

#include "QCNetworkAdmissionCore_p.h"

namespace QCurl::Internal {

const std::array<QCNetworkRequestPriority, 6> &priorityOrder()
{
    static constexpr std::array<QCNetworkRequestPriority, 6> kPriorities = {
        QCNetworkRequestPriority::Critical,
        QCNetworkRequestPriority::VeryHigh,
        QCNetworkRequestPriority::High,
        QCNetworkRequestPriority::Normal,
        QCNetworkRequestPriority::Low,
        QCNetworkRequestPriority::VeryLow,
    };
    return kPriorities;
}

bool isValidPriority(QCNetworkRequestPriority priority)
{
    return priority >= QCNetworkRequestPriority::VeryLow
           && priority <= QCNetworkRequestPriority::Critical;
}

bool AdmissionCore::changesWeightedRound(const QCNetworkSchedulerPolicy &policy) const
{
    if (policy.registeredLanes() != m_policy.registeredLanes()) {
        return true;
    }
    for (const auto &lane : policy.registeredLanes()) {
        QCNetworkSchedulerPolicy::LaneConfig candidate;
        const bool found = policy.laneConfig(lane, &candidate);
        Q_ASSERT(found);
        if (candidate.weight() != laneConfig(lane).weight()) {
            return true;
        }
    }
    return false;
}

bool AdmissionCore::applyPolicy(const QCNetworkSchedulerPolicy &policy, QString *error)
{
    if (!policy.validate(error)) {
        return false;
    }
    for (const auto &request : std::as_const(m_requests)) {
        if (!policy.isLaneRegistered(request.snapshot.lane)) {
            if (error) {
                *error = QStringLiteral("cannot remove busy scheduler lane: %1")
                             .arg(request.snapshot.lane.name());
            }
            return false;
        }
    }
    if (m_policy == policy) {
        return true;
    }
    const bool resetRound = changesWeightedRound(policy);
    m_policy              = policy;
    if (resetRound) {
        m_laneCredit.clear();
        m_laneLastStartedHost.clear();
        m_hostReservationCursor   = 0;
        m_globalReservationCursor = 0;
        m_bestEffortCursor        = 0;
    }
    return true;
}

const QCNetworkSchedulerPolicy &AdmissionCore::policy() const
{
    return m_policy;
}

QCNetworkSchedulerStatistics AdmissionCore::statistics() const
{
    return m_stats;
}

int AdmissionCore::requestIndex(SchedulerRequestId id) const
{
    for (int index = 0; index < m_requests.size(); ++index) {
        if (m_requests.at(index).id == id) {
            return index;
        }
    }
    return -1;
}

std::optional<ScheduledRequest> AdmissionCore::request(SchedulerRequestId id) const
{
    const int index = requestIndex(id);
    return index < 0 ? std::nullopt : std::optional<ScheduledRequest>(m_requests.at(index));
}

QList<SchedulerRequestId> AdmissionCore::requestIds(const QCNetworkLaneKey &lane,
                                                    bool includeRunning) const
{
    QList<SchedulerRequestId> result;
    for (const auto &request : m_requests) {
        if (request.snapshot.lane == lane
            && (includeRunning || request.state != ScheduledState::Running)) {
            result.append(request.id);
        }
    }
    return result;
}

QCNetworkSchedulerPolicy::LaneConfig AdmissionCore::laneConfig(const QCNetworkLaneKey &lane) const
{
    QCNetworkSchedulerPolicy::LaneConfig result;
    const bool found = m_policy.laneConfig(lane, &result);
    Q_ASSERT(found);
    return result;
}

bool AdmissionCore::hasRunnablePending() const
{
    if (m_stats.runningRequests() >= m_policy.maxConcurrentRequests()) {
        return false;
    }
    for (const auto &request : m_requests) {
        if (request.state == ScheduledState::Pending
            && m_hostConnectionCount.value(request.snapshot.origin)
                   < m_policy.maxRequestsPerHost()) {
            return true;
        }
    }
    return false;
}

} // namespace QCurl::Internal
