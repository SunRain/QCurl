#include "QCWebSocketPool.h"

#ifdef QCURL_WEBSOCKET_SUPPORT

#include "QCWebSocket.h"
#include "QCWebSocketReconnectPolicy.h"
#include "private/QCWebSocketPoolPrivate_p.h"

#include <QDateTime>
#include <QDebug>
#include <QEventLoop>
#include <QMutexLocker>
#include <QTimer>

#include <chrono>

namespace QCurl {

namespace {

constexpr std::chrono::seconds kConnectionWaitTimeout{5};

} // namespace

QCWebSocket *QCWebSocketPool::createNewConnection(const QUrl &url)
{
    QCWebSocketOptions options;

    // 应用 Preview 自动重连策略（如果启用）
    if (d_ptr->config.autoReconnect()) {
        options.setReconnectPolicy(QCWebSocketReconnectPolicy::standardReconnect());
    }

    // 应用 SSL/TLS 配置（用于 WSS + 自定义 CA 等）
    options.setSslConfig(d_ptr->config.sslConfig());

    QCWebSocket *socket = new QCWebSocket(url, options, this);

    // 连接断开信号
    connect(socket, &QCWebSocket::disconnected, this, &QCWebSocketPool::onSocketDisconnected);

    // 同步连接，等待时间由 pool 内部策略控制。
    socket->open();

    QEventLoop loop;
    QTimer timeout;
    timeout.setSingleShot(true);
    connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
    connect(socket, &QCWebSocket::connected, &loop, &QEventLoop::quit);
    connect(socket, &QCWebSocket::errorOccurred, &loop, &QEventLoop::quit);

    timeout.start(std::chrono::duration_cast<std::chrono::milliseconds>(kConnectionWaitTimeout));
    loop.exec();

    if (socket->state() != QCWebSocket::State::Connected) {
        qWarning() << "QCWebSocketPool: 连接失败:" << socket->errorString();
        socket->deleteLater();
        return nullptr;
    }

    return socket;
}

void QCWebSocketPool::removeConnection(QCWebSocket *socket)
{
    if (!d_ptr->socketToUrl.contains(socket)) {
        return;
    }

    QUrl url = d_ptr->socketToUrl[socket];
    d_ptr->socketToUrl.remove(socket);

    if (d_ptr->pools.contains(url)) {
        auto &pool = d_ptr->pools[url];
        for (int i = 0; i < pool.size(); ++i) {
            if (pool[i].socket == socket) {
                pool.removeAt(i);
                break;
            }
        }

        if (pool.isEmpty()) {
            d_ptr->pools.remove(url);
        }
    }
}

void QCWebSocketPool::cleanupIdleConnections()
{
    QDateTime now = QDateTime::currentDateTime();

    for (auto it = d_ptr->pools.begin(); it != d_ptr->pools.end();) {
        QUrl url   = it.key();
        auto &pool = it.value();

        // 计算空闲连接数
        int idleCount = 0;
        for (const auto &conn : pool) {
            if (!conn.inUse) {
                idleCount++;
            }
        }

        // 移除超时的空闲连接（保留最小数量）
        for (int i = pool.size() - 1; i >= 0; --i) {
            auto &conn = pool[i];

            if (!conn.inUse && idleCount > d_ptr->config.minIdleConnections()) {
                qint64 idleSeconds = conn.lastUsedTime.secsTo(now);

                if (idleSeconds > d_ptr->config.maxIdleTime()) {
                    qDebug() << "QCWebSocketPool: 清理空闲连接" << url.toString()
                             << "空闲时间:" << idleSeconds << "秒";

                    d_ptr->socketToUrl.remove(conn.socket);
                    conn.socket->close();
                    conn.socket->deleteLater();
                    pool.removeAt(i);
                    idleCount--;

                    emit connectionClosed(url);
                }
            }
        }

        // 如果该 URL 的池为空，移除整个池
        if (pool.isEmpty()) {
            it = d_ptr->pools.erase(it);
        } else {
            ++it;
        }
    }
}

void QCWebSocketPool::sendKeepAlive()
{
    for (auto &pool : d_ptr->pools) {
        for (auto &conn : pool) {
            if (!conn.inUse && conn.socket->state() == QCWebSocket::State::Connected) {
                // 发送空 Ping 消息（使用 sendTextMessage 代替，因为 sendPing 不在 API 中）
                // libcurl WebSocket API 会自动处理 Ping/Pong 帧
                // 这里发送一个空的文本消息作为心跳检测
                conn.socket->sendTextMessage(QString());
            }
        }
    }
}

bool QCWebSocketPool::canCreateConnection(const QUrl &url) const
{
    // 检查该 URL 的池大小
    if (d_ptr->pools.contains(url)) {
        if (d_ptr->pools[url].size() >= d_ptr->config.maxPoolSize()) {
            return false;
        }
    }

    // 检查全局连接数
    if (totalConnectionCount() >= d_ptr->config.maxTotalConnections()) {
        return false;
    }

    return true;
}

int QCWebSocketPool::totalConnectionCount() const
{
    int count = 0;
    for (const auto &pool : d_ptr->pools) {
        count += pool.size();
    }
    return count;
}

// ==================
// 槽函数实现
// ==================

void QCWebSocketPool::onCleanupTimer()
{
    QMutexLocker locker(&d_ptr->mutex);
    cleanupIdleConnections();
}

void QCWebSocketPool::onKeepAliveTimer()
{
    QMutexLocker locker(&d_ptr->mutex);
    sendKeepAlive();
}

void QCWebSocketPool::onSocketDisconnected()
{
    QCWebSocket *socket = qobject_cast<QCWebSocket *>(sender());
    if (!socket) {
        return;
    }

    QMutexLocker locker(&d_ptr->mutex);

    if (!d_ptr->socketToUrl.contains(socket)) {
        return;
    }

    QUrl url = d_ptr->socketToUrl[socket];
    qDebug() << "QCWebSocketPool: 连接断开" << url.toString();

    // 从池中移除断开的连接
    removeConnection(socket);
    socket->deleteLater();

    emit connectionClosed(url);
}

} // namespace QCurl

#endif // QCURL_WEBSOCKET_SUPPORT
