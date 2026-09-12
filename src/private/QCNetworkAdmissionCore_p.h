// SPDX-License-Identifier: MIT
// Copyright (c) 2025 QCurl Project

#ifndef QCNETWORKADMISSIONCORE_P_H
#define QCNETWORKADMISSIONCORE_P_H

#include "QCNetworkRequestPriority.h"
#include "QCNetworkSchedulerPolicy.h"

#include <QHash>
#include <QList>
#include <QStringList>

#include <array>
#include <optional>

namespace QCurl::Internal {

using SchedulerRequestId = quint64;

enum class ScheduledState {
    Pending,
    Deferred,
    Running,
};

/// 已提交元数据的 owning 值快照；不依赖 reply 或队列记录的生命周期。
struct ReplySnapshot
{
    QCNetworkLaneKey lane;
    QString origin;
    QCNetworkRequestPriority priority = QCNetworkRequestPriority::Normal;
};

/// 唯一调度记录；Running 只表示占槽，不承接传输状态。
struct ScheduledRequest
{
    SchedulerRequestId id = 0;
    ReplySnapshot snapshot;
    ScheduledState state = ScheduledState::Pending;
};

/// 一次选择并占槽后的结果；索引不跨层，Qt 层仅使用稳定标识和复制的快照。
struct AdmissionStart
{
    ScheduledRequest request;
    bool pendingQueueEmptied = false;
};

/// 无回调状态提交的结果；通知使用此快照，不在重入后回查旧记录。
struct AdmissionChange
{
    SchedulerCommandResult result = SchedulerCommandResult::NotTracked;
    ReplySnapshot snapshot;
    bool pendingQueueEmptied = false;
};

enum class FinalizeTrigger {
    FinishedSignal,
    ExplicitCancel,
    Destroyed,
};

/// Qt 层在对象有效时捕获的终态观测；durationMs 来自单调时钟。
struct ReplyOutcome
{
    bool cancelled       = false;
    qint64 bytesReceived = 0;
    qint64 bytesSent     = 0;
    qint64 durationMs    = 0;
};

/// 终态记录已移除后返回的通知依据；重复终结不会再次生成通知。
struct FinalizeResult
{
    ReplySnapshot snapshot;
    bool wasTracked          = false;
    bool emitCancelled       = false;
    bool emitFinished        = false;
    bool pendingQueueEmptied = false;
};

const std::array<QCNetworkRequestPriority, 6> &priorityOrder();
bool isValidPriority(QCNetworkRequestPriority priority);

// 只处理值和跨层标识；全部调度状态及计数在无回调的转换内提交。
class Q_DECL_HIDDEN AdmissionCore
{
public:
    [[nodiscard]] bool applyPolicy(const QCNetworkSchedulerPolicy &policy, QString *error = nullptr);
    const QCNetworkSchedulerPolicy &policy() const;
    QCNetworkSchedulerStatistics statistics() const;
    std::optional<ScheduledRequest> request(SchedulerRequestId id) const;
    QList<SchedulerRequestId> requestIds(const QCNetworkLaneKey &lane, bool includeRunning) const;
    bool hasRunnablePending() const;

    [[nodiscard]] SchedulerCommandResult enqueue(SchedulerRequestId id,
                                                 const ReplySnapshot &snapshot);
    [[nodiscard]] AdmissionChange defer(SchedulerRequestId id);
    [[nodiscard]] AdmissionChange undefer(SchedulerRequestId id);
    [[nodiscard]] AdmissionChange changePriority(SchedulerRequestId id,
                                                 QCNetworkRequestPriority priority);
    // 选择、占槽和记账一次完成；返回空值表示当前没有可启动项。
    [[nodiscard]] std::optional<AdmissionStart> takeNextPending();
    // 终结立即移除记录；不存在的标识没有统计或通知副作用，不保存终态历史表。
    [[nodiscard]] FinalizeResult finalize(SchedulerRequestId id,
                                          FinalizeTrigger trigger,
                                          const ReplyOutcome &outcome = {});

private:
    int requestIndex(SchedulerRequestId id) const;
    QCNetworkSchedulerPolicy::LaneConfig laneConfig(const QCNetworkLaneKey &lane) const;
    bool changesWeightedRound(const QCNetworkSchedulerPolicy &policy) const;
    QStringList rotatedLaneHosts(const QCNetworkLaneKey &lane) const;
    int candidateIndexForLane(const QCNetworkLaneKey &lane, int reservedPerHost = 0) const;
    int selectReservationHostIndex();
    int selectReservationGlobalIndex();
    int selectBestEffortIndex();
    void markRunning(const ScheduledRequest &request);
    void releaseRunning(const ReplySnapshot &snapshot);
    void recordCompletion(const ReplyOutcome &outcome);

    QCNetworkSchedulerPolicy m_policy = QCNetworkSchedulerPolicy::defaultPolicy();
    QList<ScheduledRequest> m_requests;
    QCNetworkSchedulerStatistics m_stats;
    QHash<QString, int> m_hostConnectionCount;
    QHash<QString, int> m_runningLaneCount;
    QHash<QString, QHash<QString, int>> m_runningLaneHostCount;
    // 每次补充最多 INT_MAX 个启动份额；不累计多轮额度，也不做 weight * quantum。
    QHash<QString, qint64> m_laneCredit;
    QHash<QString, QString> m_laneLastStartedHost;
    int m_hostReservationCursor   = 0;
    int m_globalReservationCursor = 0;
    int m_bestEffortCursor        = 0;
};

} // namespace QCurl::Internal

#endif // QCNETWORKADMISSIONCORE_P_H
