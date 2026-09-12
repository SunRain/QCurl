// SPDX-License-Identifier: MIT
// Copyright (c) 2025 QCurl Project

#include "QCNetworkAdmissionCore_p.h"

namespace QCurl::Internal {

SchedulerCommandResult AdmissionCore::enqueue(SchedulerRequestId id, const ReplySnapshot &snapshot)
{
    if (!id || !m_policy.isLaneRegistered(snapshot.lane) || !isValidPriority(snapshot.priority)) {
        return SchedulerCommandResult::InvalidArgument;
    }
    if (requestIndex(id) >= 0) {
        return SchedulerCommandResult::NoChange;
    }
    m_requests.append({id, snapshot, ScheduledState::Pending});
    m_stats.setPendingRequests(m_stats.pendingRequests() + 1);
    return SchedulerCommandResult::Applied;
}

AdmissionChange AdmissionCore::defer(SchedulerRequestId id)
{
    const int index = requestIndex(id);
    if (index < 0) {
        return {};
    }
    auto &request = m_requests[index];
    if (request.state != ScheduledState::Pending) {
        return {SchedulerCommandResult::InvalidState, request.snapshot};
    }
    request.state = ScheduledState::Deferred;
    m_stats.setPendingRequests(m_stats.pendingRequests() - 1);
    return {SchedulerCommandResult::Applied, request.snapshot, m_stats.pendingRequests() == 0};
}

AdmissionChange AdmissionCore::undefer(SchedulerRequestId id)
{
    const int index = requestIndex(id);
    if (index < 0) {
        return {};
    }
    if (m_requests.at(index).state != ScheduledState::Deferred) {
        return {SchedulerCommandResult::InvalidState, m_requests.at(index).snapshot};
    }
    auto request  = m_requests.takeAt(index);
    request.state = ScheduledState::Pending;
    m_requests.append(request);
    m_stats.setPendingRequests(m_stats.pendingRequests() + 1);
    return {SchedulerCommandResult::Applied, request.snapshot};
}

AdmissionChange AdmissionCore::changePriority(SchedulerRequestId id,
                                              QCNetworkRequestPriority priority)
{
    if (!isValidPriority(priority)) {
        return {SchedulerCommandResult::InvalidArgument, {}};
    }
    const int index = requestIndex(id);
    if (index < 0) {
        return {};
    }
    auto &request = m_requests[index];
    if (request.state != ScheduledState::Pending) {
        return {SchedulerCommandResult::InvalidState, request.snapshot};
    }
    if (request.snapshot.priority == priority) {
        return {SchedulerCommandResult::NoChange, request.snapshot};
    }
    request.snapshot.priority = priority;
    return {SchedulerCommandResult::Applied, request.snapshot};
}

void AdmissionCore::markRunning(const ScheduledRequest &request)
{
    const QString lane = request.snapshot.lane.name();
    m_stats.setPendingRequests(m_stats.pendingRequests() - 1);
    m_stats.setRunningRequests(m_stats.runningRequests() + 1);
    ++m_runningLaneCount[lane];
    ++m_hostConnectionCount[request.snapshot.origin];
    ++m_runningLaneHostCount[lane][request.snapshot.origin];
    m_laneLastStartedHost.insert(lane, request.snapshot.origin);
}

void AdmissionCore::releaseRunning(const ReplySnapshot &snapshot)
{
    const QString lane = snapshot.lane.name();
    m_stats.setRunningRequests(m_stats.runningRequests() - 1);
    if (--m_runningLaneCount[lane] == 0) {
        m_runningLaneCount.remove(lane);
    }
    if (--m_hostConnectionCount[snapshot.origin] == 0) {
        m_hostConnectionCount.remove(snapshot.origin);
    }
    auto &hosts = m_runningLaneHostCount[lane];
    if (--hosts[snapshot.origin] == 0) {
        hosts.remove(snapshot.origin);
    }
    if (hosts.isEmpty()) {
        m_runningLaneHostCount.remove(lane);
    }
}

void AdmissionCore::recordCompletion(const ReplyOutcome &outcome)
{
    const int completed  = m_stats.completedRequests() + 1;
    const double average = m_stats.avgResponseTime();
    m_stats.setCompletedRequests(completed);
    m_stats.setAvgResponseTime(average + (outcome.durationMs - average) / completed);
    m_stats.setTotalBytesReceived(m_stats.totalBytesReceived() + outcome.bytesReceived);
    m_stats.setTotalBytesSent(m_stats.totalBytesSent() + outcome.bytesSent);
}

FinalizeResult AdmissionCore::finalize(SchedulerRequestId id,
                                       FinalizeTrigger trigger,
                                       const ReplyOutcome &outcome)
{
    const int index = requestIndex(id);
    if (index < 0) {
        return {};
    }
    const auto request = m_requests.takeAt(index);
    FinalizeResult result;
    result.wasTracked = true;
    result.snapshot   = request.snapshot;
    if (request.state == ScheduledState::Running) {
        releaseRunning(request.snapshot);
    } else if (request.state == ScheduledState::Pending) {
        m_stats.setPendingRequests(m_stats.pendingRequests() - 1);
        result.pendingQueueEmptied = m_stats.pendingRequests() == 0;
    }
    if (trigger == FinalizeTrigger::ExplicitCancel
        || (trigger == FinalizeTrigger::FinishedSignal && outcome.cancelled)) {
        m_stats.setCancelledRequests(m_stats.cancelledRequests() + 1);
        result.emitCancelled = true;
    } else if (trigger == FinalizeTrigger::FinishedSignal
               && request.state == ScheduledState::Running) {
        recordCompletion(outcome);
        result.emitFinished = true;
    }
    return result;
}

std::optional<AdmissionStart> AdmissionCore::takeNextPending()
{
    if (!hasRunnablePending()) {
        return std::nullopt;
    }
    int index = selectReservationHostIndex();
    if (index < 0) {
        index = selectReservationGlobalIndex();
    }
    if (index < 0) {
        index = selectBestEffortIndex();
    }
    if (index < 0) {
        return std::nullopt;
    }
    auto &request = m_requests[index];
    request.state = ScheduledState::Running;
    markRunning(request);
    return AdmissionStart{request, m_stats.pendingRequests() == 0};
}

} // namespace QCurl::Internal
