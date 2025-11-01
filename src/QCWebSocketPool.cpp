#include "QCWebSocketPool.h"

#ifdef QCURL_WEBSOCKET_SUPPORT

#include "QCWebSocketReconnectPolicy.h"
#include <QDebug>
#include <QEventLoop>

QT_BEGIN_NAMESPACE

namespace QCurl {

// ============================================================================
// 构造函数和析构函数
// ============================================================================

QCWebSocketPool::QCWebSocketPool(const Config &config, QObject *parent)
    : QObject(parent), m_config(config)
{
    // 创建清理定时器（每 60 秒检查一次空闲连接）
    m_cleanupTimer = new QTimer(this);
    connect(m_cleanupTimer, &QTimer::timeout, this, &QCWebSocketPool::onCleanupTimer);
    m_cleanupTimer->start(60000);  // 60 秒

    // 创建心跳定时器
    if (m_config.enableKeepAlive) {
        m_keepAliveTimer = new QTimer(this);
        connect(m_keepAliveTimer, &QTimer::timeout, this, &QCWebSocketPool::onKeepAliveTimer);
        m_keepAliveTimer->start(m_config.keepAliveInterval * 1000);
    }

    qDebug() << "QCWebSocketPool: 连接池已创建，配置:"
             << "maxPoolSize=" << m_config.maxPoolSize
             << "maxIdleTime=" << m_config.maxIdleTime << "s";
}

QCWebSocketPool::QCWebSocketPool(QObject *parent)
    : QCWebSocketPool(Config(), parent)
{
}

QCWebSocketPool::~QCWebSocketPool()
{
    clearPool();  // 清理所有连接
    qDebug() << "QCWebSocketPool: 连接池已销毁";
}

// ============================================================================
// 核心 API 实现
// ============================================================================

QCWebSocket* QCWebSocketPool::acquire(const QUrl &url)
{
    QMutexLocker locker(&m_mutex);

    // 1. 查找池中可用连接（优先复用）
    if (m_pools.contains(url)) {
        auto &pool = m_pools[url];
        for (auto &conn : pool) {
            if (!conn.inUse && conn.socket->state() == QCWebSocket::State::Connected) {
                conn.inUse = true;
                conn.lastUsedTime = QDateTime::currentDateTime();
                conn.reuseCount++;
                
                m_hitCounts[url]++;
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
    PooledConnection conn;
    conn.socket = socket;
    conn.lastUsedTime = QDateTime::currentDateTime();
    conn.createdTime = QDateTime::currentDateTime();
    conn.inUse = true;
    conn.reuseCount = 0;

    m_pools[url].append(conn);
    m_socketToUrl[socket] = url;
    m_missCounts[url]++;

    qDebug() << "QCWebSocketPool: 创建新连接" << url.toString()
             << "当前池大小:" << m_pools[url].size();

    emit connectionCreated(url);
    return socket;
}

void QCWebSocketPool::release(QCWebSocket *socket)
{
    QMutexLocker locker(&m_mutex);

    if (!socket) {
        qWarning() << "QCWebSocketPool: 尝试释放空指针";
        return;
    }

    if (!m_socketToUrl.contains(socket)) {
        qWarning() << "QCWebSocketPool: 尝试释放未知连接";
        return;
    }

    QUrl url = m_socketToUrl[socket];
    auto &pool = m_pools[url];

    for (auto &conn : pool) {
        if (conn.socket == socket) {
            if (!conn.inUse) {
                qWarning() << "QCWebSocketPool: 连接已处于空闲状态" << url.toString();
                return;
            }
            
            conn.inUse = false;
            conn.lastUsedTime = QDateTime::currentDateTime();
            
            qDebug() << "QCWebSocketPool: 归还连接" << url.toString();
            return;
        }
    }
}

bool QCWebSocketPool::contains(const QUrl &url) const
{
    QMutexLocker locker(&m_mutex);
    return m_pools.contains(url) && !m_pools[url].isEmpty();
}

// ============================================================================
// 池管理实现
// ============================================================================

void QCWebSocketPool::clearPool(const QUrl &url)
{
    QMutexLocker locker(&m_mutex);

    if (url.isEmpty()) {
        // 清理所有池
        qDebug() << "QCWebSocketPool: 清理所有连接池";
        for (auto it = m_pools.begin(); it != m_pools.end(); ++it) {
            for (auto &conn : it.value()) {
                conn.socket->close();
                conn.socket->deleteLater();
            }
        }
        m_pools.clear();
        m_socketToUrl.clear();
        m_hitCounts.clear();
        m_missCounts.clear();
    } else {
        // 清理指定 URL 的池
        if (m_pools.contains(url)) {
            qDebug() << "QCWebSocketPool: 清理连接池" << url.toString();
            auto &pool = m_pools[url];
            for (auto &conn : pool) {
                m_socketToUrl.remove(conn.socket);
                conn.socket->close();
                conn.socket->deleteLater();
                emit connectionClosed(url);
            }
            m_pools.remove(url);
            m_hitCounts.remove(url);
            m_missCounts.remove(url);
        }
    }
}

void QCWebSocketPool::preWarm(const QUrl &url, int count)
{
    if (count <= 0) {
        return;
    }

    qDebug() << "QCWebSocketPool: 预热连接" << url.toString() << "数量:" << count;

    for (int i = 0; i < count; ++i) {
        auto *socket = acquire(url);
        if (socket) {
            release(socket);  // 立即归还（变为空闲状态）
        } else {
            qWarning() << "QCWebSocketPool: 预热失败，已创建" << i << "个连接";
            break;
        }
    }
}

void QCWebSocketPool::setConfig(const Config &config)
{
    QMutexLocker locker(&m_mutex);
    m_config = config;
    
    // 更新心跳定时器
    if (m_config.enableKeepAlive) {
        if (!m_keepAliveTimer) {
            m_keepAliveTimer = new QTimer(this);
            connect(m_keepAliveTimer, &QTimer::timeout, this, &QCWebSocketPool::onKeepAliveTimer);
        }
        m_keepAliveTimer->start(m_config.keepAliveInterval * 1000);
    } else {
        if (m_keepAliveTimer) {
            m_keepAliveTimer->stop();
        }
    }
    
    qDebug() << "QCWebSocketPool: 配置已更新";
}

QCWebSocketPool::Config QCWebSocketPool::config() const
{
    QMutexLocker locker(&m_mutex);
    return m_config;
}

// ============================================================================
// 统计信息实现
// ============================================================================

QCWebSocketPool::Stats QCWebSocketPool::statistics(const QUrl &url) const
{
    QMutexLocker locker(&m_mutex);
    Stats stats;

    if (url.isEmpty()) {
        // 全局统计
        for (const auto &pool : m_pools) {
            for (const auto &conn : pool) {
                stats.totalConnections++;
                if (conn.inUse) {
                    stats.activeConnections++;
                } else {
                    stats.idleConnections++;
                }
            }
        }
        
        for (int count : m_hitCounts) {
            stats.hitCount += count;
        }
        for (int count : m_missCounts) {
            stats.missCount += count;
        }
    } else {
        // 指定 URL 的统计
        if (m_pools.contains(url)) {
            const auto &pool = m_pools[url];
            for (const auto &conn : pool) {
                stats.totalConnections++;
                if (conn.inUse) {
                    stats.activeConnections++;
                } else {
                    stats.idleConnections++;
                }
            }
        }
        
        stats.hitCount = m_hitCounts.value(url, 0);
        stats.missCount = m_missCounts.value(url, 0);
    }

    // 计算命中率
    int totalRequests = stats.hitCount + stats.missCount;
    if (totalRequests > 0) {
        stats.hitRate = (double)stats.hitCount / totalRequests * 100.0;
    }

    return stats;
}

// ============================================================================
// 私有方法实现
// ============================================================================

QCWebSocket* QCWebSocketPool::createNewConnection(const QUrl &url)
{
    QCWebSocket *socket = new QCWebSocket(url, this);

    // 应用 v2.4.0 自动重连策略（如果启用）
    if (m_config.autoReconnect) {
        QCWebSocketReconnectPolicy policy;
        policy.maxRetries = 3;
        policy.initialDelay = std::chrono::milliseconds(1000);
        policy.backoffMultiplier = 2.0;
        socket->setReconnectPolicy(policy);
    }

    // 连接断开信号
    connect(socket, &QCWebSocket::disconnected, this, &QCWebSocketPool::onSocketDisconnected);

    // 同步连接（阻塞等待，最多 5 秒）
    socket->open();
    
    QEventLoop loop;
    QTimer timeout;
    timeout.setSingleShot(true);
    connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
    connect(socket, &QCWebSocket::connected, &loop, &QEventLoop::quit);
    connect(socket, &QCWebSocket::errorOccurred, &loop, &QEventLoop::quit);
    
    timeout.start(5000);
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
    if (!m_socketToUrl.contains(socket)) {
        return;
    }

    QUrl url = m_socketToUrl[socket];
    m_socketToUrl.remove(socket);

    if (m_pools.contains(url)) {
        auto &pool = m_pools[url];
        for (int i = 0; i < pool.size(); ++i) {
            if (pool[i].socket == socket) {
                pool.removeAt(i);
                break;
            }
        }

        if (pool.isEmpty()) {
            m_pools.remove(url);
        }
    }
}

void QCWebSocketPool::cleanupIdleConnections()
{
    QDateTime now = QDateTime::currentDateTime();

    for (auto it = m_pools.begin(); it != m_pools.end();) {
        QUrl url = it.key();
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
            
            if (!conn.inUse && idleCount > m_config.minIdleConnections) {
                qint64 idleSeconds = conn.lastUsedTime.secsTo(now);
                
                if (idleSeconds > m_config.maxIdleTime) {
                    qDebug() << "QCWebSocketPool: 清理空闲连接" << url.toString()
                             << "空闲时间:" << idleSeconds << "秒";
                    
                    m_socketToUrl.remove(conn.socket);
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
            it = m_pools.erase(it);
        } else {
            ++it;
        }
    }
}

void QCWebSocketPool::sendKeepAlive()
{
    for (auto &pool : m_pools) {
        for (auto &conn : pool) {
            if (!conn.inUse && conn.socket->state() == QCWebSocket::State::Connected) {
                // 发送空 Ping 消息（使用 sendTextMessage 代替，因为 sendPing 不在 API 中）
                // libcurl WebSocket API 会自动处理 Ping/Pong 帧
                // 这里发送一个空的文本消息作为心跳检测
                conn.socket->sendTextMessage("");
            }
        }
    }
}

bool QCWebSocketPool::canCreateConnection(const QUrl &url) const
{
    // 检查该 URL 的池大小
    if (m_pools.contains(url)) {
        if (m_pools[url].size() >= m_config.maxPoolSize) {
            return false;
        }
    }

    // 检查全局连接数
    if (totalConnectionCount() >= m_config.maxTotalConnections) {
        return false;
    }

    return true;
}

int QCWebSocketPool::totalConnectionCount() const
{
    int count = 0;
    for (const auto &pool : m_pools) {
        count += pool.size();
    }
    return count;
}

// ============================================================================
// 槽函数实现
// ============================================================================

void QCWebSocketPool::onCleanupTimer()
{
    QMutexLocker locker(&m_mutex);
    cleanupIdleConnections();
}

void QCWebSocketPool::onKeepAliveTimer()
{
    QMutexLocker locker(&m_mutex);
    sendKeepAlive();
}

void QCWebSocketPool::onSocketDisconnected()
{
    QCWebSocket *socket = qobject_cast<QCWebSocket*>(sender());
    if (!socket) {
        return;
    }

    QMutexLocker locker(&m_mutex);
    
    if (!m_socketToUrl.contains(socket)) {
        return;
    }

    QUrl url = m_socketToUrl[socket];
    qDebug() << "QCWebSocketPool: 连接断开" << url.toString();

    // 从池中移除断开的连接
    removeConnection(socket);
    socket->deleteLater();
    
    emit connectionClosed(url);
}

} // namespace QCurl

QT_END_NAMESPACE

#endif // QCURL_WEBSOCKET_SUPPORT
