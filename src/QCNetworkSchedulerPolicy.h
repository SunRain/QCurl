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
#include <QMetaType>
#include <QSharedDataPointer>
#include <QString>

namespace QCurl {

class QCNetworkSchedulerPolicyData;
class QCNetworkSchedulerPolicyLaneConfigData;
class QCNetworkSchedulerStatisticsData;

/**
 * @brief 单请求调度命令的同步提交结果。
 *
 * Applied 只确认本次变更已提交，不表示传输完成，也不保证重入后状态仍相同。
 * 其余结果不改变请求、队列或统计，不发变化通知。先检查调用线程，再检查参数、
 * 当前 manager 的调度绑定、有效 reply 的线程亲和性及调度状态。
 */
enum class SchedulerCommandResult {
    Applied,
    NoChange,    ///< 合法但没有实际变化。
    WrongThread, ///< 不在 manager owner thread。
    NullReply,
    InvalidArgument,        ///< 优先级等参数不在有效域内。
    NotTracked,             ///< 未跟踪、其他 manager 的请求或已解除跟踪。
    ThreadAffinityMismatch, ///< 已跟踪 reply 的亲和性与 manager 不同。
    InvalidState,           ///< 当前调度状态不允许此命令。
};

/**
 * @brief manager 调度统计的独立值快照。
 *
 * Running 表示已占 admission 槽位；Completed 包含已观察到的非取消错误终态，
 * 不等于 HTTP 成功。耗时按占槽至终态的单调时间计算，单位毫秒。
 * 重复取消/终态不重复记账；没有终态观测就直接销毁仅回收槽位。
 */
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
 * 该值类型集中描述 lane 注册、reservation、启动次数权重和调度层并发限制。未注册 lane
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

        /// 每轮可启动请求数的权重，接受域为 1 至 INT_MAX，不表示带宽份额。
        [[nodiscard]] int weight() const;
        /// 设置待校验的权重；setLaneConfig() 拒绝零和负数，不截断输入。
        void setWeight(int value);

        [[nodiscard]] int reservedGlobal() const;
        void setReservedGlobal(int value);

        [[nodiscard]] int reservedPerHost() const;
        void setReservedPerHost(int value);

        /// 按全部配额值比较。
        bool operator==(const LaneConfig &other) const;

    private:
        QSharedDataPointer<QCNetworkSchedulerPolicyLaneConfigData> d;
    };

    QCNetworkSchedulerPolicy();
    QCNetworkSchedulerPolicy(const QCNetworkSchedulerPolicy &other);
    QCNetworkSchedulerPolicy(QCNetworkSchedulerPolicy &&other) noexcept;
    ~QCNetworkSchedulerPolicy();

    QCNetworkSchedulerPolicy &operator=(const QCNetworkSchedulerPolicy &other);
    QCNetworkSchedulerPolicy &operator=(QCNetworkSchedulerPolicy &&other) noexcept;

    /// 按配额和 lane 注册顺序比较；相同策略不会重置调度轮次。
    bool operator==(const QCNetworkSchedulerPolicy &other) const;

    [[nodiscard]] bool isLaneRegistered(const QCNetworkLaneKey &lane) const;
    [[nodiscard]] QList<QCNetworkLaneKey> registeredLanes() const;
    /// 注册或替换 lane 配置；invalid lane/config 会失败且不修改 policy。
    [[nodiscard]] bool setLaneConfig(const QCNetworkLaneKey &lane,
                                     const LaneConfig &config,
                                     QString *error = nullptr);
    [[nodiscard]] bool laneConfig(const QCNetworkLaneKey &lane,
                                  LaneConfig *out,
                                  QString *error = nullptr) const;

    [[nodiscard]] int maxConcurrentRequests() const;
    void setMaxConcurrentRequests(int value);

    [[nodiscard]] int maxRequestsPerHost() const;
    void setMaxRequestsPerHost(int value);

    /// 每秒观测上传与下载字节的 admission 阈值；0 禁用，不限制已运行传输的聚合速度。
    [[nodiscard]] qint64 admissionByteBudget() const;
    /// 设置非负阈值；负数使 validate() 失败。单请求速率仍由请求的 libcurl 限速配置控制。
    void setAdmissionByteBudget(qint64 value);

    /// 校验配额和默认 lane 注册；失败写入 error，成功清空 error，不改变策略。
    [[nodiscard]] bool validate(QString *error = nullptr) const;

    [[nodiscard]] static QCNetworkSchedulerPolicy defaultPolicy();

private:
    QSharedDataPointer<QCNetworkSchedulerPolicyData> d;
};

} // namespace QCurl

Q_DECLARE_METATYPE(QCurl::QCNetworkSchedulerPolicy)
Q_DECLARE_METATYPE(QCurl::QCNetworkSchedulerPolicy::LaneConfig)
Q_DECLARE_METATYPE(QCurl::QCNetworkSchedulerStatistics)
Q_DECLARE_METATYPE(QCurl::SchedulerCommandResult)

#endif // QCNETWORKSCHEDULERPOLICY_H
