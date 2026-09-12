/**
 * @file
 * @brief 声明网络响应对象与传输控制接口。
 */

#ifndef QCNETWORKREPLY_H
#define QCNETWORKREPLY_H

#include "QCNetworkError.h"
#include "QCNetworkHttpMethod.h"
#include "QCNetworkLogger.h"
#include "QCNetworkTypes.h"

#include <QByteArray>
#include <QList>
#include <QObject>
#include <QScopedPointer>
#include <QStringList>
#include <QUrl>

#include <optional>

namespace QCurl {
class QCNetworkReply;
class QCNetworkAccessManagerPrivate;
class QCNetworkRequest;

namespace Internal {
class QCNetworkReplyExecution;
struct RequestBody;
} // namespace Internal

// ==================
// 前向声明
// ==================

class QCNetworkReplyPrivate; ///< 私有实现类。

// ==================
// 枚举定义
// ==================

/**
 * @brief 传输级暂停模式（映射 libcurl CURLPAUSE_*）
 */
enum class PauseMode {
    Recv, ///< 暂停接收（CURLPAUSE_RECV）
    Send, ///< 暂停发送（CURLPAUSE_SEND）
    All,  ///< 暂停收发（CURLPAUSE_ALL）
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
    Error,     ///< 错误
};

// ==================
// QCNetworkReply 类
// ==================

/**
 * @brief 表示单个网络请求的执行状态与结果
 *
 * reply 表达 Qt 风格异步请求路径，暴露 body、header、错误状态与
 * 传输控制入口。实例只能由 `QCNetworkAccessManager` 工厂路径创建。
 *
 * @note 直接连接的信号处理函数可同步销毁 reply 或其 parent。销毁发生后，当前调用栈
 * 不会补发 `finished()` 或执行后续 cache、retry、multi 副作用。
 * @note 错误生命周期：新执行周期开始时清空旧错误；首次终态后 `error()` 是权威分类，
 * `errorString()` 仅在失败终态有效，并与状态一起保持到析构。命令返回或信号投递不建立
 * 独立的“最近错误”状态。
 * @note QObject 借用合同：所有对外暴露的 reply 裸指针均为 non-owning 当次借用；reply 或
 * 其 parent 销毁后立即失效，只能在 reply owner thread 判空、读取或调用，不得跨异步边界保存。
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
    // Some in-repo tests need to construct a reply directly without manager
    // middleware/scheduler setup. To avoid `#define private public` hacks, we
    // provide a narrowly-scoped constructor under the existing test hook build flag.
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
     * 该入口绕过 access manager 的工厂封装，仅供仓内测试搭建异步 reply 场景。
     */
    explicit QCNetworkReply(TestOnlyKey,
                            const QCNetworkRequest &request,
                            HttpMethod method,
                            const Internal::RequestBody &requestBodySource,
                            const QByteArray &requestBody = QByteArray(),
                            QObject *parent               = nullptr);

    explicit QCNetworkReply(TestOnlyKey,
                            const QCNetworkRequest &request,
                            HttpMethod method,
                            const QByteArray &requestBody = QByteArray(),
                            QObject *parent               = nullptr);
#endif

    /// 析构 reply 并释放底层 easy handle、回调和缓存资源。
    ~QCNetworkReply() override;

    /**
     * @brief 返回创建 reply 时捕获的 logger 句柄快照。
     *
     * middleware 可复制并持有该 snapshot，避免访问 reply 的私有实现。manager 后续替换
     * logger 不改变该结果；`get()` 返回的非 owning 借用只在至少一个同控制块句柄存活时有效。
     */
    [[nodiscard]] QCNetworkLoggerHandle loggerSnapshot() const;

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

    /// 返回解析后的原始 header 键值对。
    [[nodiscard]] QList<RawHeaderPair> rawHeaders() const;
    /// 返回最终响应头 block 中指定 header 的值；未命中返回空数组。
    [[nodiscard]] QByteArray rawHeader(const QByteArray &name) const;
    /// 返回最终响应头 block 中是否存在指定 header。
    [[nodiscard]] bool hasRawHeader(const QByteArray &name) const;
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

private:
    Q_DISABLE_COPY_MOVE(QCNetworkReply)

    class FactoryKey
    {
        friend class QCNetworkAccessManager;
        friend class QCNetworkAccessManagerPrivate;

        FactoryKey() = default;
    };

    /**
     * @brief 构造网络响应对象
     *
     * @param key 仅 `QCNetworkAccessManager` 可构造的工厂通行证
     * @param request 网络请求配置
     * @param method HTTP 方法
     * @param requestBody 请求体数据（用于 POST/PUT/PATCH）
     * @param parent 父对象
     *
     * @note reply 只能通过 `QCNetworkAccessManager` 工厂路径创建
     */
    explicit QCNetworkReply(FactoryKey key,
                            const QCNetworkRequest &request,
                            HttpMethod method,
                            const Internal::RequestBody &requestBodySource,
                            const QByteArray &requestBody = QByteArray(),
                            QObject *parent               = nullptr);

    // ==================
    // 私有实现（Pimpl 模式）
    // ==================

    friend class QCNetworkAccessManagerPrivate;
    friend class Internal::QCNetworkReplyExecution;

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
