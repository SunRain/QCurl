/**
 * @file
 * @brief 声明网络响应对象与传输控制接口。
 */

#ifndef QCNETWORKREPLY_H
#define QCNETWORKREPLY_H

#include "QCNetworkError.h"
#include "QCNetworkHttpMethod.h"

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
class QCNetworkRequest;

// ==================
// 前向声明
// ==================

class QCNetworkReplyPrivate; ///< 私有实现类（定义在 QCNetworkReply_p.h）

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
 * 传输控制入口。实例只能由 `QCNetworkAccessManager` 工厂路径创建。
 */
class QCURL_EXPORT QCNetworkReply : public QObject
{
    Q_OBJECT
    friend class QCNetworkAccessManager;
    friend class CurlMultiHandleProcesser;
    friend class QCCurlMultiManager;

public:
    // ==================
    // Test-only construction
    // ==================
    //
    // `QCNetworkReply` instances are intended to be created via
    // `QCNetworkAccessManager::send*()` so the manager can wire middleware,
    // mock/cache/logger/scheduler, etc.
    //
    // Some in-repo tests need to construct a reply directly (e.g. sync mode with
    // custom callbacks before execute). To avoid `#define private public` hacks,
    // we provide a narrowly-scoped constructor under the existing test hook
    // build flag.
#ifdef QCURL_ENABLE_TEST_HOOKS
    /// 仅供测试构造路径使用的 capability key。
    struct TestOnlyKey
    {
        /// 允许测试代码显式开启该构造路径。
        TestOnlyKey() = default;
    };

    /**
     * @brief 仅供测试直接构造 reply
     *
     * 该入口绕过 access manager 的工厂封装，供仓内测试搭建同步场景。
     */
    explicit QCNetworkReply(TestOnlyKey,
                            const QCNetworkRequest &request,
                            HttpMethod method,
                            ExecutionMode mode,
                            const QByteArray &requestBody = QByteArray(),
                            QObject *parent               = nullptr);
#endif

    /// 析构 reply 并释放底层 easy handle、回调和缓存资源。
    ~QCNetworkReply() override;

    // 禁止拷贝
    QCNetworkReply(const QCNetworkReply &)            = delete;
    QCNetworkReply &operator=(const QCNetworkReply &) = delete;

    // ==================
    // 执行控制
    // ==================

    /// 启动当前 reply 的执行流程。
    void execute();
    /// 取消当前请求，并尽快进入 Cancelled 终态。
    void cancel();

    /**
     * @brief 以指定错误中止 reply
     *
     * 用于在执行前或执行中快速落入错误终态，并保留错误消息。
     */
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
    /// 返回解析后的原始 header 键值对。
    [[nodiscard]] QList<RawHeaderPair> rawHeaders() const;
    /// 返回原始 header 数据块。
    [[nodiscard]] QByteArray rawHeaderData() const;
    /// 返回请求 URL。
    [[nodiscard]] QUrl url() const;
    /// 返回当前 reply 对应的 HTTP 方法。
    [[nodiscard]] HttpMethod method() const noexcept;
    /// 返回响应状态码；无响应时通常为 0。
    [[nodiscard]] int httpStatusCode() const noexcept;
    /// 返回请求耗时（毫秒）。
    [[nodiscard]] qint64 durationMs() const noexcept;
    /// 返回当前 body 缓冲中可读取的字节数。
    [[nodiscard]] qint64 bytesAvailable() const noexcept;
    /// 返回 capability 降级或兼容性告警列表。
    [[nodiscard]] QStringList capabilityWarnings() const;

    // ==================
    // 状态查询
    // ==================

    /// 返回当前 reply 状态。
    [[nodiscard]] ReplyState state() const noexcept;
    /// 返回当前错误码。
    [[nodiscard]] NetworkError error() const noexcept;
    /// 返回当前错误消息。
    [[nodiscard]] QString errorString() const;
    /// 返回 reply 是否已进入终态。
    [[nodiscard]] bool isFinished() const noexcept;
    /// 返回 reply 是否处于 Running。
    [[nodiscard]] bool isRunning() const noexcept;
    /// 返回 reply 是否处于用户显式暂停状态。
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
    /// 返回当前已接收字节数。
    [[nodiscard]] qint64 bytesReceived() const noexcept;
    /// 返回当前预期总字节数；未知时可能为 0 或负值映射结果。
    [[nodiscard]] qint64 bytesTotal() const noexcept;

    // ==================
    // 同步模式专用 API
    // ==================

    /// 设置同步模式下使用的内联请求体。
    void setRequestBody(const QByteArray &data);
    /// 设置同步模式写入 body 的数据回调。
    void setWriteCallback(const DataFunction &func);
    /// 设置同步模式处理响应头的数据回调。
    void setHeaderCallback(const DataFunction &func);
    /// 设置同步模式上传 seek 回调。
    void setSeekCallback(const SeekFunction &func);
    /// 设置同步模式进度回调。
    void setProgressCallback(const ProgressFunction &func);

    // ==================
    // 信号（异步模式）
    // ==================

Q_SIGNALS:
    /// 当 reply 进入终态时发射。
    void finished();
    /// 当 reply 进入 Error 终态时发射。
    void error(NetworkError errorCode);
    /// 当 body 缓冲可读时发射。
    void readyRead();
    /// 下载进度变化时发射。
    void downloadProgress(qint64 bytesReceived, qint64 bytesTotal);
    /// 上传进度变化时发射。
    void uploadProgress(qint64 bytesSent, qint64 bytesTotal);
    /// reply 状态发生切换时发射。
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
    /// 当请求被显式取消时发射。
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
    /// 通过 Qt 事件循环延迟销毁 reply。
    void deleteLater();

private:
    class FactoryKey
    {
        friend class QCNetworkAccessManager;

        FactoryKey() = default;
    };

    /**
     * @brief 构造网络响应对象
     *
     * @param key 仅 `QCNetworkAccessManager` 可构造的工厂通行证
     * @param request 网络请求配置
     * @param method HTTP 方法
     * @param mode 执行模式（异步/同步）
     * @param requestBody 请求体数据（用于 POST/PUT/PATCH）
     * @param parent 父对象
     *
     * @note reply 只能通过 `QCNetworkAccessManager` 工厂路径创建
     */
    explicit QCNetworkReply(FactoryKey key,
                            const QCNetworkRequest &request,
                            HttpMethod method,
                            ExecutionMode mode,
                            const QByteArray &requestBody = QByteArray(),
                            QObject *parent               = nullptr);

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
