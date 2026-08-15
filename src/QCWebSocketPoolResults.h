/**
 * @file
 * @brief 声明 WebSocket 连接池异步结果值类型。
 */

#ifndef QCWEBSOCKETPOOLRESULTS_H
#define QCWEBSOCKETPOOLRESULTS_H

#include "QCGlobal.h"

#ifdef QCURL_WEBSOCKET_SUPPORT

#include <QSharedDataPointer>
#include <QString>
#include <QtGlobal>

namespace QCurl {

class QCWebSocketAcquireResultData;
class QCWebSocketPreWarmResultData;

/**
 * @brief 单次 acquire() 的纯值异步完成结果。
 *
 * 成功结果只保存不透明 lease id，不保存、判空或解引用任何 QObject 指针。
 * 该值可在任意线程复制、读取和销毁；连接对象只能由连接池在 owner thread
 * 通过 lease id 解析。
 *
 * @note 错误生命周期：结果构造后不再变化。成功时 `error()` 为空；失败时 `status()`
 * 是权威分类，错误文本只用于诊断。preWarm 结果遵循相同合同。
 */
class QCURL_OTHER_EXTRAS_EXPORT QCWebSocketAcquireResult
{
public:
    using LeaseId = quint64;

    enum class Status {
        Success,
        WrongThread,
        PoolLimitReached,
        ConnectionFailed,
        PoolDestroyed,
        Cancelled,
    };

    QCWebSocketAcquireResult();
    QCWebSocketAcquireResult(const QCWebSocketAcquireResult &other);
    QCWebSocketAcquireResult(QCWebSocketAcquireResult &&other) noexcept;
    ~QCWebSocketAcquireResult();
    QCWebSocketAcquireResult &operator=(const QCWebSocketAcquireResult &other);
    QCWebSocketAcquireResult &operator=(QCWebSocketAcquireResult &&other) noexcept;

    /**
     * @brief 创建成功结果。
     * @param leaseId 连接池签发的非零 lease id。
     * @return 只包含纯值 lease id 的成功结果。
     */
    static QCWebSocketAcquireResult success(LeaseId leaseId);
    static QCWebSocketAcquireResult failure(Status status, const QString &error = {});
    [[nodiscard]] Status status() const noexcept;
    /**
     * @brief 获取成功 acquire 对应的不透明 lease id。
     * @return 成功结果返回非零 id；失败结果返回 0。
     * @note lease id 只用于提交给原连接池，不能转换为地址或跨连接池使用。
     */
    [[nodiscard]] LeaseId leaseId() const noexcept;
    [[nodiscard]] QString error() const;
    [[nodiscard]] bool isSuccess() const noexcept;

private:
    QSharedDataPointer<QCWebSocketAcquireResultData> d;
};

/// 单次 preWarm() 的异步完成结果。
class QCURL_OTHER_EXTRAS_EXPORT QCWebSocketPreWarmResult
{
public:
    enum class Status {
        Success,
        WrongThread,
        PoolLimitReached,
        ConnectionFailed,
        PoolDestroyed,
        Cancelled,
        /// 输入参数不在连接池合同允许的范围内。
        InvalidArgument,
    };

    QCWebSocketPreWarmResult();
    QCWebSocketPreWarmResult(const QCWebSocketPreWarmResult &other);
    QCWebSocketPreWarmResult(QCWebSocketPreWarmResult &&other) noexcept;
    ~QCWebSocketPreWarmResult();
    QCWebSocketPreWarmResult &operator=(const QCWebSocketPreWarmResult &other);
    QCWebSocketPreWarmResult &operator=(QCWebSocketPreWarmResult &&other) noexcept;

    static QCWebSocketPreWarmResult success(int requested, int warmed);
    static QCWebSocketPreWarmResult failure(Status status,
                                            int requested,
                                            int warmed,
                                            const QString &error = {});
    [[nodiscard]] Status status() const noexcept;
    [[nodiscard]] int requestedCount() const noexcept;
    [[nodiscard]] int warmedCount() const noexcept;
    [[nodiscard]] QString error() const;
    [[nodiscard]] bool isSuccess() const noexcept;

private:
    QSharedDataPointer<QCWebSocketPreWarmResultData> d;
};

} // namespace QCurl

#endif // QCURL_WEBSOCKET_SUPPORT
#endif // QCWEBSOCKETPOOLRESULTS_H
