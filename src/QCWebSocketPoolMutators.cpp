#include "QCWebSocketPool.h"

#ifdef QCURL_WEBSOCKET_SUPPORT

#include "QCWebSocket.h"
#include "private/QCWebSocketPoolPrivate_p.h"

#include <QDateTime>
#include <QThread>
#include <QTimer>

#include <atomic>
#include <limits>
#include <utility>

namespace QCurl {

namespace {

/// 每个 URL 允许创建的最大连接数。
constexpr int kMaxPoolSize = 1024;

/// 所有 URL 允许创建的最大连接数。
constexpr int kMaxTotalConnections = 4096;

/// 单次 preWarm() 允许请求的最大连接数。
constexpr int kMaxPreWarmCount = 1024;

/// 空闲连接允许使用的最大超时秒数。
constexpr int kMaxIdleTimeSeconds = 7 * 24 * 60 * 60;

/// 保活间隔转换为毫秒后仍能表示为 int 的最大秒数。
constexpr int kMaxKeepAliveIntervalSeconds = std::numeric_limits<int>::max() / 1000;

/// 为每个连接池实例分配进程内不复用的 32 位 generation。
std::atomic<quint64> s_nextLeasePoolGeneration{1};

/**
 * @brief 写入同步 mutator 的失败诊断。
 * @param error 可选的错误输出。
 * @param message 稳定的失败诊断文本。
 * @return 始终返回 `false`，用于直接结束失败路径。
 */
[[nodiscard]] bool failMutation(QString *error, const QString &message)
{
    if (error) {
        *error = message;
    }
    return false;
}

/**
 * @brief 清理成功 mutator 的错误输出。
 * @param error 可选的错误输出。
 * @return 始终返回 `true`，用于直接结束成功路径。
 */
[[nodiscard]] bool succeedMutation(QString *error)
{
    if (error) {
        error->clear();
    }
    return true;
}

} // namespace

QCWebSocketPoolPrivate::QCWebSocketPoolPrivate()
{
    const auto generation = s_nextLeasePoolGeneration.fetch_add(1, std::memory_order_relaxed);
    if (generation <= std::numeric_limits<quint32>::max()) {
        leasePoolGeneration = static_cast<quint32>(generation);
    }
}

std::optional<QCWebSocketAcquireResult::LeaseId> QCWebSocketPoolPrivate::issueLease(QObject *identity)
{
    if (!identity || leasePoolGeneration == 0
        || nextLeaseSequence > std::numeric_limits<quint32>::max()) {
        return std::nullopt;
    }
    const auto sequence           = static_cast<quint32>(nextLeaseSequence++);
    const auto leaseId            = (static_cast<quint64>(leasePoolGeneration) << 32) | sequence;
    issuedLeaseSequenceUpperBound = sequence;
    activeLeases.insert(leaseId, identity);
    return leaseId;
}

void QCWebSocketPoolPrivate::invalidateLease(const QCWebSocketAcquireResult::LeaseId leaseId)
{
    if (leaseId != 0) {
        activeLeases.remove(leaseId);
    }
}

bool QCWebSocketPoolPrivate::wasLeaseIssued(
    const QCWebSocketAcquireResult::LeaseId leaseId) const noexcept
{
    const auto generation = static_cast<quint32>(leaseId >> 32);
    const auto sequence   = static_cast<quint32>(leaseId);
    return generation == leasePoolGeneration && sequence != 0
           && sequence <= issuedLeaseSequenceUpperBound;
}

QCWebSocketPoolPrivate::ValidationResult QCWebSocketPoolPrivate::validateConfig(
    const QCWebSocketPoolConfig &config)
{
    if (config.maxPoolSize() <= 0 || config.maxPoolSize() > kMaxPoolSize) {
        return {false, QStringLiteral("maxPoolSize 超出允许范围")};
    }
    if (config.maxTotalConnections() <= 0 || config.maxTotalConnections() > kMaxTotalConnections) {
        return {false, QStringLiteral("maxTotalConnections 超出允许范围")};
    }
    if (config.minIdleConnections() < 0 || config.minIdleConnections() > config.maxPoolSize()) {
        return {false, QStringLiteral("minIdleConnections 超过每个 URL 的连接上限")};
    }
    if (config.maxIdleTime() < 0 || config.maxIdleTime() > kMaxIdleTimeSeconds) {
        return {false, QStringLiteral("maxIdleTime 超出允许范围")};
    }
    if (!keepAliveIntervalMilliseconds(config.keepAliveInterval()).has_value()) {
        return {false, QStringLiteral("keepAliveInterval 超出允许范围")};
    }
    return {true, {}};
}

QCWebSocketPoolPrivate::ValidationResult QCWebSocketPoolPrivate::validatePreWarmCount(const int count)
{
    if (count < 0 || count > kMaxPreWarmCount) {
        return {false, QStringLiteral("preWarm 数量超出允许范围")};
    }
    return {true, {}};
}

std::optional<int> QCWebSocketPoolPrivate::keepAliveIntervalMilliseconds(const int seconds)
{
    if (seconds <= 0 || seconds > kMaxKeepAliveIntervalSeconds) {
        return std::nullopt;
    }
    const auto milliseconds = static_cast<qint64>(seconds) * 1000;
    return static_cast<int>(milliseconds);
}

void QCWebSocketPoolPrivate::clearConnections(QCWebSocketPool *poolOwner, const QUrl &url)
{
    const auto clearOne = [poolOwner, this](const QUrl &key) {
        auto it = pools.find(key);
        if (it == pools.end()) {
            return;
        }
        auto connections = std::move(it.value());
        pools.erase(it);
        hitCounts.remove(key);
        missCounts.remove(key);
        for (auto &conn : connections) {
            invalidateLease(conn.leaseId);
            if (conn.acquirePromise) {
                conn.acquirePromise->addResult(
                    QCWebSocketAcquireResult::failure(QCWebSocketAcquireResult::Status::Cancelled,
                                                      QStringLiteral("pool cleared")));
                conn.acquirePromise->finish();
            }
            socketToUrl.remove(conn.identity);
            if (conn.socket && conn.socket->state() != QCWebSocket::State::Unconnected) {
                static_cast<void>(conn.socket->close());
            }
            if (conn.socket) {
                conn.socket->deleteLater();
            }
            Q_EMIT poolOwner->connectionClosed(key);
        }
    };

    if (url.isEmpty()) {
        const auto keys = pools.keys();
        for (const auto &key : keys) {
            clearOne(key);
        }
        return;
    }
    clearOne(url);
}

QCWebSocketPool::LeaseResult QCWebSocketPool::resolveLease(const LeaseId leaseId,
                                                           QCWebSocket **socket) const
{
    if (socket) {
        *socket = nullptr;
    }
    if (QThread::currentThread() != thread()) {
        return LeaseResult::WrongThread;
    }
    if (d_ptr->lifecycleState == QCWebSocketPoolPrivate::LifecycleState::Destroying) {
        return LeaseResult::PoolDestroyed;
    }
    if (!socket) {
        return LeaseResult::InvalidArgument;
    }
    const auto identityIt = d_ptr->activeLeases.constFind(leaseId);
    if (identityIt == d_ptr->activeLeases.cend()) {
        return d_ptr->wasLeaseIssued(leaseId) ? LeaseResult::InactiveLease
                                              : LeaseResult::UnknownLease;
    }
    const auto urlIt = d_ptr->socketToUrl.constFind(*identityIt);
    if (urlIt == d_ptr->socketToUrl.cend()) {
        return LeaseResult::InactiveLease;
    }
    const auto poolIt = d_ptr->pools.constFind(*urlIt);
    if (poolIt == d_ptr->pools.cend()) {
        return LeaseResult::InactiveLease;
    }
    for (const auto &conn : poolIt.value()) {
        if (conn.identity != *identityIt || conn.leaseId != leaseId) {
            continue;
        }
        if (!conn.socket || !conn.inUse || conn.acquirePromise) {
            return LeaseResult::InactiveLease;
        }
        *socket = conn.socket.data();
        return LeaseResult::Success;
    }
    return LeaseResult::InactiveLease;
}

QCWebSocketPool::LeaseResult QCWebSocketPool::release(const LeaseId leaseId)
{
    if (QThread::currentThread() != thread()) {
        return LeaseResult::WrongThread;
    }
    if (d_ptr->lifecycleState == QCWebSocketPoolPrivate::LifecycleState::Destroying) {
        return LeaseResult::PoolDestroyed;
    }
    const auto identityIt = d_ptr->activeLeases.constFind(leaseId);
    if (identityIt == d_ptr->activeLeases.cend()) {
        return d_ptr->wasLeaseIssued(leaseId) ? LeaseResult::InactiveLease
                                              : LeaseResult::UnknownLease;
    }
    const auto urlIt = d_ptr->socketToUrl.constFind(*identityIt);
    if (urlIt == d_ptr->socketToUrl.cend()) {
        return LeaseResult::InactiveLease;
    }
    auto poolIt = d_ptr->pools.find(*urlIt);
    if (poolIt == d_ptr->pools.end()) {
        return LeaseResult::InactiveLease;
    }
    for (auto &conn : poolIt.value()) {
        if (conn.identity != *identityIt || conn.leaseId != leaseId) {
            continue;
        }
        if (!conn.socket || !conn.inUse || conn.acquirePromise) {
            return LeaseResult::InactiveLease;
        }
        d_ptr->invalidateLease(leaseId);
        conn.leaseId      = 0;
        conn.inUse        = false;
        conn.lastUsedTime = QDateTime::currentDateTime();
        return LeaseResult::Success;
    }
    return LeaseResult::InactiveLease;
}

bool QCWebSocketPool::clearPool(const QUrl &url, QString *error)
{
    if (d_ptr->lifecycleState == QCWebSocketPoolPrivate::LifecycleState::Destroying) {
        return failMutation(error, QStringLiteral("QCWebSocketPool 正在销毁"));
    }
    if (QThread::currentThread() != thread()) {
        return failMutation(error,
                            QStringLiteral("QCWebSocketPool::clearPool 只能在 owner thread 调用"));
    }
    d_ptr->clearConnections(this, url);
    return succeedMutation(error);
}

bool QCWebSocketPool::setConfig(const QCWebSocketPoolConfig &config, QString *error)
{
    if (d_ptr->lifecycleState == QCWebSocketPoolPrivate::LifecycleState::Destroying) {
        return failMutation(error, QStringLiteral("QCWebSocketPool 正在销毁"));
    }
    if (QThread::currentThread() != thread()) {
        return failMutation(error,
                            QStringLiteral("QCWebSocketPool::setConfig 只能在 owner thread 调用"));
    }

    const auto validation = QCWebSocketPoolPrivate::validateConfig(config);
    if (!validation.valid) {
        return failMutation(error, validation.error);
    }

    d_ptr->config = config;
    if (d_ptr->config.enableKeepAlive()) {
        if (!d_ptr->keepAliveTimer) {
            d_ptr->keepAliveTimer = new QTimer(this);
            connect(d_ptr->keepAliveTimer,
                    &QTimer::timeout,
                    this,
                    &QCWebSocketPool::onKeepAliveTimer);
        }
        const auto interval = QCWebSocketPoolPrivate::keepAliveIntervalMilliseconds(
            d_ptr->config.keepAliveInterval());
        if (interval.has_value()) {
            d_ptr->keepAliveTimer->start(interval.value());
        }
    } else if (d_ptr->keepAliveTimer) {
        d_ptr->keepAliveTimer->stop();
    }
    return succeedMutation(error);
}

} // namespace QCurl

#endif // QCURL_WEBSOCKET_SUPPORT
