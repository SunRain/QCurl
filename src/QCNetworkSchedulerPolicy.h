// SPDX-License-Identifier: MIT
// Copyright (c) 2025 QCurl Project

/**
 * @file
 * @brief 声明 manager 级请求调度策略。
 */

#ifndef QCNETWORKSCHEDULERPOLICY_H
#define QCNETWORKSCHEDULERPOLICY_H

#include "QCGlobal.h"
#include "QCNetworkLaneKey.h"

#include <QList>
#include <QSharedDataPointer>

namespace QCurl {

class QCNetworkSchedulerPolicyData;
class QCNetworkSchedulerPolicyLaneConfigData;
class QCNetworkSchedulerStatisticsData;

/// 请求调度器统计信息，供 manager-level API 按值返回。
class QCURL_EXPORT QCNetworkSchedulerStatistics
{
public:
    QCNetworkSchedulerStatistics();
    QCNetworkSchedulerStatistics(const QCNetworkSchedulerStatistics &other);
    QCNetworkSchedulerStatistics(QCNetworkSchedulerStatistics &&other) noexcept;
    ~QCNetworkSchedulerStatistics();

    QCNetworkSchedulerStatistics &operator=(const QCNetworkSchedulerStatistics &other);
    QCNetworkSchedulerStatistics &operator=(QCNetworkSchedulerStatistics &&other) noexcept;

    [[nodiscard]] int pendingRequests() const;
    void setPendingRequests(int value);

    [[nodiscard]] int runningRequests() const;
    void setRunningRequests(int value);

    [[nodiscard]] int completedRequests() const;
    void setCompletedRequests(int value);

    [[nodiscard]] int cancelledRequests() const;
    void setCancelledRequests(int value);

    [[nodiscard]] qint64 totalBytesReceived() const;
    void setTotalBytesReceived(qint64 value);

    [[nodiscard]] qint64 totalBytesSent() const;
    void setTotalBytesSent(qint64 value);

    [[nodiscard]] double avgResponseTime() const;
    void setAvgResponseTime(double value);

private:
    QSharedDataPointer<QCNetworkSchedulerStatisticsData> d;
};

/**
 * @brief manager 级 scheduler admission policy。
 *
 * 该值类型集中描述 lane 注册、reservation、DRR 权重和调度层并发限制。未注册 lane
 * 固定按 RequireRegistered fail-closed，不会静默映射到 default lane。
 */
class QCURL_EXPORT QCNetworkSchedulerPolicy
{
public:
    class QCURL_EXPORT LaneConfig
    {
    public:
        LaneConfig();
        LaneConfig(const LaneConfig &other);
        LaneConfig(LaneConfig &&other) noexcept;
        ~LaneConfig();

        LaneConfig &operator=(const LaneConfig &other);
        LaneConfig &operator=(LaneConfig &&other) noexcept;

        [[nodiscard]] int weight() const;
        void setWeight(int value);

        [[nodiscard]] int quantum() const;
        void setQuantum(int value);

        [[nodiscard]] int reservedGlobal() const;
        void setReservedGlobal(int value);

        [[nodiscard]] int reservedPerHost() const;
        void setReservedPerHost(int value);

    private:
        QSharedDataPointer<QCNetworkSchedulerPolicyLaneConfigData> d;
    };

    enum class UnknownLaneMode {
        RequireRegistered,
    };

    QCNetworkSchedulerPolicy();
    QCNetworkSchedulerPolicy(const QCNetworkSchedulerPolicy &other);
    QCNetworkSchedulerPolicy(QCNetworkSchedulerPolicy &&other) noexcept;
    ~QCNetworkSchedulerPolicy();

    QCNetworkSchedulerPolicy &operator=(const QCNetworkSchedulerPolicy &other);
    QCNetworkSchedulerPolicy &operator=(QCNetworkSchedulerPolicy &&other) noexcept;

    [[nodiscard]] QCNetworkLaneKey defaultLane() const;
    void setDefaultLane(const QCNetworkLaneKey &lane);

    [[nodiscard]] UnknownLaneMode unknownLaneMode() const noexcept;

    [[nodiscard]] bool isLaneRegistered(const QCNetworkLaneKey &lane) const;
    [[nodiscard]] QList<QCNetworkLaneKey> registeredLanes() const;
    /// 注册或替换 lane 配置；invalid lane/config 会失败且不修改 policy。
    [[nodiscard]] bool setLaneConfig(const QCNetworkLaneKey &lane,
                                     const LaneConfig &config,
                                     QString *error = nullptr);
    [[nodiscard]] LaneConfig laneConfig(const QCNetworkLaneKey &lane) const;

    [[nodiscard]] int maxConcurrentRequests() const;
    void setMaxConcurrentRequests(int value);

    [[nodiscard]] int maxRequestsPerHost() const;
    void setMaxRequestsPerHost(int value);

    [[nodiscard]] qint64 maxBandwidthBytesPerSec() const;
    void setMaxBandwidthBytesPerSec(qint64 value);

    [[nodiscard]] bool throttlingEnabled() const;
    void setThrottlingEnabled(bool enabled);

    [[nodiscard]] bool validate(QString *error = nullptr) const;

    [[nodiscard]] static QCNetworkSchedulerPolicy defaultPolicy();

private:
    QSharedDataPointer<QCNetworkSchedulerPolicyData> d;
};

} // namespace QCurl

Q_DECLARE_METATYPE(QCurl::QCNetworkSchedulerPolicy)
Q_DECLARE_METATYPE(QCurl::QCNetworkSchedulerPolicy::LaneConfig)
Q_DECLARE_METATYPE(QCurl::QCNetworkSchedulerStatistics)

#endif // QCNETWORKSCHEDULERPOLICY_H
