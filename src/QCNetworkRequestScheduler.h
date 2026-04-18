// SPDX-License-Identifier: MIT
// Copyright (c) 2025 QCurl Project

/**
 * @file
 * @brief 声明请求调度器。
 */

#ifndef QCNETWORKREQUESTSCHEDULER_H
#define QCNETWORKREQUESTSCHEDULER_H

#include "QCGlobal.h"
#include "QCNetworkRequestPriority.h"

#include <QList>
#include <QObject>
#include <QScopedPointer>
#include <QSharedDataPointer>
#include <QString>

namespace QCurl {

class QCNetworkAccessManager;
class QCNetworkReply;
class QCNetworkRequestSchedulerConfigData;
class QCNetworkRequestSchedulerStatisticsData;
class QCNetworkRequestSchedulerLaneConfigData;

/**
 * @brief 网络请求调度器
 *
 * 管理请求的优先级队列、并发控制和带宽限制。
 *
 * 优先级契约（非抢占 / non-preemptive）：
 *
 * - 调度器是**非抢占式**：一旦请求进入 Running，后续更高优先级请求不会中断/暂停/取消它。
 * - 优先级只影响 pending 队列在“并发槽位释放时”的出队顺序；同一优先级内部为 FIFO。
 * - `Critical` 优先级仍受全局/每主机/限流硬上限约束；控制面保底由 lane reservation 提供。
 * - 若调用方需要“让出槽位/终止执行”，应显式调用 `cancelRequest()`（Running 会进入取消流程）。
 * - `deferPendingRequest()` 仅作用于 Pending：把请求从队列移入 deferred 列表；不表达传输级 pause/resume。
 * - 调度器不再创建 `QCNetworkReply`；异步请求统一由 `QCNetworkAccessManager::send*()` 创建 reply 后交给调度器排队。
 *
 * 线程 contract（NetworkEngine 合一线程）：
 *
 * - `instance()` 返回当前线程共享的 thread-local scheduler，而不是进程级全局单例。
 * - scheduler / `QCNetworkReply` / `QCCurlMultiManager` 必须处于同一 owner thread。
 * - 异步请求要求 owner thread 具备 Qt 事件循环；当 owner thread 事件循环停止或线程退出时，
 *   先前排队的 queued invoke / 信号投递不再保证可达。
 * - 从非 owner thread 调用 scheduler API 时，库会统一 marshal 回 scheduler owner thread：
 *   返回值型接口使用 BlockingQueuedConnection，fire-and-forget 接口使用 QueuedConnection。
 * - 本类信号始终在 scheduler owner thread 发射，不会漂移到调用线程。
 *
 * @note 所有公共方法都使用互斥锁保护
 */
class QCURL_EXPORT QCNetworkRequestScheduler : public QObject
{
    Q_OBJECT

public:
    /**
     * @brief 调度器配置（ABI 友好的值类型）
     */
    class QCURL_EXPORT Config
    {
    public:
        Config();
        Config(const Config &other);
        Config(Config &&other);
        ~Config();
        Config &operator=(const Config &other);
        Config &operator=(Config &&other);

        /// 全局最大并发请求数（默认 6）
        [[nodiscard]] int maxConcurrentRequests() const;
        void setMaxConcurrentRequests(int value);

        /// 每个主机最大并发请求数（默认 2）
        [[nodiscard]] int maxRequestsPerHost() const;
        void setMaxRequestsPerHost(int value);

        /// 最大带宽限制（字节/秒，0 表示无限制）
        [[nodiscard]] qint64 maxBandwidthBytesPerSec() const;
        void setMaxBandwidthBytesPerSec(qint64 value);

        /// 是否启用流量控制（默认 true）
        [[nodiscard]] bool enableThrottling() const;
        void setEnableThrottling(bool enabled);

    private:
        QSharedDataPointer<QCNetworkRequestSchedulerConfigData> d;
    };

    /**
     * @brief 调度器统计信息（ABI 友好的值类型）
     */
    class QCURL_EXPORT Statistics
    {
    public:
        Statistics();
        Statistics(const Statistics &other);
        Statistics(Statistics &&other);
        ~Statistics();
        Statistics &operator=(const Statistics &other);
        Statistics &operator=(Statistics &&other);

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
        QSharedDataPointer<QCNetworkRequestSchedulerStatisticsData> d;
    };

    /**
     * @brief lane 级调度配置（ABI 友好的值类型）
     */
    class QCURL_EXPORT LaneConfig
    {
    public:
        LaneConfig();
        LaneConfig(const LaneConfig &other);
        LaneConfig(LaneConfig &&other);
        ~LaneConfig();
        LaneConfig &operator=(const LaneConfig &other);
        LaneConfig &operator=(LaneConfig &&other);

        /// DRR 权重（仅 best-effort 阶段生效，>0）
        [[nodiscard]] int weight() const;
        void setWeight(int value);

