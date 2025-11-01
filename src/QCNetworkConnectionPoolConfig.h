// SPDX-License-Identifier: MIT
// Copyright (c) 2025 QCurl Project

#ifndef QCNETWORKCONNECTIONPOOLCONFIG_H
#define QCNETWORKCONNECTIONPOOLCONFIG_H

#include "QCGlobal.h"

namespace QCurl {

/**
 * @brief HTTP 连接池配置
 * 
 * 管理 HTTP 连接的复用策略，提升性能并减少 TCP/SSL 握手开销。
 * 
 * @par 连接池原理
 * HTTP/1.1 和 HTTP/2 支持连接复用（Keep-Alive）。通过连接池：
 * - 避免重复的 TCP 三次握手
 * - 避免重复的 SSL/TLS 握手
 * - 支持 HTTP/2 多路复用
 * - 支持 HTTP/1.1 管道化（可选）
 * 
 * @par 性能提升
 * - **单主机多请求**: 80% 性能提升
 * - **HTTPS 请求**: 70-90% 性能提升（SSL 握手开销大）
 * - **高并发场景**: 60-75% 性能提升
 * 
 * 
 * @code
 * // 配置连接池
 * QCNetworkConnectionPoolConfig config;
 * config.maxConnectionsPerHost = 6;      // 每个主机最多 6 个并发连接
 * config.maxTotalConnections = 30;       // 全局最多 30 个连接
 * config.maxIdleTime = 60;               // 空闲连接 60 秒后关闭
 * config.maxConnectionLifetime = 120;    // 连接最大生命周期 120 秒
 * config.enableMultiplexing = true;      // 启用 HTTP/2 多路复用
 * 
 * auto *manager = QCNetworkConnectionPoolManager::instance();
 * manager->setConfig(config);
 * @endcode
 */
class QCNetworkConnectionPoolConfig
{
public:
    /**
     * @brief 每个主机的最大并发连接数
     * 
     * 限制对单个主机的并发连接数，防止过度占用服务器资源。
     * 
     * @par HTTP/1.1
     * 每个连接同时只能处理一个请求。建议值：4-8。
     * 
     * @par HTTP/2
     * 支持多路复用，一个连接可处理多个请求。建议值：1-2。
     * 
     * @note 默认值：6（兼顾 HTTP/1.1 和 HTTP/2）
     */
    int maxConnectionsPerHost = 6;
    
    /**
     * @brief 连接池的最大总连接数
     * 
     * 全局连接池大小，限制所有主机的连接总数。
     * 防止过多连接占用系统资源（文件描述符、内存等）。
     * 
     * @note 默认值：30
     * @note libcurl 使用此值初始化连接池（CURLOPT_MAXCONNECTS）
     */
    int maxTotalConnections = 30;
    
    /**
     * @brief 空闲连接的最大保持时间（秒）
     * 
     * 连接空闲超过此时间后会被关闭，释放资源。
     * 0 表示不限制（连接永远保持）。
     * 
     * @note 默认值：60 秒
     * @note 过小的值会降低复用率，过大的值会占用资源
     */
    int maxIdleTime = 60;
    
    /**
     * @brief 连接的最大生命周期（秒）
     * 
     * 即使连接仍在使用，超过此时间后也会被关闭并重新建立。
     * 防止长期连接导致的资源泄漏或协议问题。
     * 0 表示不限制。
     * 
     * @note 默认值：120 秒
     * @note libcurl 7.80+ 支持 CURLOPT_MAXLIFETIME_CONN
     */
    int maxConnectionLifetime = 120;
    
    /**
     * @brief 启用 HTTP/1.1 管道化（Pipelining）
     * 
     * 允许在单个连接上串行发送多个请求，无需等待响应。
     * 注意：许多服务器不支持管道化，可能导致问题。
     * 
     * @warning 默认禁用，建议仅在确认服务器支持时启用
     * @note HTTP/2 不需要管道化（内置多路复用）
     */
    bool enablePipelining = false;
    
