#include "QCWebSocketPool.h"

#ifdef QCURL_WEBSOCKET_SUPPORT

#include "private/QCWebSocketPoolPrivate_p.h"

#include <QSharedData>
#include <QThread>

namespace QCurl {

/// WebSocket 连接池 acquire 操作的隐式共享结果存储。
class QCWebSocketAcquireResultData : public QSharedData
{
public:
    QCWebSocketAcquireResult::Status status   = QCWebSocketAcquireResult::Status::Cancelled;
    QCWebSocketAcquireResult::LeaseId leaseId = 0;
    QString error;
};

/// WebSocket 连接池预热操作的隐式共享结果存储。
class QCWebSocketPreWarmResultData : public QSharedData
{
public:
    QCWebSocketPreWarmResult::Status status = QCWebSocketPreWarmResult::Status::Cancelled;
    int requested                           = 0;
    int warmed                              = 0;
    QString error;
};

QCWebSocketAcquireResult::QCWebSocketAcquireResult()
    : d(new QCWebSocketAcquireResultData)
{}

QCWebSocketAcquireResult::QCWebSocketAcquireResult(const QCWebSocketAcquireResult &other) = default;
QCWebSocketAcquireResult::QCWebSocketAcquireResult(
    QCWebSocketAcquireResult &&other) noexcept        = default;
QCWebSocketAcquireResult::~QCWebSocketAcquireResult() = default;
QCWebSocketAcquireResult &QCWebSocketAcquireResult::operator=(
    const QCWebSocketAcquireResult &other) = default;
QCWebSocketAcquireResult &QCWebSocketAcquireResult::operator=(
    QCWebSocketAcquireResult &&other) noexcept = default;

QCWebSocketAcquireResult QCWebSocketAcquireResult::success(const LeaseId leaseId)
{
    QCWebSocketAcquireResult result;
    result.d->status  = Status::Success;
    result.d->leaseId = leaseId;
    return result;
}

QCWebSocketAcquireResult QCWebSocketAcquireResult::failure(Status status, const QString &error)
{
    QCWebSocketAcquireResult result;
    result.d->status = status;
    result.d->error  = error;
    return result;
}

QCWebSocketAcquireResult::Status QCWebSocketAcquireResult::status() const noexcept
{
    return d->status;
}
QCWebSocketAcquireResult::LeaseId QCWebSocketAcquireResult::leaseId() const noexcept
{
    return d->leaseId;
}
QString QCWebSocketAcquireResult::error() const
{
    return d->error;
}
bool QCWebSocketAcquireResult::isSuccess() const noexcept
{
    return d->status == Status::Success;
}

QCWebSocketPreWarmResult::QCWebSocketPreWarmResult()
    : d(new QCWebSocketPreWarmResultData)
{}

QCWebSocketPreWarmResult::QCWebSocketPreWarmResult(const QCWebSocketPreWarmResult &other) = default;
QCWebSocketPreWarmResult::QCWebSocketPreWarmResult(
    QCWebSocketPreWarmResult &&other) noexcept        = default;
QCWebSocketPreWarmResult::~QCWebSocketPreWarmResult() = default;
QCWebSocketPreWarmResult &QCWebSocketPreWarmResult::operator=(
    const QCWebSocketPreWarmResult &other) = default;
QCWebSocketPreWarmResult &QCWebSocketPreWarmResult::operator=(
    QCWebSocketPreWarmResult &&other) noexcept = default;

QCWebSocketPreWarmResult QCWebSocketPreWarmResult::success(int requested, int warmed)
{
    QCWebSocketPreWarmResult result;
    result.d->status    = Status::Success;
    result.d->requested = requested;
    result.d->warmed    = warmed;
    return result;
}

QCWebSocketPreWarmResult QCWebSocketPreWarmResult::failure(Status status,
                                                           int requested,
                                                           int warmed,
                                                           const QString &error)
{
    QCWebSocketPreWarmResult result;
    result.d->status    = status;
    result.d->requested = requested;
    result.d->warmed    = warmed;
    result.d->error     = error;
    return result;
}

QCWebSocketPreWarmResult::Status QCWebSocketPreWarmResult::status() const noexcept
{
    return d->status;
}
int QCWebSocketPreWarmResult::requestedCount() const noexcept
{
    return d->requested;
}
int QCWebSocketPreWarmResult::warmedCount() const noexcept
{
    return d->warmed;
}
QString QCWebSocketPreWarmResult::error() const
{
    return d->error;
}
bool QCWebSocketPreWarmResult::isSuccess() const noexcept
{
    return d->status == Status::Success;
}

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

int QCWebSocketPool::totalConnectionCount() const
{
    if (d_ptr->lifecycleState == QCWebSocketPoolPrivate::LifecycleState::Destroying) {
        return 0;
    }
    int count = 0;
    for (const auto &pool : d_ptr->pools) {
        for (const auto &conn : pool) {
            count += conn.socket ? 1 : 0;
        }
    }
    return count;
}

QCWebSocketPoolStats QCWebSocketPool::statistics(const QUrl &url) const
{
    QCWebSocketPoolStats stats;
    if (d_ptr->lifecycleState == QCWebSocketPoolPrivate::LifecycleState::Destroying
        || QThread::currentThread() != thread()) {
        return stats;
    }
    const auto addPool = [&stats](const QList<QCWebSocketPoolPrivate::PooledConnection> &pool) {
        for (const auto &conn : pool) {
            if (!conn.socket) {
                continue;
            }
            stats.setTotalConnections(stats.totalConnections() + 1);
            if (conn.inUse) {
                stats.setActiveConnections(stats.activeConnections() + 1);
            } else {
                stats.setIdleConnections(stats.idleConnections() + 1);
            }
        }
    };
    if (url.isEmpty()) {
        for (const auto &pool : d_ptr->pools) {
            addPool(pool);
        }
        for (const int count : d_ptr->hitCounts) {
            stats.setHitCount(stats.hitCount() + count);
        }
        for (const int count : d_ptr->missCounts) {
            stats.setMissCount(stats.missCount() + count);
        }
    } else {
        addPool(d_ptr->pools.value(url));
        stats.setHitCount(d_ptr->hitCounts.value(url));
        stats.setMissCount(d_ptr->missCounts.value(url));
    }
    const int totalRequests = stats.hitCount() + stats.missCount();
    if (totalRequests > 0) {
        stats.setHitRate(static_cast<double>(stats.hitCount()) / totalRequests * 100.0);
    }
    return stats;
}

} // namespace QCurl

#endif // QCURL_WEBSOCKET_SUPPORT
