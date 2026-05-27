#include "QCWebSocketPool.h"

#ifdef QCURL_WEBSOCKET_SUPPORT

#include "QCWebSocket.h"
#include "private/QCWebSocketPoolPrivate_p.h"

#include <QDateTime>
#include <QDebug>
#include <QMutexLocker>
#include <QTimer>

namespace QCurl {

// ==================
// 构造函数和析构函数
// ==================

QCWebSocketPool::QCWebSocketPool(const QCWebSocketPoolConfig &config, QObject *parent)
    : QObject(parent)
    , d_ptr(new QCWebSocketPoolPrivate)
{
    d_ptr->config = config;

    // 创建清理定时器（每 60 秒检查一次空闲连接）
    d_ptr->cleanupTimer = new QTimer(this);
    connect(d_ptr->cleanupTimer, &QTimer::timeout, this, &QCWebSocketPool::onCleanupTimer);
    d_ptr->cleanupTimer->start(60000); // 60 秒

    // 创建心跳定时器
    if (d_ptr->config.enableKeepAlive()) {
        d_ptr->keepAliveTimer = new QTimer(this);
        connect(d_ptr->keepAliveTimer, &QTimer::timeout, this, &QCWebSocketPool::onKeepAliveTimer);
        d_ptr->keepAliveTimer->start(d_ptr->config.keepAliveInterval() * 1000);
    }

    qDebug() << "QCWebSocketPool: 连接池已创建，配置:"
             << "maxPoolSize=" << d_ptr->config.maxPoolSize()
             << "maxIdleTime=" << d_ptr->config.maxIdleTime()
             << "s";
}

QCWebSocketPool::QCWebSocketPool(QObject *parent)
    : QCWebSocketPool(QCWebSocketPoolConfig(), parent)
{}

QCWebSocketPool::~QCWebSocketPool()
{
    clearPool(); // 清理所有连接
    qDebug() << "QCWebSocketPool: 连接池已销毁";
}

// ==================
// 核心 API 实现
// ==================

QCWebSocket *QCWebSocketPool::acquire(const QUrl &url)
{
    QMutexLocker locker(&d_ptr->mutex);

    // 1. 查找池中可用连接（优先复用）
    if (d_ptr->pools.contains(url)) {
        auto &pool = d_ptr->pools[url];
        for (auto &conn : pool) {
            if (!conn.inUse && conn.socket->state() == QCWebSocket::State::Connected) {
                conn.inUse        = true;
                conn.lastUsedTime = QDateTime::currentDateTime();
                conn.reuseCount++;

                d_ptr->hitCounts[url]++;
                qDebug() << "QCWebSocketPool: 复用连接" << url.toString()
                         << "复用次数:" << conn.reuseCount;

                emit connectionReused(url);
                return conn.socket;
            }
        }
    }

    // 2. 无可用连接，检查是否可以创建新连接
    if (!canCreateConnection(url)) {
        qWarning() << "QCWebSocketPool: 达到连接数限制，无法创建新连接" << url.toString();
        emit poolLimitReached(url);
        return nullptr;
    }

    // 3. 创建新连接
    QCWebSocket *socket = createNewConnection(url);
    if (!socket) {
        qWarning() << "QCWebSocketPool: 创建连接失败" << url.toString();
        return nullptr;
    }

    // 4. 添加到池中
    QCWebSocketPoolPrivate::PooledConnection conn;
    conn.socket       = socket;
    conn.lastUsedTime = QDateTime::currentDateTime();
    conn.createdTime  = QDateTime::currentDateTime();
    conn.inUse        = true;
    conn.reuseCount   = 0;

    d_ptr->pools[url].append(conn);
    d_ptr->socketToUrl[socket] = url;
    d_ptr->missCounts[url]++;

    qDebug() << "QCWebSocketPool: 创建新连接" << url.toString()
             << "当前池大小:" << d_ptr->pools[url].size();

    emit connectionCreated(url);
    return socket;
}

void QCWebSocketPool::release(QCWebSocket *socket)
{
    QMutexLocker locker(&d_ptr->mutex);

    if (!socket) {
        qWarning() << "QCWebSocketPool: 尝试释放空指针";
        return;
    }

    if (!d_ptr->socketToUrl.contains(socket)) {
        qWarning() << "QCWebSocketPool: 尝试释放未知连接";
        return;
    }

    QUrl url   = d_ptr->socketToUrl[socket];
    auto &pool = d_ptr->pools[url];

    for (auto &conn : pool) {
        if (conn.socket == socket) {
            if (!conn.inUse) {
                qWarning() << "QCWebSocketPool: 连接已处于空闲状态" << url.toString();
                return;
            }

            conn.inUse        = false;
            conn.lastUsedTime = QDateTime::currentDateTime();

            qDebug() << "QCWebSocketPool: 归还连接" << url.toString();
            return;
        }
    }
}

bool QCWebSocketPool::contains(const QUrl &url) const
{
    QMutexLocker locker(&d_ptr->mutex);
    return d_ptr->pools.contains(url) && !d_ptr->pools[url].isEmpty();
}

// ==================
// 池管理实现
// ==================

void QCWebSocketPool::clearPool(const QUrl &url)
{
    QMutexLocker locker(&d_ptr->mutex);

    if (url.isEmpty()) {
        // 清理所有池
        qDebug() << "QCWebSocketPool: 清理所有连接池";
        for (auto it = d_ptr->pools.begin(); it != d_ptr->pools.end(); ++it) {
            for (auto &conn : it.value()) {
                conn.socket->close();
                conn.socket->deleteLater();
            }
        }
        d_ptr->pools.clear();
        d_ptr->socketToUrl.clear();
        d_ptr->hitCounts.clear();
        d_ptr->missCounts.clear();
    } else {
        // 清理指定 URL 的池
        if (d_ptr->pools.contains(url)) {
            qDebug() << "QCWebSocketPool: 清理连接池" << url.toString();
            auto &pool = d_ptr->pools[url];
            for (auto &conn : pool) {
                d_ptr->socketToUrl.remove(conn.socket);
                conn.socket->close();
                conn.socket->deleteLater();
                emit connectionClosed(url);
            }
            d_ptr->pools.remove(url);
            d_ptr->hitCounts.remove(url);
            d_ptr->missCounts.remove(url);
        }
    }
}

void QCWebSocketPool::preWarm(const QUrl &url, int count)
{
    if (count <= 0) {
        return;
    }

    qDebug() << "QCWebSocketPool: 预热连接" << url.toString() << "数量:" << count;

    QList<QCWebSocket *> warmedSockets;
    warmedSockets.reserve(count);

    for (int i = 0; i < count; ++i) {
        auto *socket = acquire(url);
        if (socket) {
            warmedSockets.append(socket);
            continue;
        }

        qWarning() << "QCWebSocketPool: 预热失败，已创建" << warmedSockets.size() << "个连接";
        break;
    }

    for (QCWebSocket *socket : warmedSockets) {
        if (!socket) {
            break;
        }

        // 延后统一归还，避免在预热循环中立刻复用同一条空闲连接。
        release(socket);
    }
}

void QCWebSocketPool::setConfig(const QCWebSocketPoolConfig &config)
{
    QMutexLocker locker(&d_ptr->mutex);
    d_ptr->config = config;

    // 更新心跳定时器
    if (d_ptr->config.enableKeepAlive()) {
        if (!d_ptr->keepAliveTimer) {
            d_ptr->keepAliveTimer = new QTimer(this);
            connect(d_ptr->keepAliveTimer,
                    &QTimer::timeout,
                    this,
                    &QCWebSocketPool::onKeepAliveTimer);
        }
        d_ptr->keepAliveTimer->start(d_ptr->config.keepAliveInterval() * 1000);
    } else {
        if (d_ptr->keepAliveTimer) {
            d_ptr->keepAliveTimer->stop();
        }
    }

    qDebug() << "QCWebSocketPool: 配置已更新";
}

QCWebSocketPoolConfig QCWebSocketPool::config() const
{
    QMutexLocker locker(&d_ptr->mutex);
    return d_ptr->config;
}

// ==================
// 统计信息实现
// ==================

QCWebSocketPoolStats QCWebSocketPool::statistics(const QUrl &url) const
{
    QMutexLocker locker(&d_ptr->mutex);
    QCWebSocketPoolStats stats;

    if (url.isEmpty()) {
        // 全局统计
        for (const auto &pool : d_ptr->pools) {
            for (const auto &conn : pool) {
                stats.setTotalConnections(stats.totalConnections() + 1);
                if (conn.inUse) {
                    stats.setActiveConnections(stats.activeConnections() + 1);
                } else {
                    stats.setIdleConnections(stats.idleConnections() + 1);
                }
            }
        }

        for (int count : d_ptr->hitCounts) {
            stats.setHitCount(stats.hitCount() + count);
        }
        for (int count : d_ptr->missCounts) {
            stats.setMissCount(stats.missCount() + count);
        }
    } else {
        // 指定 URL 的统计
        if (d_ptr->pools.contains(url)) {
            const auto &pool = d_ptr->pools[url];
            for (const auto &conn : pool) {
                stats.setTotalConnections(stats.totalConnections() + 1);
                if (conn.inUse) {
                    stats.setActiveConnections(stats.activeConnections() + 1);
                } else {
                    stats.setIdleConnections(stats.idleConnections() + 1);
                }
            }
        }

        stats.setHitCount(d_ptr->hitCounts.value(url, 0));
        stats.setMissCount(d_ptr->missCounts.value(url, 0));
    }

    // 计算命中率
    const int totalRequests = stats.hitCount() + stats.missCount();
    if (totalRequests > 0) {
        stats.setHitRate(static_cast<double>(stats.hitCount()) / totalRequests * 100.0);
    }

    return stats;
}

} // namespace QCurl

#endif // QCURL_WEBSOCKET_SUPPORT
