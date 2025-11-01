// SPDX-License-Identifier: MIT
// Copyright (c) 2025 QCurl Project

#include "QCNetworkConnectionPoolManager.h"

#include <QMutexLocker>
#include <QDebug>

namespace QCurl {

// ============================================================================
// 单例实现
// ============================================================================

QCNetworkConnectionPoolManager* QCNetworkConnectionPoolManager::instance()
{
    static QCNetworkConnectionPoolManager instance;
    return &instance;
}

// ============================================================================
// 构造与析构
// ============================================================================

QCNetworkConnectionPoolManager::QCNetworkConnectionPoolManager()
    : m_totalRequests(0),
      m_reusedConnections(0)
{
    qDebug() << "QCNetworkConnectionPoolManager: Initialized with default config";
    qDebug() << "  - maxConnectionsPerHost:" << m_config.maxConnectionsPerHost;
    qDebug() << "  - maxTotalConnections:" << m_config.maxTotalConnections;
    qDebug() << "  - HTTP/2 multiplexing:" << (m_config.enableMultiplexing ? "enabled" : "disabled");
}

QCNetworkConnectionPoolManager::~QCNetworkConnectionPoolManager()
{
    qDebug() << "QCNetworkConnectionPoolManager: Destroyed";
    qDebug() << "  - Total requests:" << m_totalRequests;
    qDebug() << "  - Reused connections:" << m_reusedConnections;
    if (m_totalRequests > 0) {
        qDebug() << "  - Reuse rate:" << (m_reusedConnections * 100.0 / m_totalRequests) << "%";
    }
}

// ============================================================================
// 配置管理
// ============================================================================

void QCNetworkConnectionPoolManager::setConfig(const QCNetworkConnectionPoolConfig &config)
{
    if (!config.isValid()) {
        qWarning() << "QCNetworkConnectionPoolManager::setConfig: Invalid config, ignored";
        return;
    }
    
    QMutexLocker locker(&m_mutex);
    
    if (m_config.maxConnectionsPerHost != config.maxConnectionsPerHost ||
        m_config.maxTotalConnections != config.maxTotalConnections ||
        m_config.enableMultiplexing != config.enableMultiplexing) {
        qDebug() << "QCNetworkConnectionPoolManager: Config changed";
        qDebug() << "  - maxConnectionsPerHost:" << config.maxConnectionsPerHost;
        qDebug() << "  - maxTotalConnections:" << config.maxTotalConnections;
        qDebug() << "  - HTTP/2 multiplexing:" << (config.enableMultiplexing ? "enabled" : "disabled");
    }
    
    m_config = config;
}

QCNetworkConnectionPoolConfig QCNetworkConnectionPoolManager::config() const
{
    QMutexLocker locker(&m_mutex);
    return m_config;
}

// ============================================================================
// curl handle 配置
// ============================================================================

void QCNetworkConnectionPoolManager::configureCurlHandle(CURL *handle, const QString &host)
{
    if (!handle) {
        return;
    }
    
    QMutexLocker locker(&m_mutex);
    QCNetworkConnectionPoolConfig cfg = m_config;  // 复制一份，减少锁持有时间
    locker.unlock();
    
    // ========================================
    // 1. 连接池大小配置
    // ========================================
    
    // CURLOPT_MAXCONNECTS: 连接池的最大连接数
    // libcurl 会维护一个连接池，复用空闲连接
    curl_easy_setopt(handle, CURLOPT_MAXCONNECTS, cfg.maxTotalConnections);
    
    // ========================================
    // 2. 连接复用配置
    // ========================================
    
    // CURLOPT_FRESH_CONNECT: 强制新连接（0 = 允许复用）
    curl_easy_setopt(handle, CURLOPT_FRESH_CONNECT, 0L);
    
    // CURLOPT_FORBID_REUSE: 禁止复用（0 = 允许复用）
    curl_easy_setopt(handle, CURLOPT_FORBID_REUSE, 0L);
    
    // ========================================
    // 3. Keep-Alive 配置
    // ========================================
    
    // CURLOPT_TCP_KEEPALIVE: 启用 TCP Keep-Alive
    curl_easy_setopt(handle, CURLOPT_TCP_KEEPALIVE, 1L);
    
    // CURLOPT_TCP_KEEPIDLE: Keep-Alive 空闲时间（秒）
    curl_easy_setopt(handle, CURLOPT_TCP_KEEPIDLE, static_cast<long>(cfg.maxIdleTime));
    
    // CURLOPT_TCP_KEEPINTVL: Keep-Alive 探测间隔（秒）
    curl_easy_setopt(handle, CURLOPT_TCP_KEEPINTVL, 30L);
    
    // ========================================
    // 4. DNS 缓存配置
    // ========================================
    
    if (cfg.enableDnsCache) {
        // CURLOPT_DNS_CACHE_TIMEOUT: DNS 缓存超时（秒）
        // -1 = 永久缓存，0 = 禁用缓存
        curl_easy_setopt(handle, CURLOPT_DNS_CACHE_TIMEOUT, 
                        static_cast<long>(cfg.dnsCacheTimeout));
    } else {
        curl_easy_setopt(handle, CURLOPT_DNS_CACHE_TIMEOUT, 0L);
    }
    
    // ========================================
    // 5. HTTP/2 多路复用配置
    // ========================================
    
    if (cfg.enableMultiplexing) {
        // CURLOPT_HTTP_VERSION: 强制使用 HTTP/2
        // libcurl 会自动协商并启用多路复用
        curl_easy_setopt(handle, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_2_0);
    }
    
    // ========================================
    // 6. HTTP/1.1 管道化配置（可选，默认禁用）
    // ========================================
    
    if (cfg.enablePipelining) {
        // CURLOPT_PIPEWAIT: 等待管道化
        curl_easy_setopt(handle, CURLOPT_PIPEWAIT, 1L);
        
        qDebug() << "QCNetworkConnectionPoolManager: HTTP/1.1 pipelining enabled for" << host;
    }
    
    // ========================================
    // 7. 连接生命周期配置（libcurl 7.80+）
    // ========================================
    
#if LIBCURL_VERSION_NUM >= 0x075000
    if (cfg.maxConnectionLifetime > 0) {
        // CURLOPT_MAXLIFETIME_CONN: 连接最大生命周期（秒）
        curl_easy_setopt(handle, CURLOPT_MAXLIFETIME_CONN, 
                        static_cast<long>(cfg.maxConnectionLifetime));
    }
#endif
    
    // ========================================
    // 8. 更新主机连接计数（用于统计）
    // ========================================
    
    locker.relock();
    m_activeConnectionsPerHost[host]++;
}

// ============================================================================
// 统计管理
// ============================================================================

void QCNetworkConnectionPoolManager::recordRequestCompleted(CURL *handle, bool wasReused)
{
    if (!handle) {
        return;
    }
    
    // 从 curl handle 获取连接是否被复用的信息
    long connectionsInPool = 0;
    curl_easy_getinfo(handle, CURLINFO_NUM_CONNECTS, &connectionsInPool);
    
    // 如果 wasReused 为 true 或者 connectionsInPool == 1，说明复用了连接
    // connectionsInPool == 1 表示这次请求创建了新连接
    // connectionsInPool > 1 表示复用了已有连接
    bool actuallyReused = wasReused || (connectionsInPool > 1);
    
    QMutexLocker locker(&m_mutex);
    m_totalRequests++;
    
    if (actuallyReused) {
        m_reusedConnections++;
    }
    
}

QCNetworkConnectionPoolStatistics QCNetworkConnectionPoolManager::statistics() const
{
    QMutexLocker locker(&m_mutex);
    
    QCNetworkConnectionPoolStatistics stats;
    stats.totalRequests = m_totalRequests;
    stats.reusedConnections = m_reusedConnections;
    
    if (m_totalRequests > 0) {
        stats.reuseRate = (m_reusedConnections * 100.0) / m_totalRequests;
    }
    
    // 活跃连接数 = 所有主机的活跃连接总和
    stats.activeConnections = 0;
    for (int count : m_activeConnectionsPerHost) {
        stats.activeConnections += count;
    }
    
    // 空闲连接数 = 配置的总连接数 - 活跃连接数（估算）
    stats.idleConnections = qMax(0, m_config.maxTotalConnections - stats.activeConnections);
    
    return stats;
}

void QCNetworkConnectionPoolManager::resetStatistics()
{
    QMutexLocker locker(&m_mutex);
    
    qDebug() << "QCNetworkConnectionPoolManager: Resetting statistics";
    
    m_totalRequests = 0;
    m_reusedConnections = 0;
    m_activeConnectionsPerHost.clear();
}

void QCNetworkConnectionPoolManager::closeIdleConnections()
{
    qDebug() << "QCNetworkConnectionPoolManager: Closing idle connections";
    
    // libcurl 没有直接的 API 关闭连接池中的空闲连接
    // 这里只是提供接口，实际上连接会在超时后自动关闭
    
    // 可以通过触发 curl_multi_cleanup + curl_multi_init 来清空连接池
    // 但这需要访问 QCCurlMultiManager，超出了本管理器的职责范围
    
    qWarning() << "QCNetworkConnectionPoolManager::closeIdleConnections: "
               << "Idle connections will be closed automatically after timeout ("
               << m_config.maxIdleTime << "seconds)";
}

} // namespace QCurl
