/**
 * @file
 * @brief 声明 WebSocket 连接池接口。
 */

#ifndef QCWEBSOCKETPOOL_H
#define QCWEBSOCKETPOOL_H

#include "QCGlobal.h"

#ifdef QCURL_WEBSOCKET_SUPPORT

#include "QCNetworkSslConfig.h"
#include "QCWebSocketPoolResults.h"

#include <QFuture>
#include <QObject>
#include <QScopedPointer>
#include <QSharedDataPointer>
#include <QUrl>

namespace QCurl {
class QCWebSocket;
class QCWebSocketPoolConfigData;
class QCWebSocketPoolPrivate;
class QCWebSocketPoolStatsData;

/**
 * @brief WebSocket 连接池配置。
 *
 * 该类型使用 accessor-only shared-data 形式保持 ABI 友好。
 * 配置变更只影响后续连接池操作，不重写已建立连接的状态。
 */
class QCURL_OTHER_EXTRAS_EXPORT QCWebSocketPoolConfig
{
public:
    QCWebSocketPoolConfig();
    QCWebSocketPoolConfig(const QCWebSocketPoolConfig &other);
    QCWebSocketPoolConfig(QCWebSocketPoolConfig &&other) noexcept;
    ~QCWebSocketPoolConfig();

    QCWebSocketPoolConfig &operator=(const QCWebSocketPoolConfig &other);
    QCWebSocketPoolConfig &operator=(QCWebSocketPoolConfig &&other) noexcept;

    /// 每个 URL 的最大连接数。
    [[nodiscard]] int maxPoolSize() const noexcept;
    void setMaxPoolSize(int value) noexcept;

    /// 空闲超时秒数，超时连接会被清理。
    [[nodiscard]] int maxIdleTime() const noexcept;
    void setMaxIdleTime(int seconds) noexcept;

    /// 每个 URL 保留的最小空闲连接数。
    [[nodiscard]] int minIdleConnections() const noexcept;
    void setMinIdleConnections(int value) noexcept;

    /// 所有 URL 的全局最大连接数。
    [[nodiscard]] int maxTotalConnections() const noexcept;
    void setMaxTotalConnections(int value) noexcept;

    /// 是否启用 Ping/保活定时器。
    [[nodiscard]] bool enableKeepAlive() const noexcept;
    void setEnableKeepAlive(bool enabled) noexcept;

    /// 保活间隔秒数。
    [[nodiscard]] int keepAliveInterval() const noexcept;
    void setKeepAliveInterval(int seconds) noexcept;

    /// 空闲连接断开时是否启用自动重连策略。
    [[nodiscard]] bool autoReconnect() const noexcept;
    void setAutoReconnect(bool enabled) noexcept;

    /// WSS 连接使用的 SSL/TLS 配置。
    [[nodiscard]] QCNetworkSslConfig sslConfig() const;
    void setSslConfig(const QCNetworkSslConfig &config);

private:
    QSharedDataPointer<QCWebSocketPoolConfigData> d;
};

/**
 * @brief WebSocket 连接池统计快照。
 *
 * 该类型使用 accessor-only shared-data 形式保持 ABI 友好。
 * `hitRate()` 使用百分比值，例如 50.0 表示 50%。
 */
class QCURL_OTHER_EXTRAS_EXPORT QCWebSocketPoolStats
{
public:
    QCWebSocketPoolStats();
    QCWebSocketPoolStats(const QCWebSocketPoolStats &other);
    QCWebSocketPoolStats(QCWebSocketPoolStats &&other) noexcept;
    ~QCWebSocketPoolStats();

    QCWebSocketPoolStats &operator=(const QCWebSocketPoolStats &other);
    QCWebSocketPoolStats &operator=(QCWebSocketPoolStats &&other) noexcept;

    [[nodiscard]] int totalConnections() const noexcept;
    void setTotalConnections(int value) noexcept;

    [[nodiscard]] int activeConnections() const noexcept;
    void setActiveConnections(int value) noexcept;

    [[nodiscard]] int idleConnections() const noexcept;
    void setIdleConnections(int value) noexcept;

    [[nodiscard]] int hitCount() const noexcept;
    void setHitCount(int value) noexcept;

    [[nodiscard]] int missCount() const noexcept;
    void setMissCount(int value) noexcept;

    [[nodiscard]] double hitRate() const noexcept;
    void setHitRate(double value) noexcept;

private:
    QSharedDataPointer<QCWebSocketPoolStatsData> d;
};

/**
 * @brief 复用和管理 `QCWebSocket` 连接
 *
 * 连接池按 URL 分组维护连接，区分 in-use 与 idle 状态，并负责清理、
 * keepalive 和连接数限制。对象及其全部状态只能在 QObject owner thread 使用；
 * acquire()/preWarm() 的跨线程调用会返回已完成的 WrongThread 结果且没有 socket 副作用。
 *
 * @note 错误生命周期：每次 lease/mutator 调用返回独立结果；成功清空可选诊断，失败状态为
 * 权威分类。pool 不保存 last-error，lease 的有效性由 pool generation 与活动记录决定。
 */
class QCURL_OTHER_EXTRAS_EXPORT QCWebSocketPool : public QObject
{
    Q_OBJECT

public:
    using LeaseId = QCWebSocketAcquireResult::LeaseId;

    /**
     * @brief lease 解析或归还操作的结构化结果。
     */
    enum class LeaseResult {
        Success,
        WrongThread,
        PoolDestroyed,
        InvalidArgument,
        UnknownLease,
        InactiveLease,
    };
    Q_ENUM(LeaseResult)

