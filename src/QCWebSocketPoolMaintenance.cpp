#include "QCWebSocketPool.h"

#ifdef QCURL_WEBSOCKET_SUPPORT

#include "QCWebSocket.h"
#include "QCWebSocketReconnectPolicy.h"
#include "private/QCWebSocketPoolPrivate_p.h"

#include <QDateTime>
#include <QPointer>
#include <QRandomGenerator>
#include <QThread>
#include <QTimer>

#include <chrono>

namespace QCurl {

namespace {

constexpr std::chrono::seconds kConnectionTimeout{5};

QByteArray keepAliveNonce()
{
    return QByteArray::number(QRandomGenerator::global()->generate64(), 16).rightJustified(16, '0');
}

} // namespace
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
    operation->promise->addResult(finalResult);
    operation->promise->finish();
    preWarmOperations.removeAll(operation);
}

QCWebSocket *QCWebSocketPool::createNewConnection(const QUrl &url)
{
    if (d_ptr->lifecycleState == QCWebSocketPoolPrivate::LifecycleState::Destroying) {
        return nullptr;
    }
    QCWebSocketOptions options;
    if (d_ptr->config.autoReconnect()) {
        options.setReconnectPolicy(QCWebSocketReconnectPolicy::standardReconnect());
    }
    options.setSslConfig(d_ptr->config.sslConfig());

    auto *socket = new QCWebSocket(url, options, this);
    connect(socket, &QCWebSocket::connected, this, &QCWebSocketPool::onSocketConnected);
    connect(socket, &QCWebSocket::disconnected, this, &QCWebSocketPool::onSocketDisconnected);
    connect(socket, &QCWebSocket::errorOccurred, this, &QCWebSocketPool::onSocketError);
    connect(socket, &QCWebSocket::pongReceived, this, &QCWebSocketPool::onSocketPong);
    connect(socket, &QObject::destroyed, this, &QCWebSocketPool::onSocketDestroyed);
    const QPointer<QCWebSocket> safeSocket(socket);
    QTimer::singleShot(kConnectionTimeout, this, [this, safeSocket]() {
        if (!safeSocket) {
            return;
        }
        const auto it = d_ptr->socketToUrl.constFind(safeSocket.data());
        if (it == d_ptr->socketToUrl.cend()) {
            return;
        }
        const auto &pool = d_ptr->pools.value(*it);
        for (const auto &conn : pool) {
            if (conn.identity == safeSocket.data() && conn.acquirePromise) {
                failPendingAcquire(safeSocket.data(),
                                   QCWebSocketAcquireResult::Status::ConnectionFailed,
                                   QStringLiteral("WebSocket connection timed out"));
                if (safeSocket) {
                    static_cast<void>(safeSocket->close());
                    safeSocket->deleteLater();
                }
                return;
            }
        }
    });
    return socket;
}

/**
 * @brief 清理外部销毁的 socket 记录并完成等待中的 future。
 * @param object 被 QObject 生命周期销毁通知标记的对象。
 */
void QCWebSocketPool::onSocketDestroyed(QObject *object)
{
    if (d_ptr->lifecycleState == QCWebSocketPoolPrivate::LifecycleState::Destroying
        || QThread::currentThread() != thread() || !object) {
        return;
    }

    const auto urlIt = d_ptr->socketToUrl.constFind(object);
    if (urlIt == d_ptr->socketToUrl.cend()) {
        return;
    }
    const QUrl url = *urlIt;
    d_ptr->socketToUrl.remove(object);

    auto poolIt = d_ptr->pools.find(url);
    if (poolIt == d_ptr->pools.end()) {
        return;
    }
    auto &pool = poolIt.value();
    for (int i = 0; i < pool.size(); ++i) {
        if (pool[i].identity != object) {
            continue;
        }
        d_ptr->invalidateLease(pool[i].leaseId);
        if (pool[i].acquirePromise) {
            pool[i].acquirePromise->addResult(QCWebSocketAcquireResult::failure(
                QCWebSocketAcquireResult::Status::ConnectionFailed,
                QStringLiteral("WebSocket destroyed before acquire completed")));
            pool[i].acquirePromise->finish();
        }
        pool.removeAt(i);
        break;
    }
    if (pool.isEmpty()) {
        d_ptr->pools.erase(poolIt);
    }
    Q_EMIT connectionClosed(url);
}

