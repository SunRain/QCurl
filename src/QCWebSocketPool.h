#ifndef QCWEBSOCKETPOOL_H
#define QCWEBSOCKETPOOL_H

#include "QCurlConfig.h"

#ifdef QCURL_WEBSOCKET_SUPPORT

#include "QCNetworkSslConfig.h"
#include <QObject>
#include <QScopedPointer>
#include <QUrl>

namespace QCurl {
class QCWebSocket;

/**
 * @brief 复用和管理 `QCWebSocket` 连接
 *
 * 连接池按 URL 分组维护连接，区分 in-use 与 idle 状态，并负责清理、
 * keepalive 和连接数限制。
 */
class QCURL_EXPORT QCWebSocketPool : public QObject
{
    Q_OBJECT

public:
    /**
     * @brief 连接池配置
     */
    struct Config
    {
        int maxPoolSize         = 10;   ///< 最大连接数（每个 URL）
        int maxIdleTime         = 300;  ///< 空闲超时（秒），超时连接将被清理
        int minIdleConnections  = 2;    ///< 最小空闲连接数（保留热连接）
        int maxTotalConnections = 50;   ///< 全局最大连接数（所有 URL 总和）
        bool enableKeepAlive    = true; ///< 启用心跳保活（发送 Ping 帧）
        int keepAliveInterval   = 30;   ///< 心跳间隔（秒）
        bool autoReconnect      = true; ///< 空闲连接断开时自动重连
        QCNetworkSslConfig sslConfig;   ///< WSS 的 SSL/TLS 配置（默认安全配置）
    };

    /**
     * @brief 统计信息
     */
    struct Stats
    {
        int totalConnections  = 0;   ///< 总连接数（活跃 + 空闲）
        int activeConnections = 0;   ///< 活跃连接数（正在使用）
        int idleConnections   = 0;   ///< 空闲连接数（可复用）
        int hitCount          = 0;   ///< 复用命中次数
        int missCount         = 0;   ///< 未命中次数（需创建新连接）
        double hitRate        = 0.0; ///< 命中率（百分比）
    };

    /**
     * @brief 构造函数
     * @param config 连接池配置
     * @param parent 父对象
     */
    explicit QCWebSocketPool(const Config &config, QObject *parent = nullptr);

    /**
     * @brief 构造函数（使用默认配置）
     * @param parent 父对象
     */
    explicit QCWebSocketPool(QObject *parent = nullptr);

    /**
     * @brief 析构函数
     *
     * 关闭所有连接并清理资源。
     */
    ~QCWebSocketPool();

    // ==================
    // 核心 API
    // ==================

    /**
     * @brief 获取连接
     * @param url WebSocket URL
     * @return 可用的 QCWebSocket 指针，失败返回 nullptr
     *
     * 优先从池中复用现有连接，如果无可用连接则创建新连接。
     * 如果达到连接数限制，返回 nullptr 并发射 poolLimitReached() 信号。
     *
     * @note 获取的连接必须通过 release() 归还，否则会导致资源泄漏
     */
    QCWebSocket *acquire(const QUrl &url);

    /**
     * @brief 归还连接
     * @param socket 要归还的 WebSocket 连接
     *
     * 将连接归还到池中以供复用。连接不会被关闭，而是标记为空闲状态。
     *
     * @note 归还后不应再使用该连接指针，除非再次 acquire()
     */
    void release(QCWebSocket *socket);

    /**
     * @brief 检查池中是否包含指定 URL 的连接
     * @param url WebSocket URL
     * @return true 如果池中有该 URL 的连接
     */
    bool contains(const QUrl &url) const;

    // ==================
    // 池管理
    // ==================

    /**
     * @brief 清理连接池
     * @param url WebSocket URL，如果为空则清理所有池
     *
     * 关闭并删除指定 URL 的所有连接（包括活跃和空闲连接）。
     * 如果 url 为空，清理所有池。
     */
    void clearPool(const QUrl &url = QUrl());

    /**
     * @brief 预热连接
     * @param url WebSocket URL
     * @param count 预建立的连接数
     *
     * 预先建立指定数量的连接，减少首次请求的延迟。
     * 适用于已知即将发起大量请求的场景。
     */
    void preWarm(const QUrl &url, int count);

    /**
     * @brief 设置配置
     * @param config 新的配置
     *
     * @note 修改配置不会影响已建立的连接，仅对后续操作生效
     */
    void setConfig(const Config &config);

    /**
     * @brief 获取当前配置
     * @return 配置副本
     */
    Config config() const;

    // ==================
    // 统计信息
    // ==================

    /**
     * @brief 获取统计信息
     * @param url WebSocket URL，如果为空则返回全局统计
     * @return 统计信息结构体
     */
    Stats statistics(const QUrl &url = QUrl()) const;

signals:
    /**
     * @brief 创建新连接时发射
     * @param url WebSocket URL
     */
    void connectionCreated(const QUrl &url);

    /**
     * @brief 复用连接时发射
     * @param url WebSocket URL
     */
    void connectionReused(const QUrl &url);

    /**
     * @brief 关闭连接时发射
     * @param url WebSocket URL
     */
    void connectionClosed(const QUrl &url);

    /**
     * @brief 达到连接数限制时发射
     * @param url WebSocket URL
     */
    void poolLimitReached(const QUrl &url);

private slots:
    void onCleanupTimer();
    void onKeepAliveTimer();
    void onSocketDisconnected();

private:
    struct Impl;
    QScopedPointer<Impl> m_impl;

    // 内部方法
    QCWebSocket *createNewConnection(const QUrl &url);
    void removeConnection(QCWebSocket *socket);
    void cleanupIdleConnections();
    void sendKeepAlive();
    bool canCreateConnection(const QUrl &url) const;
    int totalConnectionCount() const;

    Q_DISABLE_COPY(QCWebSocketPool)
};

} // namespace QCurl

#endif // QCURL_WEBSOCKET_SUPPORT
#endif // QCWEBSOCKETPOOL_H
