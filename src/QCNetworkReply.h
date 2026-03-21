#ifndef QCNETWORKREPLY_H
#define QCNETWORKREPLY_H

#include "QCNetworkError.h"
#include "QCNetworkHttpMethod.h"
#include "QCNetworkRequest.h"

#include <QByteArray>
#include <QList>
#include <QObject>
#include <QPair>
#include <QScopedPointer>
#include <QStringList>
#include <QUrl>

#include <functional>
#include <optional>

namespace QCurl {
class QCNetworkReply;

// ==================
// 前向声明
// ==================

class QCNetworkReplyPrivate; // 私有实现类（定义在 QCNetworkReply_p.h）

// ==================
// 枚举定义
// ==================

/**
 * @brief 执行模式
 */
enum class ExecutionMode {
    Async, ///< 异步执行（由 QCCurlMultiManager 驱动）
    Sync   ///< 同步执行（阻塞调用 curl_easy_perform）
};

/**
 * @brief 传输级暂停模式（映射 libcurl CURLPAUSE_*）
 */
enum class PauseMode {
    Recv, ///< 暂停接收（CURLPAUSE_RECV）
    Send, ///< 暂停发送（CURLPAUSE_SEND）
    All   ///< 暂停收发（CURLPAUSE_ALL）
};

/**
 * @brief 请求状态
 */
enum class ReplyState {
    Idle,      ///< 空闲（未开始）
    Running,   ///< 运行中
    Paused,    ///< 已暂停（仅异步传输级 pause/resume）
    Finished,  ///< 已完成
    Cancelled, ///< 已取消
    Error      ///< 错误
};

// ==================
// 类型定义
// ==================

using RawHeaderPair = QPair<QByteArray, QByteArray>;

/// 数据回调函数类型（同步模式）
using DataFunction = std::function<size_t(char *buffer, size_t size)>;
/// 定位回调函数类型（同步模式）
using SeekFunction = std::function<int(qint64 offset, int origin)>;
/// 进度回调函数类型（同步模式）
using ProgressFunction
    = std::function<void(qint64 dltotal, qint64 dlnow, qint64 ultotal, qint64 ulnow)>;

// ==================
// QCNetworkReply 类
// ==================

/**
 * @brief 表示单个网络请求的执行状态与结果
 *
 * reply 同时覆盖同步和异步执行路径，并暴露 body、header、错误状态与
 * 传输控制入口。
 */
class QCURL_EXPORT QCNetworkReply : public QObject
{
    Q_OBJECT
    friend class QCNetworkAccessManager;
    friend class CurlMultiHandleProcesser;
    friend class QCCurlMultiManager;

public:
    // ==================
    // 构造与析构
    // ==================

    /**
     * @brief 构造网络响应对象
     *
     * @param request 网络请求配置
     * @param method HTTP 方法
     * @param mode 执行模式（异步/同步）
     * @param requestBody 请求体数据（用于 POST/PUT/PATCH）
     * @param parent 父对象
     *
     * @note 通常不直接构造，而是通过 QCNetworkAccessManager 工厂方法创建
     */
    explicit QCNetworkReply(const QCNetworkRequest &request,
                            HttpMethod method,
                            ExecutionMode mode,
                            const QByteArray &requestBody = QByteArray(),
                            QObject *parent               = nullptr);

    ~QCNetworkReply() override;

    // 禁止拷贝
    QCNetworkReply(const QCNetworkReply &)            = delete;
    QCNetworkReply &operator=(const QCNetworkReply &) = delete;

    // ==================
    // 执行控制
    // ==================

    void execute();
    void cancel();
    void abortWithError(NetworkError error, const QString &message = QString());

    /**
     * @brief 传输级暂停（仅 Async 生效）
     *
     * 语义：
     * - 仅允许 `Running → Paused`（幂等）
     * - 跨线程调用会自动 marshal 到 reply 线程
     * - 不影响调度层（不会 cancel / removeReply / 出队）
     */
    void pauseTransport(PauseMode mode = PauseMode::All);

    /**
     * @brief 传输级恢复（仅 Async 生效）
     *
     * 语义：
     * - 仅允许 `Paused → Running`（幂等）
     * - 成功恢复后会触发一次 multi wakeup，以推进传输
     */
    void resumeTransport();

    // ==================
    // 数据访问（现代 C++17 风格）
    // ==================

    /**
     * @brief 读取并清空当前响应体缓冲
     *
     * 返回值语义：
     * - std::nullopt：尚无数据可读（未到终态且缓冲为空）
     * - QByteArray()：已到终态且 body 为空，或已在终态被 drain 过
     *
     * @note 这是 drain API，会修改内部缓冲区，因此为非 const
     */
    [[nodiscard]] std::optional<QByteArray> readAll();

