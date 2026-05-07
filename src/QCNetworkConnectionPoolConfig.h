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

    [[nodiscard]] int maxConnectionsPerHost() const;
    void setMaxConnectionsPerHost(int value);

    [[nodiscard]] int maxTotalConnections() const;
    void setMaxTotalConnections(int value);

    /// 空值表示不设置 CURLMOPT_MAX_TOTAL_CONNECTIONS，保留 libcurl 默认行为。
    [[nodiscard]] std::optional<long> multiMaxTotalConnections() const;
    void setMultiMaxTotalConnections(long value);
    void clearMultiMaxTotalConnections();

    /// 空值表示不设置 CURLMOPT_MAX_HOST_CONNECTIONS。
    [[nodiscard]] std::optional<long> multiMaxHostConnections() const;
    void setMultiMaxHostConnections(long value);
    void clearMultiMaxHostConnections();

    /// 空值表示不设置 CURLMOPT_MAX_CONCURRENT_STREAMS。
    [[nodiscard]] std::optional<long> multiMaxConcurrentStreams() const;
    void setMultiMaxConcurrentStreams(long value);
    void clearMultiMaxConcurrentStreams();

    /// 空值表示不设置 CURLMOPT_MAXCONNECTS 的 multi 级限制。
    [[nodiscard]] std::optional<long> multiMaxConnects() const;
    void setMultiMaxConnects(long value);
    void clearMultiMaxConnects();

    [[nodiscard]] int maxIdleTime() const;
    void setMaxIdleTime(int seconds);

    [[nodiscard]] int maxConnectionLifetime() const;
    void setMaxConnectionLifetime(int seconds);

    [[nodiscard]] bool pipeliningEnabled() const;
    void setPipeliningEnabled(bool enabled);

    [[nodiscard]] bool multiplexingEnabled() const;
    void setMultiplexingEnabled(bool enabled);

    [[nodiscard]] bool dnsCacheEnabled() const;
    void setDnsCacheEnabled(bool enabled);

    [[nodiscard]] int dnsCacheTimeout() const;
    void setDnsCacheTimeout(int seconds);

    [[nodiscard]] bool connectionWarmingEnabled() const;
    void setConnectionWarmingEnabled(bool enabled);

    /// 所有数量限制非负，且单 Host 连接数不超过全局连接数时返回 true。
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
