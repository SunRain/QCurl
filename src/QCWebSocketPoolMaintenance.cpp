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
    const QPointer<QCWebSocketPool> safePool(this);
    QTimer::singleShot(kConnectionTimeout, this, [this, safeSocket, safePool]() {
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
                if (safePool && safeSocket) {
                    static_cast<void>(safeSocket->close());
                }
                if (safePool && safeSocket) {
                    safeSocket->deleteLater();
                }
                return;
            }
        }
    });
    return socket;
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