    /**
     * @brief 构造函数
     * @param config 连接池配置
     * @param parent 父对象
     */
    explicit QCWebSocketPool(const QCWebSocketPoolConfig &config, QObject *parent = nullptr);

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
     * @return 每次调用唯一的异步完成结果
     *
     * 优先从池中复用现有连接，如果无可用连接则创建新连接。
     * 如果达到连接数限制，返回 PoolLimitReached 并发射 poolLimitReached() 信号。
     *
     * @note 成功结果只携带 lease id。调用方必须在连接池 owner thread 使用
     * resolveLease() 临时解析连接，并在使用结束后通过 release() 归还同一 lease。
     */
    [[nodiscard]] QFuture<QCWebSocketAcquireResult> acquire(const QUrl &url);

    /**
     * @brief 在连接池 owner thread 解析活动 lease 对应的连接。
     * @param leaseId acquire() 成功结果中的非零 lease id。
     * @param socket 接收非 owning 借用指针；函数进入时先写入 nullptr。
     * @return 解析结果；只有 Success 表示 `socket` 已写入有效借用指针。
     *
     * 成功返回的指针由连接池拥有，仅在原 lease 保持活动、连接未被清理或销毁且
     * 连接池仍存活期间有效。调用方不得删除、重新设置 parent 或建立 owning
     * 智能指针；跨事件循环或可能重入的调用保存该指针时必须自行使用 QPointer。
     *
     * @note 本函数及连接对象的后续操作只能在连接池 owner thread 执行。
     * WrongThread、PoolDestroyed、InvalidArgument、UnknownLease 和 InactiveLease
     * 均不会改变连接池状态。
     */
    [[nodiscard]] LeaseResult resolveLease(LeaseId leaseId, QCWebSocket **socket) const;

    /**
     * @brief 将活动 lease 对应的连接同步归还连接池。
     * @param leaseId acquire() 成功结果中的非零 lease id。
     * @return 结构化归还结果。
     *
     * 成功时连接转为空闲状态且该 lease 立即失效。重复归还、连接已清理或已销毁
     * 返回 InactiveLease；从未由本池签发的 id 返回 UnknownLease。错误线程和销毁中
     * 调用均失败且不改变连接池状态。
     *
     * @note 归还成功后，先前由 resolveLease() 返回的借用指针不得继续使用。
     */
    [[nodiscard]] LeaseResult release(LeaseId leaseId);

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
     * @brief 同步清理连接池。
     * @param url 要清理的 WebSocket URL；空 URL 表示清理全部连接。
     * @param error 可选的错误输出；失败时写入稳定诊断，成功时清空。
     * @return 在 owner thread 中完成清理时返回 `true`，否则返回 `false`。
     *
     * 关闭并延迟删除指定 URL 的全部连接，包括活跃连接和空闲连接。
     * 合法的空池清理是成功的 no-op。错误线程或销毁中的连接池会失败，
     * 不会排队到其他线程，也不会改变连接池状态。
     */
    [[nodiscard]] bool clearPool(const QUrl &url = QUrl(), QString *error = nullptr);

    /**
     * @brief 预热连接
     * @param url WebSocket URL
     * @param count 预建立的连接数
     *
     * 异步预先建立指定数量的连接，减少首次请求的延迟。
     * 适用于已知即将发起大量请求的场景。
     */
    [[nodiscard]] QFuture<QCWebSocketPreWarmResult> preWarm(const QUrl &url, int count);

    /**
     * @brief 同步更新连接池配置。
     * @param config 新配置；参数仅在本次调用期间借用，成功时复制到池内。
     * @param error 可选的错误输出；失败时写入稳定诊断，成功时清空。
     * @return 在 owner thread 中完成配置更新时返回 `true`，否则返回 `false`。
     *
     * 错误线程、销毁中的连接池或非法配置会失败且不改变现有配置和定时器。
     * 调用不会排队到其他线程。配置更新只影响后续操作，不重写已建立连接的状态。
     */
    [[nodiscard]] bool setConfig(const QCWebSocketPoolConfig &config, QString *error = nullptr);

    /**
     * @brief 获取当前配置
     * @return 配置副本
     */
    QCWebSocketPoolConfig config() const;

    // ==================
    // 统计信息
    // ==================

    /**
     * @brief 获取统计信息
     * @param url WebSocket URL，如果为空则返回全局统计
     * @return 统计信息结构体
     */
    QCWebSocketPoolStats statistics(const QUrl &url = QUrl()) const;

Q_SIGNALS:
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

private Q_SLOTS:
    void onCleanupTimer();
    void onKeepAliveTimer();
    void onSocketConnected();
    void onSocketDisconnected();
    void onSocketError(const QString &error);
    void onSocketPong(const QByteArray &payload);
    void onSocketDestroyed(QObject *object);

private:
    Q_DISABLE_COPY_MOVE(QCWebSocketPool)

    QScopedPointer<QCWebSocketPoolPrivate> d_ptr;

    // 内部方法
    QCWebSocket *createNewConnection(const QUrl &url);
    void failPendingAcquire(QCWebSocket *socket,
                            QCWebSocketAcquireResult::Status status,
                            const QString &error);
    void removeConnection(QCWebSocket *socket);
    void cleanupIdleConnections();
    void sendKeepAlive();
    bool canCreateConnection(const QUrl &url) const;
    int totalConnectionCount() const;
};

} // namespace QCurl

#endif // QCURL_WEBSOCKET_SUPPORT
#endif // QCWEBSOCKETPOOL_H