    /**
     * @brief 读取响应体（readAll 别名）
     *
     * @return 同 readAll；Error/Idle 状态返回 std::nullopt
     */
    [[nodiscard]] std::optional<QByteArray> readBody();
    [[nodiscard]] QList<RawHeaderPair> rawHeaders() const;
    [[nodiscard]] QByteArray rawHeaderData() const;
    [[nodiscard]] QUrl url() const;
    [[nodiscard]] HttpMethod method() const noexcept;
    [[nodiscard]] int httpStatusCode() const noexcept;
    [[nodiscard]] qint64 durationMs() const noexcept;
    [[nodiscard]] qint64 bytesAvailable() const noexcept;
    [[nodiscard]] QStringList capabilityWarnings() const;

    // ==================
    // 状态查询
    // ==================

    [[nodiscard]] ReplyState state() const noexcept;
    [[nodiscard]] NetworkError error() const noexcept;
    [[nodiscard]] QString errorString() const;
    [[nodiscard]] bool isFinished() const noexcept;
    [[nodiscard]] bool isRunning() const noexcept;
    [[nodiscard]] bool isPaused() const noexcept;

    /**
     * @brief backpressure 是否激活（仅异步请求 + 启用时）
     *
     * backpressure 为内部接收流控：通过传输级 pause/resume 控制数据进入缓冲，
     * 不会改变 ReplyState（避免与用户显式 pause 语义冲突）。
     */
    [[nodiscard]] bool isBackpressureActive() const noexcept;

    /**
     * @brief 上传发送方向是否因“source not ready”处于内部 pause
     *
     * 说明：
     * - 仅表达内部流控 pause，不包含用户显式 pause（ReplyState::Paused）
     * - 不改变 ReplyState，用于可诊断与一致性合同验证
     */
    [[nodiscard]] bool isUploadSendPaused() const noexcept;

    /**
     * @brief backpressure 上限（bytes）
     *
     * 该值为高水位线（soft limit），用于内部接收流控触发点。
     * 注意：它不是 hard cap；由于 libcurl write callback 无法部分消费，bytesAvailable()
     * 允许出现“有界超限”（通常最多一个 write callback chunk）。
     *
     * @return 0 表示未启用
     */
    [[nodiscard]] qint64 backpressureLimitBytes() const noexcept;

    /**
     * @brief backpressure 低水位线（bytes）
     * @return 0 表示使用默认值（limit/2）或未启用
     */
    [[nodiscard]] qint64 backpressureResumeBytes() const noexcept;

    /**
     * @brief backpressure 缓冲峰值（bytes）
     * @return 0 表示未启用
     */
    [[nodiscard]] qint64 backpressureBufferedBytesPeak() const noexcept;
    [[nodiscard]] qint64 bytesReceived() const noexcept;
    [[nodiscard]] qint64 bytesTotal() const noexcept;

    // ==================
    // 同步模式专用 API
    // ==================

    void setRequestBody(const QByteArray &data);
    void setWriteCallback(const DataFunction &func);
    void setHeaderCallback(const DataFunction &func);
    void setSeekCallback(const SeekFunction &func);
    void setProgressCallback(const ProgressFunction &func);

    // ==================
    // 信号（异步模式）
    // ==================

Q_SIGNALS:
    void finished();
    void error(NetworkError errorCode);
    void readyRead();
    void downloadProgress(qint64 bytesReceived, qint64 bytesTotal);
    void uploadProgress(qint64 bytesSent, qint64 bytesTotal);
    void stateChanged(ReplyState newState);

    /**
     * @brief backpressure 状态变化信号（内部流控）
     *
     * 当内部接收流控进入/退出 pause 时发射，不会改变 ReplyState。
     */
    void backpressureStateChanged(bool active, qint64 bufferedBytes, qint64 limitBytes);

    /**
     * @brief 上传发送方向内部 pause 状态变化（source not ready）
     *
     * 仅表达内部流控，不改变 ReplyState。
     */
    void uploadSendPausedChanged(bool paused);
    void cancelled();

    /**
     * @brief 重试尝试信号
     *
     * 当请求失败并即将重试时发射此信号。
     *
     * @param attemptCount 当前重试次数（0 = 首次尝试，1 = 第一次重试，依此类推）
     * @param error 导致重试的错误类型
     */
    void retryAttempt(int attemptCount, NetworkError error);

    // ==================
    // 公共槽
    // ==================

public Q_SLOTS:
    void deleteLater();

private:
    // ==================
    // 私有实现（Pimpl 模式）
    // ==================

    Q_DECLARE_PRIVATE(QCNetworkReply)
    QScopedPointer<QCNetworkReplyPrivate> d_ptr;

    // ==================
    // 缓存集成私有方法
    // ==================

    bool loadFromCache(bool ignoreExpiry);
    QMap<QByteArray, QByteArray> parseResponseHeaders();
};

} // namespace QCurl

#endif // QCNETWORKREPLY_H
