#include "QCWebSocketPool.h"

#ifdef QCURL_WEBSOCKET_SUPPORT

#include "QCWebSocket.h"
#include "private/QCWebSocketPoolPrivate_p.h"

#include <QDateTime>
#include <QMetaObject>
#include <QPointer>
#include <QPromise>
#include <QThread>
#include <QTimer>

namespace QCurl {

namespace {

template<typename Result>
QFuture<Result> finishedFuture(const Result &result)
{
    QPromise<Result> promise;
    promise.start();
    promise.addResult(result);
    promise.finish();
    return promise.future();
}

} // namespace

QCWebSocketPool::QCWebSocketPool(const QCWebSocketPoolConfig &config, QObject *parent)
    : QObject(parent)
    , d_ptr(new QCWebSocketPoolPrivate)
{
    const auto validation = QCWebSocketPoolPrivate::validateConfig(config);
    d_ptr->config         = validation.valid ? config : QCWebSocketPoolConfig();
    d_ptr->cleanupTimer   = new QTimer(this);
    connect(d_ptr->cleanupTimer, &QTimer::timeout, this, &QCWebSocketPool::onCleanupTimer);
    d_ptr->cleanupTimer->start(60000);

    if (d_ptr->config.enableKeepAlive()) {
        d_ptr->keepAliveTimer = new QTimer(this);
        connect(d_ptr->keepAliveTimer, &QTimer::timeout, this, &QCWebSocketPool::onKeepAliveTimer);
        const auto interval = QCWebSocketPoolPrivate::keepAliveIntervalMilliseconds(
            d_ptr->config.keepAliveInterval());
        if (interval.has_value()) {
            d_ptr->keepAliveTimer->start(interval.value());
        }
    }
}

QCWebSocketPool::QCWebSocketPool(QObject *parent)
    : QCWebSocketPool(QCWebSocketPoolConfig(), parent)
{}

QCWebSocketPool::~QCWebSocketPool()
{
    d_ptr->lifecycleState = QCWebSocketPoolPrivate::LifecycleState::Destroying;
    for (auto &pool : d_ptr->pools) {
        for (auto &conn : pool) {
            if (conn.acquirePromise) {
                conn.acquirePromise->addResult(
                    QCWebSocketAcquireResult::failure(QCWebSocketAcquireResult::Status::PoolDestroyed,
                                                      QStringLiteral("pool destroyed")));
                conn.acquirePromise->finish();
                conn.acquirePromise.reset();
            }
        }
    }
    d_ptr->clearConnections(this, QUrl());
    for (auto &operation : d_ptr->preWarmOperations) {
        if (!operation->finished) {
            operation->finished = true;
            operation->promise->addResult(
                QCWebSocketPreWarmResult::failure(QCWebSocketPreWarmResult::Status::PoolDestroyed,
                                                  operation->requested,
                                                  operation->warmed,
                                                  QStringLiteral("pool destroyed")));
            operation->promise->finish();
        }
    }
}

QFuture<QCWebSocketAcquireResult> QCWebSocketPool::acquire(const QUrl &url)
{
    if (d_ptr->lifecycleState == QCWebSocketPoolPrivate::LifecycleState::Destroying) {
        return finishedFuture(
            QCWebSocketAcquireResult::failure(QCWebSocketAcquireResult::Status::PoolDestroyed,
                                              QStringLiteral("pool is destroying")));
    }
    if (QThread::currentThread() != thread()) {
        return finishedFuture(QCWebSocketAcquireResult::failure(
            QCWebSocketAcquireResult::Status::WrongThread,
            QStringLiteral("QCWebSocketPool::acquire requires the owner thread")));
    }

    auto promise = std::make_shared<QPromise<QCWebSocketAcquireResult>>();
    promise->start();
    const auto future = promise->future();

    auto &pool = d_ptr->pools[url];
    for (auto &conn : pool) {
        if (conn.socket && !conn.inUse && conn.socket->state() == QCWebSocket::State::Connected) {
            const auto leaseId = d_ptr->issueLease(conn.identity);
            if (!leaseId.has_value()) {
                promise->addResult(QCWebSocketAcquireResult::failure(
                    QCWebSocketAcquireResult::Status::ConnectionFailed,
                    QStringLiteral("WebSocket lease id space exhausted")));
                promise->finish();
                return future;
            }
            conn.inUse        = true;
            conn.leaseId      = leaseId.value();
            conn.lastUsedTime = QDateTime::currentDateTime();
            conn.reuseCount++;
            d_ptr->hitCounts[url]++;
            promise->addResult(QCWebSocketAcquireResult::success(leaseId.value()));
            promise->finish();
            Q_EMIT connectionReused(url);
            return future;
        }
    }

    if (!canCreateConnection(url)) {
        promise->addResult(
            QCWebSocketAcquireResult::failure(QCWebSocketAcquireResult::Status::PoolLimitReached,
                                              QStringLiteral("connection pool limit reached")));
        promise->finish();
        Q_EMIT poolLimitReached(url);
        return future;
    }

    auto *socket = createNewConnection(url);
    if (!socket) {
        promise->addResult(
            QCWebSocketAcquireResult::failure(QCWebSocketAcquireResult::Status::ConnectionFailed,
                                              QStringLiteral("unable to create WebSocket")));
        promise->finish();
        return future;
    }
    const auto leaseId = d_ptr->issueLease(socket);
    if (!leaseId.has_value()) {
        socket->deleteLater();
        promise->addResult(
            QCWebSocketAcquireResult::failure(QCWebSocketAcquireResult::Status::ConnectionFailed,
                                              QStringLiteral(
                                                  "WebSocket lease id space exhausted")));
        promise->finish();
        return future;
    }

    QCWebSocketPoolPrivate::PooledConnection conn;
    conn.socket         = socket;
    conn.identity       = socket;
    conn.lastUsedTime   = QDateTime::currentDateTime();
    conn.createdTime    = conn.lastUsedTime;
    conn.inUse          = true;
    conn.leaseId        = leaseId.value();
    conn.acquirePromise = promise;
    pool.append(conn);
    d_ptr->socketToUrl.insert(socket, url);
    d_ptr->missCounts[url]++;
    static_cast<void>(socket->open());
    return future;
}

bool QCWebSocketPool::contains(const QUrl &url) const
{
    if (d_ptr->lifecycleState == QCWebSocketPoolPrivate::LifecycleState::Destroying
        || QThread::currentThread() != thread()) {
        return false;
    }
    const auto it = d_ptr->pools.constFind(url);
    if (it == d_ptr->pools.cend()) {
        return false;
    }
    for (const auto &conn : it.value()) {
        if (conn.socket) {
            return true;
        }
    }
    return false;
}

QFuture<QCWebSocketPreWarmResult> QCWebSocketPool::preWarm(const QUrl &url, int count)
{
    if (d_ptr->lifecycleState == QCWebSocketPoolPrivate::LifecycleState::Destroying) {
        return finishedFuture(
            QCWebSocketPreWarmResult::failure(QCWebSocketPreWarmResult::Status::PoolDestroyed,
                                              count,
                                              0,
                                              QStringLiteral("pool is destroying")));
    }
    if (QThread::currentThread() != thread()) {
        return finishedFuture(QCWebSocketPreWarmResult::failure(
            QCWebSocketPreWarmResult::Status::WrongThread,
            count,
            0,
            QStringLiteral("QCWebSocketPool::preWarm requires the owner thread")));
    }
    const auto validation = QCWebSocketPoolPrivate::validatePreWarmCount(count);
    if (!validation.valid) {
        return finishedFuture(
            QCWebSocketPreWarmResult::failure(QCWebSocketPreWarmResult::Status::InvalidArgument,
                                              count,
                                              0,
                                              validation.error));
    }
    if (count == 0) {
        return finishedFuture(QCWebSocketPreWarmResult::success(count, 0));
    }

    auto operation     = std::make_shared<QCWebSocketPoolPrivate::PreWarmOperation>();
    operation->promise = std::make_shared<QPromise<QCWebSocketPreWarmResult>>();
    operation->promise->start();
    operation->requested = count;
    d_ptr->preWarmOperations.append(operation);

    operation->pending = count;
    for (int i = 0; i < count; ++i) {
        acquire(url).then(this, [this, operation](QCWebSocketAcquireResult result) {
            /**
             * @brief 在当前信号回调结束后结算预热归还结果。
             *
             * connectionCreated() 允许同步重入清理连接池；延后一轮事件循环后，
             * release() 能够观察到最新索引并将失败转换为 ConnectionFailed。
             */
            QMetaObject::invokeMethod(
                this,
                [this, operation, result]() mutable {
                    if (result.isSuccess()) {
                        if (release(result.leaseId()) != LeaseResult::Success) {
                            result = QCWebSocketAcquireResult::failure(
                                QCWebSocketAcquireResult::Status::ConnectionFailed,
                                QStringLiteral("preWarm 归还连接失败"));
                        }
                    }
                    d_ptr->completePreWarmAcquire(operation, result);
                },
                Qt::QueuedConnection);
        });
    }
    return operation->promise->future();
}

QCWebSocketPoolConfig QCWebSocketPool::config() const
{
    if (d_ptr->lifecycleState == QCWebSocketPoolPrivate::LifecycleState::Destroying
        || QThread::currentThread() != thread()) {
        return {};
    }
    return d_ptr->config;
}

} // namespace QCurl

#endif // QCURL_WEBSOCKET_SUPPORT
