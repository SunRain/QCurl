// SPDX-License-Identifier: MIT
// Copyright (c) 2025 QCurl Project

/**
 * @file
 * @brief 声明连接池配置项。
 */

#ifndef QCNETWORKCONNECTIONPOOLCONFIG_H
#define QCNETWORKCONNECTIONPOOLCONFIG_H

#include "QCGlobal.h"

#include <QSharedDataPointer>

#include <optional>

namespace QCurl {

class QCNetworkConnectionPoolConfigData;

/// HTTP 连接池配置，采用隐式共享以便在管理器和请求路径间按值传递。
class QCURL_EXPORT QCNetworkConnectionPoolConfig
{
public:
    QCNetworkConnectionPoolConfig();
    QCNetworkConnectionPoolConfig(const QCNetworkConnectionPoolConfig &other);
    QCNetworkConnectionPoolConfig(QCNetworkConnectionPoolConfig &&other) noexcept;
    ~QCNetworkConnectionPoolConfig();

    QCNetworkConnectionPoolConfig &operator=(const QCNetworkConnectionPoolConfig &other);
    QCNetworkConnectionPoolConfig &operator=(QCNetworkConnectionPoolConfig &&other) noexcept;

    /// 每线程 multi 的原生连接上限；空值恢复 libcurl 默认值 0（不限）。
    [[nodiscard]] std::optional<long> multiMaxTotalConnections() const;
    void setMultiMaxTotalConnections(long value);
    void clearMultiMaxTotalConnections();

    /// 每线程 multi 的单主机连接上限；空值恢复默认值 0（不限）。
    [[nodiscard]] std::optional<long> multiMaxHostConnections() const;
    void setMultiMaxHostConnections(long value);
    void clearMultiMaxHostConnections();

    /// 每连接的 HTTP/2、HTTP/3 并发流上限；空值恢复默认值 100。
    [[nodiscard]] std::optional<long> multiMaxConcurrentStreams() const;
    void setMultiMaxConcurrentStreams(long value);
    void clearMultiMaxConcurrentStreams();

    /// 每线程 multi 的连接缓存容量；空值恢复默认值 0（libcurl 自动调整）。
    [[nodiscard]] std::optional<long> multiMaxConnects() const;
    void setMultiMaxConnects(long value);
    void clearMultiMaxConnects();

    /// 允许复用的连接最大空闲年龄，映射 CURLOPT_MAXAGE_CONN，单位为秒。
    [[nodiscard]] int maxIdleTime() const;
    void setMaxIdleTime(int seconds);

    /// 允许复用的连接最大年龄，映射 CURLOPT_MAXLIFETIME_CONN，单位为秒。
    [[nodiscard]] int maxConnectionLifetime() const;
    void setMaxConnectionLifetime(int seconds);

    /// 控制 CURLMOPT_PIPELINING 的 CURLPIPE_MULTIPLEX，不改写请求的 HTTP 版本。
    [[nodiscard]] bool multiplexingEnabled() const;
    void setMultiplexingEnabled(bool enabled);

    [[nodiscard]] bool dnsCacheEnabled() const;
    void setDnsCacheEnabled(bool enabled);

    [[nodiscard]] int dnsCacheTimeout() const;
    void setDnsCacheTimeout(int seconds);

    /// 数量和年龄非负，并发流限制大于零；DNS 缓存允许 -1 表示不超时。
    [[nodiscard]] bool isValid() const;

    /**
     * @brief 获取预设配置：保守模式
     *
     * 适用于资源受限的环境或需要与旧服务器兼容的场景。
     *
     * @return 保守配置
     */
    static QCNetworkConnectionPoolConfig conservative();

    /**
     * @brief 获取预设配置：激进模式
     *
     * 适用于高性能需求、服务器性能良好的场景。
     *
     * @return 激进配置
     */
    static QCNetworkConnectionPoolConfig aggressive();

    /**
     * @brief 获取预设配置：HTTP/2 优化
     *
     * 针对 HTTP/2 服务器优化的配置。
     *
     * @return HTTP/2 优化配置
     */
    static QCNetworkConnectionPoolConfig http2Optimized();

private:
    QSharedDataPointer<QCNetworkConnectionPoolConfigData> d;
};

} // namespace QCurl

#endif // QCNETWORKCONNECTIONPOOLCONFIG_H
