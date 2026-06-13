#include "QCWebSocketPool.h"

#ifdef QCURL_WEBSOCKET_SUPPORT

#include <QSharedData>

namespace QCurl {

namespace {

constexpr int kDefaultMaxPoolSize              = 10;
constexpr int kDefaultMaxIdleTimeSeconds       = 300;
constexpr int kDefaultMinIdleConnections       = 2;
constexpr int kDefaultMaxTotalConnections      = 50;
constexpr int kDefaultKeepAliveIntervalSeconds = 30;

} // namespace

/// QCWebSocketPoolConfig 的共享负载；仅保存可复制的配置值。
class QCWebSocketPoolConfigData : public QSharedData
{
public:
    int maxPoolSize         = kDefaultMaxPoolSize;
    int maxIdleTime         = kDefaultMaxIdleTimeSeconds;
    int minIdleConnections  = kDefaultMinIdleConnections;
    int maxTotalConnections = kDefaultMaxTotalConnections;
    bool enableKeepAlive    = true;
    int keepAliveInterval   = kDefaultKeepAliveIntervalSeconds;
    bool autoReconnect      = true;
    QCNetworkSslConfig sslConfig;
};

/// QCWebSocketPoolStats 的共享负载；表示某一时刻的统计快照。
class QCWebSocketPoolStatsData : public QSharedData
{
public:
    int totalConnections  = 0;
    int activeConnections = 0;
    int idleConnections   = 0;
    int hitCount          = 0;
    int missCount         = 0;
    double hitRate        = 0.0;
};

QCWebSocketPoolConfig::QCWebSocketPoolConfig()
    : d(new QCWebSocketPoolConfigData)
{}

QCWebSocketPoolConfig::QCWebSocketPoolConfig(const QCWebSocketPoolConfig &other) = default;

QCWebSocketPoolConfig::QCWebSocketPoolConfig(QCWebSocketPoolConfig &&other) noexcept = default;

QCWebSocketPoolConfig::~QCWebSocketPoolConfig() = default;

QCWebSocketPoolConfig &QCWebSocketPoolConfig::operator=(const QCWebSocketPoolConfig &other) = default;

QCWebSocketPoolConfig &QCWebSocketPoolConfig::operator=(
    QCWebSocketPoolConfig &&other) noexcept = default;

int QCWebSocketPoolConfig::maxPoolSize() const noexcept
{
    return d->maxPoolSize;
}

void QCWebSocketPoolConfig::setMaxPoolSize(int value) noexcept
{
    d->maxPoolSize = value;
}

int QCWebSocketPoolConfig::maxIdleTime() const noexcept
{
    return d->maxIdleTime;
}

void QCWebSocketPoolConfig::setMaxIdleTime(int seconds) noexcept
{
    d->maxIdleTime = seconds;
}

int QCWebSocketPoolConfig::minIdleConnections() const noexcept
{
    return d->minIdleConnections;
}

void QCWebSocketPoolConfig::setMinIdleConnections(int value) noexcept
{
    d->minIdleConnections = value;
}

int QCWebSocketPoolConfig::maxTotalConnections() const noexcept
{
    return d->maxTotalConnections;
}

void QCWebSocketPoolConfig::setMaxTotalConnections(int value) noexcept
{
    d->maxTotalConnections = value;
}

bool QCWebSocketPoolConfig::enableKeepAlive() const noexcept
{
    return d->enableKeepAlive;
}

void QCWebSocketPoolConfig::setEnableKeepAlive(bool enabled) noexcept
{
    d->enableKeepAlive = enabled;
}

int QCWebSocketPoolConfig::keepAliveInterval() const noexcept
{
    return d->keepAliveInterval;
}

void QCWebSocketPoolConfig::setKeepAliveInterval(int seconds) noexcept
{
    d->keepAliveInterval = seconds;
}

bool QCWebSocketPoolConfig::autoReconnect() const noexcept
{
    return d->autoReconnect;
}

void QCWebSocketPoolConfig::setAutoReconnect(bool enabled) noexcept
{
    d->autoReconnect = enabled;
}

QCNetworkSslConfig QCWebSocketPoolConfig::sslConfig() const
{
    return d->sslConfig;
}

void QCWebSocketPoolConfig::setSslConfig(const QCNetworkSslConfig &config)
{
    d->sslConfig = config;
}

QCWebSocketPoolStats::QCWebSocketPoolStats()
    : d(new QCWebSocketPoolStatsData)
{}

QCWebSocketPoolStats::QCWebSocketPoolStats(const QCWebSocketPoolStats &other) = default;

QCWebSocketPoolStats::QCWebSocketPoolStats(QCWebSocketPoolStats &&other) noexcept = default;

QCWebSocketPoolStats::~QCWebSocketPoolStats() = default;

QCWebSocketPoolStats &QCWebSocketPoolStats::operator=(const QCWebSocketPoolStats &other) = default;

QCWebSocketPoolStats &QCWebSocketPoolStats::operator=(
    QCWebSocketPoolStats &&other) noexcept = default;

int QCWebSocketPoolStats::totalConnections() const noexcept
{
    return d->totalConnections;
}

void QCWebSocketPoolStats::setTotalConnections(int value) noexcept
{
    d->totalConnections = value;
}

int QCWebSocketPoolStats::activeConnections() const noexcept
{
    return d->activeConnections;
}

void QCWebSocketPoolStats::setActiveConnections(int value) noexcept
{
    d->activeConnections = value;
}

int QCWebSocketPoolStats::idleConnections() const noexcept
{
    return d->idleConnections;
}

void QCWebSocketPoolStats::setIdleConnections(int value) noexcept
{
    d->idleConnections = value;
}

int QCWebSocketPoolStats::hitCount() const noexcept
{
    return d->hitCount;
}

void QCWebSocketPoolStats::setHitCount(int value) noexcept
{
    d->hitCount = value;
}

int QCWebSocketPoolStats::missCount() const noexcept
{
    return d->missCount;
}

void QCWebSocketPoolStats::setMissCount(int value) noexcept
{
    d->missCount = value;
}

double QCWebSocketPoolStats::hitRate() const noexcept
{
    return d->hitRate;
}

void QCWebSocketPoolStats::setHitRate(double value) noexcept
{
    d->hitRate = value;
}

} // namespace QCurl

#endif // QCURL_WEBSOCKET_SUPPORT
