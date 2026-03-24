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
 * - `Critical` 优先级会绕过 pending 队列并立即启动；它不会抢占已 Running 的请求，但可能突破并发/每主机限制。
 * - 若调用方需要“让出槽位/终止执行”，必须显式调用 `cancelRequest()`/`deferRequest()` 等 API（Running 下 defer 会通过 cancel 终止并转入 deferred）。
 * - 调度器不再创建 `QCNetworkReply`；异步请求统一由 `QCNetworkAccessManager::send*()` 创建 reply 后交给调度器排队。
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
     * @brief 获取当前线程绑定的调度器实例
     *
     * 实现为 thread-local；不同线程拿到的是不同实例。
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
     * @brief 延后调度请求（非传输级暂停）
     *
     * - pending：从队列移除并进入 deferred 列表（不触发传输级 pause）。
     * - running：为释放并发槽位允许停止执行并转入 deferred；当前实现使用 cancel 终止执行，
     *   因此 undefer 后一般会从头重新请求。
     *
     * @param reply 要延后的响应对象
     */
    void deferRequest(QCNetworkReply *reply);

    /**
     * @brief 恢复调度请求（从 deferred 列表重新入队）
     *
     * 当前实现会以 `Normal` 优先级重新入队，不会恢复 defer 前的原始优先级。
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
     * @param priority 请求优先级
     */
    void requestQueued(QCNetworkReply *reply, QCNetworkRequestPriority priority);

    /**
     * @brief 请求已开始执行
     *
     * @param reply 响应对象
     */
    void requestStarted(QCNetworkReply *reply);

    /**
     * @brief 请求已完成
     *
     * @param reply 响应对象
     */
    void requestFinished(QCNetworkReply *reply);

    /**
     * @brief 请求已取消
     *
     * @param reply 响应对象
     */
    void requestCancelled(QCNetworkReply *reply);

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
    void scheduleReply(QCNetworkReply *reply, QCNetworkRequestPriority priority);

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
