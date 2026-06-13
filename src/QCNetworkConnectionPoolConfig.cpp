// SPDX-License-Identifier: MIT
// Copyright (c) 2025 QCurl Project

#include "QCNetworkConnectionPoolConfig.h"

#include <QSharedData>

namespace QCurl {

namespace {

constexpr int kDefaultMaxConnectionsPerHost        = 6;
constexpr int kDefaultMaxTotalConnections          = 30;
constexpr int kDefaultMaxIdleTimeSeconds           = 60;
constexpr int kDefaultMaxConnectionLifetimeSeconds = 120;
constexpr int kDefaultDnsCacheTimeoutSeconds       = 60;

constexpr int kConservativeMaxConnectionsPerHost        = 2;
constexpr int kConservativeMaxTotalConnections          = 10;
constexpr int kConservativeMaxIdleTimeSeconds           = 30;
constexpr int kConservativeMaxConnectionLifetimeSeconds = 60;

constexpr int kAggressiveMaxConnectionsPerHost        = 10;
constexpr int kAggressiveMaxTotalConnections          = 100;
constexpr int kAggressiveMaxIdleTimeSeconds           = 120;
constexpr int kAggressiveMaxConnectionLifetimeSeconds = 300;

constexpr int kHttp2MaxConnectionsPerHost        = 2;
constexpr int kHttp2MaxTotalConnections          = 20;
constexpr int kHttp2MaxIdleTimeSeconds           = 90;
constexpr int kHttp2MaxConnectionLifetimeSeconds = 180;

} // namespace

/// 连接池配置的共享存储；空 optional 表示保留 libcurl 默认 multi 限制。
class QCNetworkConnectionPoolConfigData : public QSharedData
{
public:
    int maxConnectionsPerHost                     = kDefaultMaxConnectionsPerHost;
    int maxTotalConnections                       = kDefaultMaxTotalConnections;
    std::optional<long> multiMaxTotalConnections  = std::nullopt;
    std::optional<long> multiMaxHostConnections   = std::nullopt;
    std::optional<long> multiMaxConcurrentStreams = std::nullopt;
    std::optional<long> multiMaxConnects          = std::nullopt;
    int maxIdleTime                               = kDefaultMaxIdleTimeSeconds;
    int maxConnectionLifetime                     = kDefaultMaxConnectionLifetimeSeconds;
    bool enablePipelining                         = false;
    bool enableMultiplexing                       = true;
    bool enableDnsCache                           = true;
    int dnsCacheTimeout                           = kDefaultDnsCacheTimeoutSeconds;
    bool enableConnectionWarming                  = false;
};

QCNetworkConnectionPoolConfig::QCNetworkConnectionPoolConfig()
    : d(new QCNetworkConnectionPoolConfigData)
{}

QCNetworkConnectionPoolConfig::QCNetworkConnectionPoolConfig(
    const QCNetworkConnectionPoolConfig &other) = default;

QCNetworkConnectionPoolConfig::QCNetworkConnectionPoolConfig(
    QCNetworkConnectionPoolConfig &&other) noexcept = default;

QCNetworkConnectionPoolConfig::~QCNetworkConnectionPoolConfig() = default;

QCNetworkConnectionPoolConfig &QCNetworkConnectionPoolConfig::operator=(
    const QCNetworkConnectionPoolConfig &other) = default;

QCNetworkConnectionPoolConfig &QCNetworkConnectionPoolConfig::operator=(
    QCNetworkConnectionPoolConfig &&other) noexcept = default;

int QCNetworkConnectionPoolConfig::maxConnectionsPerHost() const
{
    return d->maxConnectionsPerHost;
}

void QCNetworkConnectionPoolConfig::setMaxConnectionsPerHost(int value)
{
    d->maxConnectionsPerHost = value;
}

int QCNetworkConnectionPoolConfig::maxTotalConnections() const
{
    return d->maxTotalConnections;
}

void QCNetworkConnectionPoolConfig::setMaxTotalConnections(int value)
{
    d->maxTotalConnections = value;
}

std::optional<long> QCNetworkConnectionPoolConfig::multiMaxTotalConnections() const
{
    return d->multiMaxTotalConnections;
}

void QCNetworkConnectionPoolConfig::setMultiMaxTotalConnections(long value)
{
    d->multiMaxTotalConnections = value;
}

void QCNetworkConnectionPoolConfig::clearMultiMaxTotalConnections()
{
    d->multiMaxTotalConnections.reset();
}

std::optional<long> QCNetworkConnectionPoolConfig::multiMaxHostConnections() const
{
    return d->multiMaxHostConnections;
}

void QCNetworkConnectionPoolConfig::setMultiMaxHostConnections(long value)
{
    d->multiMaxHostConnections = value;
}

void QCNetworkConnectionPoolConfig::clearMultiMaxHostConnections()
{
    d->multiMaxHostConnections.reset();
}

std::optional<long> QCNetworkConnectionPoolConfig::multiMaxConcurrentStreams() const
{
    return d->multiMaxConcurrentStreams;
}

void QCNetworkConnectionPoolConfig::setMultiMaxConcurrentStreams(long value)
{
    d->multiMaxConcurrentStreams = value;
}

void QCNetworkConnectionPoolConfig::clearMultiMaxConcurrentStreams()
{
    d->multiMaxConcurrentStreams.reset();
}

std::optional<long> QCNetworkConnectionPoolConfig::multiMaxConnects() const
{
    return d->multiMaxConnects;
}

void QCNetworkConnectionPoolConfig::setMultiMaxConnects(long value)
{
    d->multiMaxConnects = value;
}

void QCNetworkConnectionPoolConfig::clearMultiMaxConnects()
{
    d->multiMaxConnects.reset();
}

int QCNetworkConnectionPoolConfig::maxIdleTime() const
{
    return d->maxIdleTime;
}

void QCNetworkConnectionPoolConfig::setMaxIdleTime(int seconds)
{
    d->maxIdleTime = seconds;
}

int QCNetworkConnectionPoolConfig::maxConnectionLifetime() const
{
    return d->maxConnectionLifetime;
}

void QCNetworkConnectionPoolConfig::setMaxConnectionLifetime(int seconds)
{
    d->maxConnectionLifetime = seconds;
}

bool QCNetworkConnectionPoolConfig::pipeliningEnabled() const
{
    return d->enablePipelining;
}

void QCNetworkConnectionPoolConfig::setPipeliningEnabled(bool enabled)
{
    d->enablePipelining = enabled;
}

bool QCNetworkConnectionPoolConfig::multiplexingEnabled() const
{
    return d->enableMultiplexing;
}

void QCNetworkConnectionPoolConfig::setMultiplexingEnabled(bool enabled)
{
    d->enableMultiplexing = enabled;
}

bool QCNetworkConnectionPoolConfig::dnsCacheEnabled() const
{
    return d->enableDnsCache;
}

void QCNetworkConnectionPoolConfig::setDnsCacheEnabled(bool enabled)
{
    d->enableDnsCache = enabled;
}

int QCNetworkConnectionPoolConfig::dnsCacheTimeout() const
{
    return d->dnsCacheTimeout;
}

void QCNetworkConnectionPoolConfig::setDnsCacheTimeout(int seconds)
{
    d->dnsCacheTimeout = seconds;
}

bool QCNetworkConnectionPoolConfig::connectionWarmingEnabled() const
{
    return d->enableConnectionWarming;
}

void QCNetworkConnectionPoolConfig::setConnectionWarmingEnabled(bool enabled)
{
    d->enableConnectionWarming = enabled;
}

bool QCNetworkConnectionPoolConfig::isValid() const
{
    return d->maxConnectionsPerHost > 0 && d->maxTotalConnections > 0
           && d->maxConnectionsPerHost <= d->maxTotalConnections && d->maxIdleTime >= 0
           && d->maxConnectionLifetime >= 0 && d->dnsCacheTimeout >= -1
           && (!d->multiMaxTotalConnections.has_value() || d->multiMaxTotalConnections.value() >= 0)
           && (!d->multiMaxHostConnections.has_value() || d->multiMaxHostConnections.value() >= 0)
           && (!d->multiMaxConcurrentStreams.has_value()
               || d->multiMaxConcurrentStreams.value() >= 0)
           && (!d->multiMaxConnects.has_value() || d->multiMaxConnects.value() >= 0);
}

QCNetworkConnectionPoolConfig QCNetworkConnectionPoolConfig::conservative()
{
    QCNetworkConnectionPoolConfig config;
    config.setMaxConnectionsPerHost(kConservativeMaxConnectionsPerHost);
    config.setMaxTotalConnections(kConservativeMaxTotalConnections);
    config.setMaxIdleTime(kConservativeMaxIdleTimeSeconds);
    config.setMaxConnectionLifetime(kConservativeMaxConnectionLifetimeSeconds);
    config.setPipeliningEnabled(false);
    config.setMultiplexingEnabled(false);
    return config;
}

QCNetworkConnectionPoolConfig QCNetworkConnectionPoolConfig::aggressive()
{
    QCNetworkConnectionPoolConfig config;
    config.setMaxConnectionsPerHost(kAggressiveMaxConnectionsPerHost);
    config.setMaxTotalConnections(kAggressiveMaxTotalConnections);
    config.setMaxIdleTime(kAggressiveMaxIdleTimeSeconds);
    config.setMaxConnectionLifetime(kAggressiveMaxConnectionLifetimeSeconds);
    config.setPipeliningEnabled(false);
    config.setMultiplexingEnabled(true);
    config.setConnectionWarmingEnabled(true);
    return config;
}

QCNetworkConnectionPoolConfig QCNetworkConnectionPoolConfig::http2Optimized()
{
    QCNetworkConnectionPoolConfig config;
    config.setMaxConnectionsPerHost(kHttp2MaxConnectionsPerHost);
    config.setMaxTotalConnections(kHttp2MaxTotalConnections);
    config.setMaxIdleTime(kHttp2MaxIdleTimeSeconds);
    config.setMaxConnectionLifetime(kHttp2MaxConnectionLifetimeSeconds);
    config.setPipeliningEnabled(false);
    config.setMultiplexingEnabled(true);
    return config;
}

} // namespace QCurl