        /// 每轮补充给 deficit 的基数（>0）
        [[nodiscard]] int quantum() const;
        void setQuantum(int value);

        /// 在 best-effort 前先为该 lane 预留的全局槽位
        [[nodiscard]] int reservedGlobal() const;
        void setReservedGlobal(int value);

        /// 在 best-effort 前先为该 lane+host 预留的槽位
        [[nodiscard]] int reservedPerHost() const;
        void setReservedPerHost(int value);

    private:
        QSharedDataPointer<QCNetworkRequestSchedulerLaneConfigData> d;
    };

    /**
     * @brief lane 取消范围
     */
    enum class CancelLaneScope {
        PendingOnly,       ///< 只清 pending + deferred，不打断已 Running 的请求
        PendingAndRunning, ///< pending + deferred + running 一并取消，用于整条 lane 排空
    };

    /**
     * @brief 获取当前线程绑定的调度器实例
     *
     * 实现为 thread-local；不同线程拿到的是不同实例。
     *
     * @note 该实例是“当前线程共享实例”，不是 `QCNetworkAccessManager` 私有对象。
     */
    static QCNetworkRequestScheduler *instance();

    /**
     * @brief 设置调度器配置
     *
     * @param config 新的配置
     */
    void setConfig(const Config &config);

    /**
     * @brief 获取当前配置
     *
     * @return 当前配置的副本
     */
    Config config() const;

    /**
     * @brief 设置 lane 调度配置
     *
     * lane 名称会先做 trim；空字符串表示 default lane。
     */
    void setLaneConfig(const QString &lane, const LaneConfig &config);

    /**
     * @brief 获取 lane 调度配置
     */
    LaneConfig laneConfig(const QString &lane) const;

    /**
     * @brief 延后调度请求（仅 Pending，非传输级 pause/resume）
     *
     * 仅对 Pending 状态有效：从 pending 队列移除并进入 deferred 列表。
     *
     * 对 Running/Deferred 状态不会做任何事并返回 false。若调用方需要释放并发槽位，
     * 请使用 `cancelRequest()`（或按 lane/范围使用 `cancelLaneRequests()`）。
     *
     * @param reply 要延后的响应对象
     * @return 是否成功将请求从 Pending 移入 deferred
     */
    bool deferPendingRequest(QCNetworkReply *reply);

    /**
     * @brief 恢复调度请求（从 deferred 列表重新入队）
     *
     * 当前实现会保留 defer 前的原始 priority 重新入队，不做静默降级。
     *
     * @param reply 要恢复调度的响应对象
     */
    void undeferRequest(QCNetworkReply *reply);

    /**
     * @brief 取消请求
     *
     * 取消请求并从队列中移除。
     *
     * @param reply 要取消的响应对象
     */
    void cancelRequest(QCNetworkReply *reply);

    /**
     * @brief 取消所有请求
     *
     * 取消所有等待中和执行中的请求。
     */
    void cancelAllRequests();

    /**
     * @brief 取消指定 lane 的请求
     *
     * `PendingOnly` 适合只清理排队中的控制/数据面请求，
     * `PendingAndRunning` 则会把正在执行的同 lane 请求也纳入取消。
     *
     * @return 被取消的请求数量
     */
    int cancelLaneRequests(const QString &lane, CancelLaneScope scope);

    /**
     * @brief 动态调整请求优先级
     *
     * @param reply 要调整优先级的响应对象
     * @param newPriority 新的优先级
     *
     * @note 只能调整 pending 中的请求；不会抢占/影响已 Running 的请求，也不会触发对 Running 的隐式 cancel/pause。
     */
    void changePriority(QCNetworkReply *reply, QCNetworkRequestPriority newPriority);

    /**
     * @brief 获取统计信息
     *
     * @return 当前统计信息的副本
     */
    Statistics statistics() const;

    /**
     * @brief 获取所有等待中的请求
     *
     * @return 等待中的请求列表
     */
    QList<QCNetworkReply *> pendingRequests() const;

    /**
     * @brief 获取所有执行中的请求
     *
     * @return 执行中的请求列表
     */
    QList<QCNetworkReply *> runningRequests() const;

signals:
    /**
     * @brief 请求已加入队列
     *
     * @param reply 响应对象
     * @param lane 调度 lane 快照（空字符串表示 default lane）
     * @param hostKey origin 快照（`scheme://host:effectivePort`，IPv6 使用方括号）
     *        默认端口覆盖 `http/https/ws/wss`；未知 scheme 且 URL 未显式带端口时，
     *        `effectivePort` 固定为 `0`（不会出现 `:-1`）
     * @param priority 请求优先级
     */
    void requestQueued(QCNetworkReply *reply,
                       const QString &lane,
                       const QString &hostKey,
                       QCNetworkRequestPriority priority);

