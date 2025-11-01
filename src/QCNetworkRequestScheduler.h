// SPDX-License-Identifier: MIT
// Copyright (c) 2025 QCurl Project

#ifndef QCNETWORKREQUESTSCHEDULER_H
#define QCNETWORKREQUESTSCHEDULER_H

#include <QObject>
#include <QMutex>
#include <QMap>
#include <QQueue>
#include <QHash>
#include <QList>
#include <QPointer>
#include <QDateTime>
#include <QTimer>

#include "QCNetworkRequestPriority.h"
#include "QCNetworkRequest.h"
#include "QCNetworkReply.h"

namespace QCurl {

/**
 * @brief 网络请求调度器
 * 
 * 管理网络请求的优先级队列、并发控制和带宽限制。
 * 
 * 核心功能：
 * - 优先级调度：6 种优先级（VeryLow → Critical）
 * - 并发控制：全局并发限制 + 每主机并发限制
 * - 带宽限制：可配置的带宽上限（字节/秒）
 * - 请求管理：暂停、恢复、取消请求
 * - 统计信息：实时追踪请求状态和流量
 * 
 * @note 线程安全：所有公共方法都使用互斥锁保护
 * 
 * @code
 * // 启用调度器
 * QCNetworkAccessManager manager;
 * manager.enableRequestScheduler(true);
 * 
 * // 配置调度器
 * auto *scheduler = manager.scheduler();
 * QCNetworkRequestScheduler::Config config;
 * config.maxConcurrentRequests = 10;
 * config.maxRequestsPerHost = 3;
 * config.maxBandwidthBytesPerSec = 1024 * 1024;  // 1 MB/s
 * scheduler->setConfig(config);
 * 
 * // 调度高优先级请求
 * QCNetworkRequest request(QUrl("https://api.example.com"));
 * request.setPriority(QCNetworkRequestPriority::High);
 * auto *reply = manager.scheduleGet(request);
 * @endcode
 */
class QCNetworkRequestScheduler : public QObject
{
    Q_OBJECT

public:
    /**
     * @brief 调度器配置
     */
    struct Config {
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
    struct Statistics {
        int pendingRequests = 0;        ///< 等待中的请求数
        int runningRequests = 0;        ///< 执行中的请求数
        int completedRequests = 0;      ///< 已完成的请求数
        int cancelledRequests = 0;      ///< 已取消的请求数
        qint64 totalBytesReceived = 0;  ///< 总接收字节数
        qint64 totalBytesSent = 0;      ///< 总发送字节数
        double avgResponseTime = 0.0;   ///< 平均响应时间（毫秒）
    };
    
    /**
     * @brief 获取调度器单例
     * 
     * @return 调度器实例指针
     */
    static QCNetworkRequestScheduler* instance();
    
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
     * @brief 调度一个网络请求
     * 
     * 根据优先级将请求加入队列。Critical 优先级的请求会跳过队列立即执行。
     * 
     * @param request 网络请求
     * @param method HTTP 方法
     * @param priority 请求优先级（默认：Normal）
     * @param body 请求体（对于 POST/PUT/PATCH）
     * @return 网络响应对象
     * 
     * @note 返回的 QCNetworkReply 对象的生命周期由调用者管理
     */
    QCNetworkReply* scheduleRequest(
        const QCNetworkRequest &request,
        HttpMethod method,
        QCNetworkRequestPriority priority = QCNetworkRequestPriority::Normal,
        const QByteArray &body = QByteArray()
    );
    
    /**
     * @brief 暂停请求
     * 
     * 暂停等待中的请求，将其从队列中移除但不释放对象。
     * 
     * @param reply 要暂停的响应对象
     * 
     * @note 只能暂停等待中的请求，无法暂停正在执行的请求
     */
    void pauseRequest(QCNetworkReply *reply);
    
    /**
     * @brief 恢复请求
     * 
     * 将暂停的请求重新加入队列。
     * 
     * @param reply 要恢复的响应对象
     */
    void resumeRequest(QCNetworkReply *reply);
    
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
     * @note 只能调整等待中的请求，无法调整正在执行的请求
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
    QList<QCNetworkReply*> pendingRequests() const;
    
    /**
     * @brief 获取所有执行中的请求
     * 
     * @return 执行中的请求列表
     */
    QList<QCNetworkReply*> runningRequests() const;

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
    explicit QCNetworkRequestScheduler(QObject *parent = nullptr);
    ~QCNetworkRequestScheduler() override;
    
    // 禁止拷贝和赋值
    Q_DISABLE_COPY(QCNetworkRequestScheduler)
    
    /**
     * @brief 队列中的请求项
     */
    struct QueuedRequest {
        QPointer<QCNetworkReply> reply;  ///< 响应对象（使用 QPointer 防止悬空指针）
        QCNetworkRequestPriority priority;  ///< 请求优先级
        QDateTime queueTime;  ///< 加入队列的时间
        QString host;  ///< 主机名
    };
    
    mutable QMutex m_mutex;  ///< 保护所有成员变量的互斥锁
    Config m_config;  ///< 调度器配置
    
    // 优先级队列
    QMap<QCNetworkRequestPriority, QQueue<QueuedRequest>> m_pendingQueue;
    
    // 运行中的请求
    QList<QPointer<QCNetworkReply>> m_runningRequests;
    QHash<QString, int> m_hostConnectionCount;  ///< 主机连接计数
    
    // 暂停的请求
    QList<QPointer<QCNetworkReply>> m_pausedRequests;
    
    // 统计信息
    Statistics m_stats;
    QHash<QCNetworkReply*, QDateTime> m_requestStartTimes;  ///< 请求开始时间
    QHash<QCNetworkReply*, QString> m_replyHosts;  ///< Reply 对应的主机名
    
    // 带宽控制
    QTimer *m_throttleTimer;  ///< 带宽重置定时器
    qint64 m_bytesTransferredInWindow;  ///< 当前窗口内传输的字节数
    
    /**
     * @brief 处理队列
     * 
     * 从高到低优先级处理队列中的请求，直到达到并发限制或带宽限制。
     */
    void processQueue();
    
    /**
     * @brief 检查是否可以启动请求
     * 
     * @param req 队列中的请求项
     * @return true 如果可以启动
     */
    bool canStartRequest(const QueuedRequest &req) const;
    
    /**
     * @brief 启动请求
     * 
     * @param req 队列中的请求项
     */
    void startRequest(const QueuedRequest &req);
    
    /**
     * @brief 请求完成的槽函数
     * 
     * @param reply 完成的响应对象
     */
    void onRequestFinished(QCNetworkReply *reply);
    
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