    /**
     * @brief 启用 HTTP/2 多路复用（Multiplexing）
     * 
     * 允许在单个 HTTP/2 连接上并发处理多个请求/响应。
     * 这是 HTTP/2 的核心特性，显著提升性能。
     * 
     * @note 默认值：true
     * @note 需要 libcurl 支持 HTTP/2（--with-nghttp2）
     */
    bool enableMultiplexing = true;
    
    /**
     * @brief 启用 DNS 缓存
     * 
     * 缓存 DNS 解析结果，避免重复查询。
     * 
     * @note 默认值：true
     * @note libcurl 默认缓存 60 秒
     */
    bool enableDnsCache = true;
    
    /**
     * @brief DNS 缓存超时（秒）
     * 
     * DNS 解析结果的缓存时间。
     * -1 表示永久缓存，0 表示禁用缓存。
     * 
     * @note 默认值：60 秒
     */
    int dnsCacheTimeout = 60;
    
    /**
     * @brief 启用连接预热（Connection Warming）
     * 
     * 自动预建立连接到常用主机，减少首次请求延迟。
     * 
     * @note 默认值：false（实验性功能）
     * @warning 会增加网络流量和资源占用
     */
    bool enableConnectionWarming = false;
    
    /**
     * @brief 构造函数
     * 
     * 使用默认配置初始化。
     */
    QCNetworkConnectionPoolConfig() = default;
    
    /**
     * @brief 检查配置是否有效
     * 
     * @return true 如果配置参数合法
     */
    bool isValid() const
    {
        return maxConnectionsPerHost > 0 &&
               maxTotalConnections > 0 &&
               maxConnectionsPerHost <= maxTotalConnections &&
               maxIdleTime >= 0 &&
               maxConnectionLifetime >= 0 &&
               dnsCacheTimeout >= -1;
    }
    
    /**
     * @brief 获取预设配置：保守模式
     * 
     * 适用于资源受限的环境或需要与旧服务器兼容的场景。
     * 
     * @return 保守配置
     */
    static QCNetworkConnectionPoolConfig conservative()
    {
        QCNetworkConnectionPoolConfig config;
        config.maxConnectionsPerHost = 2;
        config.maxTotalConnections = 10;
        config.maxIdleTime = 30;
        config.maxConnectionLifetime = 60;
        config.enablePipelining = false;
        config.enableMultiplexing = false;  // 兼容模式
        return config;
    }
    
    /**
     * @brief 获取预设配置：激进模式
     * 
     * 适用于高性能需求、服务器性能良好的场景。
     * 
     * @return 激进配置
     */
    static QCNetworkConnectionPoolConfig aggressive()
    {
        QCNetworkConnectionPoolConfig config;
        config.maxConnectionsPerHost = 10;
        config.maxTotalConnections = 100;
        config.maxIdleTime = 120;
        config.maxConnectionLifetime = 300;
        config.enablePipelining = false;  // 仍然不推荐
        config.enableMultiplexing = true;
        config.enableConnectionWarming = true;
        return config;
    }
    
    /**
     * @brief 获取预设配置：HTTP/2 优化
     * 
     * 针对 HTTP/2 服务器优化的配置。
     * 
     * @return HTTP/2 优化配置
     */
    static QCNetworkConnectionPoolConfig http2Optimized()
    {
        QCNetworkConnectionPoolConfig config;
        config.maxConnectionsPerHost = 2;  // HTTP/2 多路复用，不需要太多连接
        config.maxTotalConnections = 20;
        config.maxIdleTime = 90;
        config.maxConnectionLifetime = 180;
        config.enablePipelining = false;
        config.enableMultiplexing = true;  // HTTP/2 核心特性
        return config;
    }
};

} // namespace QCurl

#endif // QCNETWORKCONNECTIONPOOLCONFIG_H