    /**
     * @brief 请求即将开始执行（about-to-start）
     *
     * 该信号表示 scheduler owner thread 已进入“即将调用 `reply->execute()`”的 handoff，
     * 且即将尝试触发 reply 的执行。
     *
     * contract:
     * - 这是最后一个可拦截点：若在该信号的 direct slot 中调用 `cancelRequest()`，调度器必须
     *   阻止后续 `reply->execute()`，并且不会发射 `requestStarted`。
     * - 若 reply 在 queued handoff 执行前已析构/已 finished/cancelled，则不会发射该信号。
     *
     * @param reply 响应对象
     * @param lane 调度 lane 快照
     * @param hostKey origin 快照（IPv6 使用方括号）
     */
    void requestAboutToStart(QCNetworkReply *reply, const QString &lane, const QString &hostKey);

    /**
     * @brief 请求已开始执行（started）
     *
     * 该信号表示 scheduler owner thread 已完成对 `reply->execute()` 的调用（启动提交点），
     * 不再表达 “about-to-start”。
     *
     * contract:
     * - `requestStarted` 只会在对应的 `requestAboutToStart` 之后发射。
     * - 显式取消（`cancelRequest/cancelAllRequests/cancelLaneRequests`）一旦在 scheduler 侧生效，
     *   不得再出现同一 reply 的 `requestStarted`。
     *
     * @param reply 响应对象
     * @param lane 调度 lane 快照
     * @param hostKey origin 快照（IPv6 使用方括号）
     */
    void requestStarted(QCNetworkReply *reply, const QString &lane, const QString &hostKey);

    /**
     * @brief 请求已完成
     *
     * @param reply 响应对象
     * @param lane 调度 lane 快照
     * @param hostKey origin 快照（IPv6 使用方括号）
     */
    void requestFinished(QCNetworkReply *reply, const QString &lane, const QString &hostKey);

    /**
     * @brief 请求已取消
     *
     * @param reply 响应对象
     * @param lane 调度 lane 快照
     * @param hostKey origin 快照（IPv6 使用方括号）
     */
    void requestCancelled(QCNetworkReply *reply, const QString &lane, const QString &hostKey);

    /**
     * @brief 队列已清空
     *
     * 当所有等待中的请求都已处理完毕时发射。
     */
    void queueEmpty();

    /**
     * @brief 带宽被限流
     *
     * 当当前带宽使用超过配置的限制时发射。
     *
     * @param currentBytesPerSec 当前每秒传输的字节数
     */
    void bandwidthThrottled(qint64 currentBytesPerSec);

private:
    friend class QCNetworkAccessManager;

    explicit QCNetworkRequestScheduler(QObject *parent = nullptr);
    ~QCNetworkRequestScheduler() override;

    // 禁止拷贝和赋值
    Q_DISABLE_COPY(QCNetworkRequestScheduler)

    struct Impl;
    QScopedPointer<Impl> m_impl;

    /**
     * @brief 调度已由 `QCNetworkAccessManager` 创建好的 reply
     *
     * 调度器仅负责排队、统计和启动，不再负责构造 reply。
     * lane/hostKey/priority 会在这里被快照，后续 scheduler 信号都引用这份快照。
     */
    void scheduleReply(QCNetworkReply *reply,
                       const QString &lane,
                       QCNetworkRequestPriority priority);

    /**
     * @brief 处理队列
     *
     * 按 per-host reservation → lane global reservation → DRR best-effort
     * 的顺序选择下一批请求，直到达到并发限制或带宽限制。
     */
    void processQueue();

    /**
     * @brief 启动请求
     *
     * 该步骤会把 `reply->execute()` 排回 reply owner thread，并维护
     * requestAboutToStart/requestStarted 的启动 handoff contract。
     *
     * @param reply 要启动的响应对象
     */
    void startRequest(QCNetworkReply *reply);

    /**
     * @brief 请求完成的槽函数
     *
     * finished/cancelled 共用同一条 finalize 路径，确保计数和信号顺序稳定。
     *
     * @param reply 完成的响应对象
     */
    void onRequestFinished(QCNetworkReply *reply);

    /**
     * @brief Reply 对象销毁处理（兜底：避免队列/统计卡死）
     *
     * 当用户在 finished 槽中直接 delete reply（而非 deleteLater）时，
     * finished 的 queued 回调可能无法执行；此处用于回收占用的并发槽位与主机计数。
     */
    void onReplyDestroyed(QObject *obj);

    /**
     * @brief 更新带宽统计
     *
     * 每秒调用一次，重置带宽窗口。
     */
    void updateBandwidthStats();

    /**
     * @brief 从队列中移除请求
     *
     * @param reply 要移除的响应对象
     * @return true 如果找到并移除
     */
    bool removeFromQueue(QCNetworkReply *reply);
};

} // namespace QCurl

#endif // QCNETWORKREQUESTSCHEDULER_H
