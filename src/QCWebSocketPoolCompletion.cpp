#include "QCWebSocketPool.h"

#ifdef QCURL_WEBSOCKET_SUPPORT

#include "QCWebSocket.h"
#include "private/QCWebSocketPoolPrivate_p.h"

#include <QThread>

#include <utility>

namespace QCurl {

void QCWebSocketPoolPrivate::completePreWarmAcquire(
    const std::shared_ptr<PreWarmOperation> &operation, const QCWebSocketAcquireResult &result)
{
    if (lifecycleState == LifecycleState::Destroying || operation->finished) {
        return;
    }
    operation->pending--;
    if (result.isSuccess()) {
        operation->warmed++;
    } else if (operation->status == QCWebSocketPreWarmResult::Status::Success) {
        switch (result.status()) {
            case QCWebSocketAcquireResult::Status::WrongThread:
                operation->status = QCWebSocketPreWarmResult::Status::WrongThread;
                break;
            case QCWebSocketAcquireResult::Status::PoolLimitReached:
                operation->status = QCWebSocketPreWarmResult::Status::PoolLimitReached;
                break;
            case QCWebSocketAcquireResult::Status::PoolDestroyed:
                operation->status = QCWebSocketPreWarmResult::Status::PoolDestroyed;
                break;
            case QCWebSocketAcquireResult::Status::Cancelled:
                operation->status = QCWebSocketPreWarmResult::Status::Cancelled;
                break;
            case QCWebSocketAcquireResult::Status::ConnectionFailed:
            case QCWebSocketAcquireResult::Status::Success:
                operation->status = QCWebSocketPreWarmResult::Status::ConnectionFailed;
                break;
        }
        operation->error = result.error();
    }
    if (operation->pending != 0) {
        return;
    }
    operation->finished    = true;
    const auto finalResult = operation->status == QCWebSocketPreWarmResult::Status::Success
                                 ? QCWebSocketPreWarmResult::success(operation->requested,
                                                                     operation->warmed)
                                 : QCWebSocketPreWarmResult::failure(operation->status,
                                                                     operation->requested,
                                                                     operation->warmed,
                                                                     operation->error);
    auto promise           = std::move(operation->promise);
    preWarmOperations.removeAll(operation);
    promise->addResult(finalResult);
    promise->finish();
}

std::optional<QCWebSocketPoolPrivate::RemovedConnection> QCWebSocketPoolPrivate::takeConnection(
    QObject *identity)
{
    const auto urlIt = socketToUrl.constFind(identity);
    if (urlIt == socketToUrl.cend()) {
        return std::nullopt;
    }
    const QUrl url = *urlIt;
    socketToUrl.remove(identity);
    auto poolIt = pools.find(url);
    if (poolIt == pools.end()) {
        return std::nullopt;
    }
    auto &pool = poolIt.value();
    for (qsizetype i = 0; i < pool.size(); ++i) {
        if (pool[i].identity != identity) {
            continue;
        }
        RemovedConnection removed{url, pool.takeAt(i)};
        invalidateLease(removed.connection.leaseId);
        if (pool.isEmpty()) {
            pools.erase(poolIt);
        }
        return removed;
    }
    return std::nullopt;
}

void QCWebSocketPoolPrivate::clearConnections(QCWebSocketPool *poolOwner,
                                              const QUrl &url,
                                              QCWebSocketAcquireResult::Status status)
{
    QMap<QUrl, QList<PooledConnection>> removed;
    if (url.isEmpty()) {
        removed = std::move(pools);
        pools.clear();
        hitCounts.clear();
        missCounts.clear();
    } else {
        removed.insert(url, pools.take(url));
        hitCounts.remove(url);
        missCounts.remove(url);
    }
    // 批量操作先撤销全部旧状态，continuation 新建的记录不属于这次清理。
    for (const auto &connections : std::as_const(removed)) {
        for (const auto &conn : connections) {
            invalidateLease(conn.leaseId);
            socketToUrl.remove(conn.identity);
        }
    }
    const QPointer<QCWebSocketPool> safePool(poolOwner);
    for (auto it = removed.begin(); it != removed.end(); ++it) {
        for (auto &conn : it.value()) {
            auto promise = std::move(conn.acquirePromise);
            if (promise) {
                promise->addResult(QCWebSocketAcquireResult::failure(
                    status,
                    status == QCWebSocketAcquireResult::Status::PoolDestroyed
                        ? QStringLiteral("pool destroyed")
                        : QStringLiteral("pool cleared")));
                promise->finish();
            }
            // finish()/close()/信号均可销毁 owner；此后只使用局部记录和观察指针。
            if (safePool && conn.socket && conn.socket->state() != QCWebSocket::State::Unconnected) {
                static_cast<void>(conn.socket->close());
            }
            if (safePool && conn.socket) {
                conn.socket->deleteLater();
            }
            if (safePool) {
                Q_EMIT safePool->connectionClosed(it.key());
            }
        }
    }
}

void QCWebSocketPool::onSocketDestroyed(QObject *object)
{
    if (d_ptr->lifecycleState == QCWebSocketPoolPrivate::LifecycleState::Destroying
        || QThread::currentThread() != thread() || !object) {
        return;
    }
    auto removed = d_ptr->takeConnection(object);
    if (!removed) {
        return;
    }
    const QPointer<QCWebSocketPool> safePool(this);
    auto promise = std::move(removed->connection.acquirePromise);
    if (promise) {
        promise->addResult(
            QCWebSocketAcquireResult::failure(QCWebSocketAcquireResult::Status::ConnectionFailed,
                                              QStringLiteral(
                                                  "WebSocket destroyed before acquire completed")));
        promise->finish();
    }
    if (safePool) {
        Q_EMIT connectionClosed(removed->url);
    }
}

void QCWebSocketPool::failPendingAcquire(QCWebSocket *socket,
                                         QCWebSocketAcquireResult::Status status,
                                         const QString &error)
{
    if (d_ptr->lifecycleState == QCWebSocketPoolPrivate::LifecycleState::Destroying) {
        return;
    }
    auto removed = d_ptr->takeConnection(socket);
    if (!removed) {
        return;
    }
    // promise 脱离容器并由栈上 owner 保活，finish 的同步重入不会释放它。
    auto promise = std::move(removed->connection.acquirePromise);
    if (promise) {
        promise->addResult(QCWebSocketAcquireResult::failure(status, error));
        promise->finish();
    }
}

void QCWebSocketPool::removeConnection(QCWebSocket *socket)
{
    if (d_ptr->lifecycleState != QCWebSocketPoolPrivate::LifecycleState::Destroying) {
        static_cast<void>(d_ptr->takeConnection(socket));
    }
}

void QCWebSocketPool::onSocketConnected()
{
    if (d_ptr->lifecycleState == QCWebSocketPoolPrivate::LifecycleState::Destroying) {
        return;
    }
    auto *socket     = qobject_cast<QCWebSocket *>(sender());
    const auto urlIt = socket ? d_ptr->socketToUrl.constFind(socket) : d_ptr->socketToUrl.cend();
    if (urlIt == d_ptr->socketToUrl.cend()) {
        return;
    }
    const QUrl url = *urlIt;
    std::shared_ptr<QPromise<QCWebSocketAcquireResult>> promise;
    LeaseId leaseId = 0;
    for (auto &conn : d_ptr->pools[url]) {
        if (conn.identity == socket && conn.acquirePromise) {
            promise = std::move(conn.acquirePromise);
            leaseId = conn.leaseId;
            break;
        }
    }
    if (!promise) {
        return;
    }
    const QPointer<QCWebSocketPool> safePool(this);
    promise->addResult(QCWebSocketAcquireResult::success(leaseId));
    promise->finish();
    if (safePool) {
        Q_EMIT connectionCreated(url);
    }
}

void QCWebSocketPool::onSocketError(const QString &error)
{
    if (d_ptr->lifecycleState == QCWebSocketPoolPrivate::LifecycleState::Destroying) {
        return;
    }
    const QPointer<QCWebSocket> socket(qobject_cast<QCWebSocket *>(sender()));
    const auto urlIt = socket ? d_ptr->socketToUrl.constFind(socket.data())
                              : d_ptr->socketToUrl.cend();
    if (urlIt == d_ptr->socketToUrl.cend()) {
        return;
    }
    const QUrl url      = *urlIt;
    bool wasEstablished = true;
    for (const auto &conn : d_ptr->pools.value(url)) {
        if (conn.identity == socket.data()) {
            wasEstablished = !conn.acquirePromise;
            break;
        }
    }
    const QPointer<QCWebSocketPool> safePool(this);
    failPendingAcquire(socket.data(), QCWebSocketAcquireResult::Status::ConnectionFailed, error);
    if (!safePool) {
        return;
    }
    if (socket) {
        socket->deleteLater();
    }
    if (wasEstablished) {
        Q_EMIT connectionClosed(url);
    }
}

void QCWebSocketPool::onSocketDisconnected()
{
    if (d_ptr->lifecycleState == QCWebSocketPoolPrivate::LifecycleState::Destroying) {
        return;
    }
    const QPointer<QCWebSocket> socket(qobject_cast<QCWebSocket *>(sender()));
    const auto urlIt = socket ? d_ptr->socketToUrl.constFind(socket.data())
                              : d_ptr->socketToUrl.cend();
    if (urlIt == d_ptr->socketToUrl.cend()) {
        return;
    }
    const QUrl url = *urlIt;
    const QPointer<QCWebSocketPool> safePool(this);
    failPendingAcquire(socket.data(),
                       QCWebSocketAcquireResult::Status::ConnectionFailed,
                       QStringLiteral("WebSocket disconnected during acquire"));
    if (!safePool) {
        return;
    }
    if (socket) {
        socket->deleteLater();
    }
    Q_EMIT connectionClosed(url);
}

} // namespace QCurl

#endif // QCURL_WEBSOCKET_SUPPORT
