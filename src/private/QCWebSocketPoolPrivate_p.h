/**
 * @file
 * @brief 声明 QCWebSocketPool 的私有运行时状态。
 */

#ifndef QCWEBSOCKETPOOLPRIVATE_P_H
#define QCWEBSOCKETPOOLPRIVATE_P_H

#include "QCWebSocketPool.h"

#include <QDateTime>
#include <QHash>
#include <QMap>
#include <QMutex>

class QTimer;

namespace QCurl {

/// 保存连接池运行时状态，避免在 public header 暴露容器、锁和定时器布局。
class QCWebSocketPoolPrivate
{
public:
    /// 连接池内的单条 socket 记录；socket 由 QCWebSocketPool 通过 parent/deleteLater 管理。
    struct PooledConnection
    {
        QCWebSocket *socket = nullptr;
        QDateTime lastUsedTime;
        QDateTime createdTime;
        bool inUse = false;
        int reuseCount = 0;
    };

    /// URL 到连接列表的主索引；由 mutex 保护。
    QMap<QUrl, QList<PooledConnection>> pools;
    /// socket 到 URL 的反向索引；用于 release 和断线清理。
    QHash<QCWebSocket *, QUrl> socketToUrl;
    /// 当前配置快照；setConfig() 后只影响后续操作。
    QCWebSocketPoolConfig config;
    /// 由 QCWebSocketPool parent 管理的空闲清理定时器。
    QTimer *cleanupTimer = nullptr;
    /// 由 QCWebSocketPool parent 管理的保活定时器。
    QTimer *keepAliveTimer = nullptr;
    /// 保护连接池容器和统计计数；不保护 socket 内部状态。
    mutable QMutex mutex;
    /// 每个 URL 的复用命中次数。
    QHash<QUrl, int> hitCounts;
    /// 每个 URL 的新建连接次数。
    QHash<QUrl, int> missCounts;
};

} // namespace QCurl

#endif // QCWEBSOCKETPOOLPRIVATE_P_H