void QCWebSocketPool::failPendingAcquire(QCWebSocket *socket,
                                         QCWebSocketAcquireResult::Status status,
                                         const QString &error)
{
    if (d_ptr->lifecycleState == QCWebSocketPoolPrivate::LifecycleState::Destroying) {
        return;
    }
    const auto urlIt = d_ptr->socketToUrl.constFind(socket);
    if (urlIt == d_ptr->socketToUrl.cend()) {
        return;
    }
    const QUrl url = *urlIt;
    auto &pool     = d_ptr->pools[url];
    for (int i = 0; i < pool.size(); ++i) {
        if (pool[i].identity == socket) {
            d_ptr->invalidateLease(pool[i].leaseId);
            if (pool[i].acquirePromise) {
                pool[i].acquirePromise->addResult(QCWebSocketAcquireResult::failure(status, error));
                pool[i].acquirePromise->finish();
            }
            pool.removeAt(i);
            break;
        }
    }
    d_ptr->socketToUrl.remove(socket);
    if (pool.isEmpty()) {
        d_ptr->pools.remove(url);
    }
}

void QCWebSocketPool::removeConnection(QCWebSocket *socket)
{
    if (d_ptr->lifecycleState == QCWebSocketPoolPrivate::LifecycleState::Destroying) {
        return;
    }
    const auto urlIt = d_ptr->socketToUrl.constFind(socket);
    if (urlIt == d_ptr->socketToUrl.cend()) {
        return;
    }
    const QUrl url = *urlIt;
    d_ptr->socketToUrl.remove(socket);
    auto poolIt = d_ptr->pools.find(url);
    if (poolIt == d_ptr->pools.end()) {
        return;
    }
    auto &pool = poolIt.value();
    for (int i = 0; i < pool.size(); ++i) {
        if (pool[i].identity == socket) {
            d_ptr->invalidateLease(pool[i].leaseId);
            pool.removeAt(i);
            break;
        }
    }
    if (pool.isEmpty()) {
        d_ptr->pools.erase(poolIt);
    }
}

void QCWebSocketPool::cleanupIdleConnections()
{
    if (d_ptr->lifecycleState == QCWebSocketPoolPrivate::LifecycleState::Destroying) {
        return;
    }
    const QDateTime now = QDateTime::currentDateTime();
    QList<QPair<QUrl, QPointer<QCWebSocket>>> expired;
    for (auto poolIt = d_ptr->pools.begin(); poolIt != d_ptr->pools.end(); ++poolIt) {
        int idleCount = 0;
        for (const auto &conn : poolIt.value()) {
            idleCount += conn.socket && !conn.inUse ? 1 : 0;
        }
        for (const auto &conn : poolIt.value()) {
            if (conn.socket && !conn.inUse && idleCount > d_ptr->config.minIdleConnections()
                && conn.lastUsedTime.secsTo(now) > d_ptr->config.maxIdleTime()) {
                expired.append({poolIt.key(), conn.socket});
                idleCount--;
            }
        }
    }
    for (const auto &[url, socket] : expired) {
        if (!socket) {
            continue;
        }
        removeConnection(socket.data());
        static_cast<void>(socket->close());
        socket->deleteLater();
        Q_EMIT connectionClosed(url);
    }
}

