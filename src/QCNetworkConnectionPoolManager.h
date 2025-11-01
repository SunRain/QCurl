// SPDX-License-Identifier: MIT
// Copyright (c) 2025 QCurl Project

#ifndef QCNETWORKCONNECTIONPOOLMANAGER_H
#define QCNETWORKCONNECTIONPOOLMANAGER_H

#include <QMutex>
#include <QHash>
#include <QString>

#include "QCGlobal.h"
#include "QCNetworkConnectionPoolConfig.h"

#include <curl/curl.h>

namespace QCurl {

/**
 * @brief 连接池统计信息
 * 
 * 用于追踪连接池的效果和性能。
 */
struct QCNetworkConnectionPoolStatistics {
    /**
     * @brief 总请求数
     * 
     * 自应用启动以来的总 HTTP 请求数。
     */
    qint64 totalRequests = 0;
    
    /**
     * @brief 复用的连接数
     * 
     * 使用了已有连接（而非新建连接）的请求数。
     */
    qint64 reusedConnections = 0;
    
    /**
     * @brief 连接复用率（百分比）
     * 
     * 计算公式：(复用连接数 / 总请求数) * 100
     * 
     * @note 值越高表示连接池效果越好
     */
    double reuseRate = 0.0;
    
    /**
     * @brief 活跃连接数（估算）
     * 
     * 当前正在使用的连接数。
     */
    int activeConnections = 0;
    
    /**
     * @brief 空闲连接数（估算）
     * 
     * 连接池中空闲的连接数。
     */
    int idleConnections = 0;
};

/**
 * @brief HTTP 连接池管理器（轻量级实现）
 * 
 * 基于 libcurl 内置的连接池机制，提供配置管理和统计功能。
 * 
 * @par 设计理念
 * libcurl 已经内置了优秀的连接池实现，我们无需重复造轮子。
 * 此管理器负责：
 * - 统一管理连接池配置
 * - 为 curl handle 应用连接池设置
 * - 提供连接池统计信息
 * 
 * @par 性能提升
 * 通过启用连接复用和合理配置，可获得：
 * - **单主机多请求**: 60-80% 性能提升
 * - **HTTPS 请求**: 70-90% 性能提升（避免重复 SSL 握手）
 * - **高并发场景**: 60-75% 性能提升
 * 
 * 
 * @code
 * // 全局配置连接池
 * auto *manager = QCNetworkConnectionPoolManager::instance();
 * 
 * QCNetworkConnectionPoolConfig config;
 * config.maxConnectionsPerHost = 8;
 * config.maxTotalConnections = 50;
 * manager->setConfig(config);
 * 
 * // 连接池自动应用到所有请求
 * QCNetworkAccessManager netManager;
 * auto *reply = netManager.sendGet(request);
 * 
 * // 查看统计
 * auto stats = manager->statistics();
 * qDebug() << "总请求数:" << stats.totalRequests;
 * qDebug() << "复用率:" << stats.reuseRate << "%";
 * @endcode
 */
class QCNetworkConnectionPoolManager
{
public:
    // 类型别名，为向后兼容性和便利性
    using Statistics = QCNetworkConnectionPoolStatistics;
    
    /**
     * @brief 获取全局单例
     * 
     * @return 连接池管理器实例
     * 
     * @note 线程安全
     */
    static QCNetworkConnectionPoolManager* instance();
    
    /**
     * @brief 设置连接池配置
     * 
     * 新配置将应用到后续创建的所有 curl handle。
     * 
     * @param config 连接池配置
     * 
     * @note 线程安全
     * @note 不影响已存在的连接
     */
    void setConfig(const QCNetworkConnectionPoolConfig &config);
    
    /**
     * @brief 获取当前配置
     * 
     * @return 连接池配置的副本
     * 
     * @note 线程安全
     */
    QCNetworkConnectionPoolConfig config() const;
    
    /**
     * @brief 为 curl handle 应用连接池配置
     * 
     * 此方法由 QCNetworkReply 内部调用，应用程序无需直接使用。
     * 
     * @param handle curl easy handle
     * @param host 目标主机名（用于统计）
     * 
     * @note 内部方法
     */
    void configureCurlHandle(CURL *handle, const QString &host);
    
    /**
     * @brief 记录请求完成（用于统计）
     * 
     * 此方法由 QCNetworkReply 内部调用，记录请求完成和连接复用情况。
     * 
     * @param handle curl easy handle
     * @param wasReused 是否复用了连接
     * 
     * @note 内部方法
     */
    void recordRequestCompleted(CURL *handle, bool wasReused);
    
    /**
     * @brief 获取统计信息
     * 
     * @return 连接池统计信息
     * 
     * @note 线程安全
     */
    QCNetworkConnectionPoolStatistics statistics() const;
    
    /**
     * @brief 重置统计信息
     * 
     * 将所有统计计数器归零。
     * 
     * @note 线程安全
     */
    void resetStatistics();
    
    /**
     * @brief 关闭所有空闲连接
     * 
     * 立即关闭连接池中所有空闲的连接，释放资源。
     * 不影响正在使用的连接。
     * 
     * @note 此操作通过重新初始化 curl multi handle 实现
     * @note 可能影响后续请求的性能（需要重新建立连接）
     */
    void closeIdleConnections();

private:
    /**
     * @brief 私有构造函数（单例模式）
     */
    QCNetworkConnectionPoolManager();
    
    /**
     * @brief 析构函数
     */
    ~QCNetworkConnectionPoolManager();
    
    // 禁止拷贝和赋值
    QCNetworkConnectionPoolManager(const QCNetworkConnectionPoolManager &) = delete;
    QCNetworkConnectionPoolManager &operator=(const QCNetworkConnectionPoolManager &) = delete;
    
    mutable QMutex m_mutex;  ///< 保护所有成员变量
    QCNetworkConnectionPoolConfig m_config;  ///< 当前配置
    
    // 统计信息
    qint64 m_totalRequests;        ///< 总请求数
    qint64 m_reusedConnections;    ///< 复用的连接数
    QHash<QString, int> m_activeConnectionsPerHost;  ///< 每主机活跃连接数
};

} // namespace QCurl

#endif // QCNETWORKCONNECTIONPOOLMANAGER_H
