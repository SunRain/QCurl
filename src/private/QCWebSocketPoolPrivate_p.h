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
#include <QPointer>
#include <QPromise>

#include <memory>
#include <optional>

class QTimer;

namespace QCurl {

/// 保存连接池运行时状态，避免在 public header 暴露容器、锁和定时器布局。
class QCWebSocketPoolPrivate
{
public:
    QCWebSocketPoolPrivate();

    /// 保存输入校验的布尔结果和稳定失败诊断。
    struct ValidationResult
    {
        bool valid = false;
        QString error;
    };

    /// 连接池生命周期状态；Destroying 状态禁止所有外部重入操作。
    enum class LifecycleState {
        Running,
        Destroying,
    };

    LifecycleState lifecycleState = LifecycleState::Running;

    /// 连接池内的单条 socket 记录；socket 由 QCWebSocketPool 通过 parent/deleteLater 管理。
    struct PooledConnection
    {
        QPointer<QCWebSocket> socket; ///< 由 QCWebSocketPool 通过 QObject parent 持有。
        QObject *identity = nullptr;  ///< 仅用于 destroyed 信号匹配，不解引用该指针。
        QCWebSocketAcquireResult::LeaseId leaseId = 0; ///< 当前活动借用；0 表示未借出。
        QDateTime lastUsedTime;
        QDateTime createdTime;
        bool inUse     = false; ///< true 时不得被其他 acquire() 调用复用。
        int reuseCount = 0;     ///< 成功从池中再次取得该连接的累计次数。
        std::shared_ptr<QPromise<QCWebSocketAcquireResult>> acquirePromise;
        QByteArray keepAliveNonce;
        QDateTime keepAliveDeadline;
    };

    /// URL 到连接列表的主索引；仅由 pool owner thread 访问。
    QMap<QUrl, QList<PooledConnection>> pools;
    /// QObject 到 URL 的反向索引；仅由 pool owner thread 访问。
    QHash<QObject *, QUrl> socketToUrl;
    /// 活动 lease 到 socket 身份地址的索引；地址只用于 owner-thread 容器匹配。
    QHash<QCWebSocketAcquireResult::LeaseId, QObject *> activeLeases;
    /// 当前连接池的进程内唯一 generation；0 表示 generation 空间已耗尽。
    quint32 leasePoolGeneration = 0;
    /// 下一次签发的池内序号；超过 quint32 上限后停止签发。
    quint64 nextLeaseSequence = 1;
    /// 当前连接池生命周期内已签发池内序号的上界。
    quint32 issuedLeaseSequenceUpperBound = 0;
    /// 当前配置快照；setConfig() 后只影响后续操作。
    QCWebSocketPoolConfig config;
    /// 由 QCWebSocketPool parent 管理的空闲清理定时器。
    QTimer *cleanupTimer = nullptr;
    /// 由 QCWebSocketPool parent 管理的保活定时器。
    QTimer *keepAliveTimer = nullptr;
    /// 每个 URL 的复用命中次数。
    QHash<QUrl, int> hitCounts;
    /// 每个 URL 的新建连接次数。
    QHash<QUrl, int> missCounts;

    struct PreWarmOperation
    {
        std::shared_ptr<QPromise<QCWebSocketPreWarmResult>> promise;
        int requested                           = 0;
        int pending                             = 0;
        int warmed                              = 0;
        QCWebSocketPreWarmResult::Status status = QCWebSocketPreWarmResult::Status::Success;
        QString error;
        bool finished = false;
    };
    QList<std::shared_ptr<PreWarmOperation>> preWarmOperations;

    /// 校验连接池配置的全部公开范围约束。
    [[nodiscard]] static ValidationResult validateConfig(const QCWebSocketPoolConfig &config);
    /// 校验单次 preWarm 请求的连接数量。
    [[nodiscard]] static ValidationResult validatePreWarmCount(int count);
    /// 将有效保活秒数转换为 int 可表示的毫秒数。
    [[nodiscard]] static std::optional<int> keepAliveIntervalMilliseconds(int seconds);
    /**
     * @brief 为指定 socket 身份签发本池生命周期内不复用的 lease id。
     * @param identity 仅用于容器匹配的 socket 身份地址，不在此函数中解引用。
     * @return 成功时返回非零 id；参数为空或 id 空间耗尽时返回 std::nullopt。
     */
    [[nodiscard]] std::optional<QCWebSocketAcquireResult::LeaseId> issueLease(QObject *identity);
    /**
     * @brief 撤销活动 lease；对 0 或已撤销 id 安全无操作。
     * @param leaseId 待撤销的 lease id。
     */
    void invalidateLease(QCWebSocketAcquireResult::LeaseId leaseId);
    /**
     * @brief 判断 id 是否曾由当前连接池实例签发。
     * @param leaseId 待判断的 id。
     * @return 非零且不大于已签发上界时返回 true。
     */
    [[nodiscard]] bool wasLeaseIssued(QCWebSocketAcquireResult::LeaseId leaseId) const noexcept;
    /// 清理指定 URL 的连接和索引；空 URL 表示清理全部连接。
    void clearConnections(QCWebSocketPool *poolOwner, const QUrl &url);

    /// 收口 preWarm 单条 acquire 的完成状态，并在最后一条完成时结算 operation promise。
    void completePreWarmAcquire(const std::shared_ptr<PreWarmOperation> &operation,
                                const QCWebSocketAcquireResult &result);
};

} // namespace QCurl

#endif // QCWEBSOCKETPOOLPRIVATE_P_H