void QCWebSocketPool::sendKeepAlive()
{
    if (d_ptr->lifecycleState == QCWebSocketPoolPrivate::LifecycleState::Destroying) {
        return;
    }
    const QDateTime now = QDateTime::currentDateTime();
    QList<QPair<QUrl, QPointer<QCWebSocket>>> expired;
    for (auto poolIt = d_ptr->pools.begin(); poolIt != d_ptr->pools.end(); ++poolIt) {
        for (auto &conn : poolIt.value()) {
            if (!conn.socket || conn.inUse
                || conn.socket->state() != QCWebSocket::State::Connected) {
                continue;
            }
            if (!conn.keepAliveNonce.isEmpty()) {
                if (conn.keepAliveDeadline <= now) {
                    expired.append({poolIt.key(), conn.socket});
                }
                continue;
            }
            conn.keepAliveNonce    = keepAliveNonce();
            conn.keepAliveDeadline = now.addSecs(qMax(1, d_ptr->config.keepAliveInterval()));
            static_cast<void>(conn.socket->ping(conn.keepAliveNonce));
        }
    }
    for (const auto &[url, socket] : expired) {
        if (!socket) {
            continue;
        }
        removeConnection(socket.data());
        static_cast<void>(socket->close());
        socket->deleteLater();
        Q_EMIT connectionClosed(url);
    }
}

bool QCWebSocketPool::canCreateConnection(const QUrl &url) const
{
    if (d_ptr->lifecycleState == QCWebSocketPoolPrivate::LifecycleState::Destroying) {
        return false;
    }
    return d_ptr->pools.value(url).size() < d_ptr->config.maxPoolSize()
           && totalConnectionCount() < d_ptr->config.maxTotalConnections();
}

void QCWebSocketPool::onCleanupTimer()
{
    if (d_ptr->lifecycleState == QCWebSocketPoolPrivate::LifecycleState::Destroying) {
        return;
    }
    cleanupIdleConnections();
}

void QCWebSocketPool::onKeepAliveTimer()
{
    if (d_ptr->lifecycleState == QCWebSocketPoolPrivate::LifecycleState::Destroying) {
        return;
    }
    sendKeepAlive();
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
    auto &pool     = d_ptr->pools[url];
    for (auto &conn : pool) {
        if (conn.socket.data() == socket && conn.acquirePromise) {
            auto promise = std::move(conn.acquirePromise);
            promise->addResult(QCWebSocketAcquireResult::success(conn.leaseId));
            promise->finish();
            Q_EMIT connectionCreated(url);
            return;
        }
    }
}

void QCWebSocketPool::onSocketError(const QString &error)
{
    if (d_ptr->lifecycleState == QCWebSocketPoolPrivate::LifecycleState::Destroying) {
        return;
    }
    auto *socket = qobject_cast<QCWebSocket *>(sender());
    if (!socket) {
        return;
    }
    const auto urlIt = d_ptr->socketToUrl.constFind(socket);
    if (urlIt == d_ptr->socketToUrl.cend()) {
        return;
    }
    const QUrl url      = *urlIt;
    bool wasEstablished = true;
    for (const auto &conn : d_ptr->pools.value(url)) {
        if (conn.identity == socket) {
            wasEstablished = !conn.acquirePromise;
            break;
        }
    }
    failPendingAcquire(socket, QCWebSocketAcquireResult::Status::ConnectionFailed, error);
    socket->deleteLater();
    if (wasEstablished) {
        Q_EMIT connectionClosed(url);
    }
}

void QCWebSocketPool::onSocketDisconnected()
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
    failPendingAcquire(socket,
                       QCWebSocketAcquireResult::Status::ConnectionFailed,
                       QStringLiteral("WebSocket disconnected during acquire"));
    removeConnection(socket);
    socket->deleteLater();
    Q_EMIT connectionClosed(url);
}

void QCWebSocketPool::onSocketPong(const QByteArray &payload)
{
    if (d_ptr->lifecycleState == QCWebSocketPoolPrivate::LifecycleState::Destroying) {
        return;
    }
    auto *socket     = qobject_cast<QCWebSocket *>(sender());
    const auto urlIt = socket ? d_ptr->socketToUrl.constFind(socket) : d_ptr->socketToUrl.cend();
    if (urlIt == d_ptr->socketToUrl.cend()) {
        return;
    }
    auto &pool = d_ptr->pools[*urlIt];
    for (auto &conn : pool) {
        if (conn.socket.data() == socket && !conn.keepAliveNonce.isEmpty()
            && conn.keepAliveNonce == payload) {
            conn.keepAliveNonce.clear();
            conn.keepAliveDeadline = {};
            return;
        }
    }
}

} // namespace QCurl

#endif // QCURL_WEBSOCKET_SUPPORT
