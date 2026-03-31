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
#include <QString>

namespace QCurl {

class QCNetworkAccessManager;
class QCNetworkReply;

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
     * @brief 调度器配置
     */
    struct Config
    {
        /**
         * @brief 全局最大并发请求数
         *
         * 限制同时执行的请求总数。
         * 默认值：6
         */
        int maxConcurrentRequests = 6;

        /**
         * @brief 每个主机的最大并发请求数
         *
         * 防止单个主机被过多请求压垮。
         * 默认值：2
         */
        int maxRequestsPerHost = 2;

        /**
         * @brief 最大带宽限制（字节/秒）
         *
         * 0 表示无限制。
         * 默认值：0（无限制）
         */
        qint64 maxBandwidthBytesPerSec = 0;

        /**
         * @brief 启用流量控制
         *
         * 如果为 true，调度器会监控带宽使用情况并在超限时暂停新请求。
         * 默认值：true
         */
        bool enableThrottling = true;
    };

    /**
     * @brief 调度器统计信息
     */
    struct Statistics
    {
        int pendingRequests       = 0;   ///< 等待中的请求数
        int runningRequests       = 0;   ///< 执行中的请求数
        int completedRequests     = 0;   ///< 已完成的请求数
        int cancelledRequests     = 0;   ///< 已取消的请求数
        qint64 totalBytesReceived = 0;   ///< 总接收字节数
        qint64 totalBytesSent     = 0;   ///< 总发送字节数
        double avgResponseTime    = 0.0; ///< 平均响应时间（毫秒）
    };

    /**
     * @brief lane 级调度配置
     */
    struct LaneConfig
    {
        int weight          = 1; ///< DRR 权重（>0）
        int quantum         = 1; ///< deficit 基数（>0）
        int reservedGlobal  = 0; ///< lane 全局预留并发槽位
        int reservedPerHost = 0; ///< lane 每 hostKey 预留并发槽位
    };

    /**
     * @brief lane 取消范围
     */
    enum class CancelLaneScope {
        PendingOnly,       ///< pending + deferred
        PendingAndRunning, ///< pending + deferred + running
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
     */
    void scheduleReply(QCNetworkReply *reply,
                       const QString &lane,
                       QCNetworkRequestPriority priority);

    /**
     * @brief 处理队列
     *
     * 从高到低优先级处理队列中的请求，直到达到并发限制或带宽限制。
     */
    void processQueue();

    /**
     * @brief 启动请求
     *
     * @param reply 要启动的响应对象
     */
    void startRequest(QCNetworkReply *reply);

    /**
     * @brief 请求完成的槽函数
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
