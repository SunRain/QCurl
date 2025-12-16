#ifndef QCNETWORKREPLY_H
#define QCNETWORKREPLY_H

#include <QObject>
#include <QUrl>
#include <QByteArray>
#include <QList>
#include <QPair>

#include <optional>
#include <functional>

#include "QCNetworkRequest.h"
#include "QCNetworkError.h"
#include "QCUtility.h"

namespace QCurl {

// ============================================================================
// 前向声明
// ============================================================================

class QCNetworkReplyPrivate;  // 私有实现类（定义在 QCNetworkReply_p.h）

// ============================================================================
// 枚举定义
// ============================================================================

/**
 * @brief HTTP 请求方法
 */
enum class HttpMethod {
    Head,   ///< HEAD 请求（仅获取响应头）
    Get,    ///< GET 请求（获取完整响应）
    Post,   ///< POST 请求（发送数据）
    Put,    ///< PUT 请求（上传资源）
    Delete, ///< DELETE 请求（删除资源）
    Patch   ///< PATCH 请求（部分更新）
};

/**
 * @brief 执行模式
 */
enum class ExecutionMode {
    Async, ///< 异步执行（使用 CurlMultiHandleProcesser）
    Sync   ///< 同步执行（阻塞调用 curl_easy_perform）
};

/**
 * @brief 请求状态
 */
enum class ReplyState {
    Idle,      ///< 空闲（未开始）
    Running,   ///< 运行中
    Finished,  ///< 已完成
    Cancelled, ///< 已取消
    Error      ///< 错误
};

// ============================================================================
// 类型定义
// ============================================================================

using RawHeaderPair = QPair<QByteArray, QByteArray>;

/// 数据回调函数类型（同步模式）
using DataFunction = std::function<size_t(char *buffer, size_t size)>;
/// 定位回调函数类型（同步模式）
using SeekFunction = std::function<int(qint64 offset, int origin)>;
/// 进度回调函数类型（同步模式）
using ProgressFunction = std::function<void(qint64 dltotal, qint64 dlnow,
                                            qint64 ultotal, qint64 ulnow)>;

// ============================================================================
// QCNetworkReply 类
// ============================================================================

/**
 * @brief 统一的网络响应类
 *
 * 整合了原有的 6 个 Reply 类的功能：
 * - QCNetworkAsyncReply (异步基类)
 * - QCNetworkAsyncHttpHeadReply (HEAD)
 * - QCNetworkAsyncHttpGetReply (GET)
 * - QCNetworkAsyncDataPostReply (POST)
 * - QCNetworkSyncReply (同步)
 * - CurlEasyHandleInitializtionClass (curl封装基类)
 *
 * @par 设计特点
 * - 使用 HttpMethod 枚举区分 HTTP 方法（而非继承）
 * - 使用 ExecutionMode 枚举支持同步/异步两种模式
 * - 使用 QCCurlHandleManager 管理 curl 句柄（RAII）
 * - 使用 Pimpl 模式隐藏实现细节
 * - 现代 C++17 风格（[[nodiscard]]、std::optional）
 *
 * @par 异步模式示例
 * @code
 * auto *mgr = new QCNetworkAccessManager();
 * QCNetworkRequest request(QUrl("https://example.com/api"));
 *
 * QCNetworkReply *reply = mgr->sendGet(request);
 *
 * connect(reply, &QCNetworkReply::finished, [reply]() {
 *     if (reply->error() == NetworkError::NoError) {
 *         if (auto data = reply->readAll()) {
 *             qDebug() << "Downloaded:" << data->size() << "bytes";
 *         }
 *     }
 *     reply->deleteLater();
 * });
 * @endcode
 *
 */
class QCNetworkReply : public QObject
{
    Q_OBJECT
    friend class QCNetworkAccessManager;
    friend class CurlMultiHandleProcesser;  // 旧实现（待删除）
    friend class QCCurlMultiManager;        // 新实现（v2.0）

public:
    // ========================================================================
    // 构造与析构
    // ========================================================================

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
                           QObject *parent = nullptr);

    ~QCNetworkReply() override;

    // 禁止拷贝
    QCNetworkReply(const QCNetworkReply&) = delete;
    QCNetworkReply& operator=(const QCNetworkReply&) = delete;

    // ========================================================================
    // 执行控制
    // ========================================================================

    void execute();
    void cancel();
    void pause();
    void resume();

    // ========================================================================
    // 数据访问（现代 C++17 风格）
    // ========================================================================

    [[nodiscard]] std::optional<QByteArray> readAll() const;
    [[nodiscard]] std::optional<QByteArray> readBody() const;
    [[nodiscard]] QList<RawHeaderPair> rawHeaders() const;
    [[nodiscard]] QByteArray rawHeaderData() const;
    [[nodiscard]] QUrl url() const;
    [[nodiscard]] qint64 bytesAvailable() const noexcept;

    // ========================================================================
    // 状态查询
    // ========================================================================

    [[nodiscard]] ReplyState state() const noexcept;
    [[nodiscard]] NetworkError error() const noexcept;
    [[nodiscard]] QString errorString() const;
    [[nodiscard]] bool isFinished() const noexcept;
    [[nodiscard]] bool isRunning() const noexcept;
    [[nodiscard]] qint64 bytesReceived() const noexcept;
    [[nodiscard]] qint64 bytesTotal() const noexcept;

    // ========================================================================
    // 同步模式专用 API
    // ========================================================================

    void setRequestBody(const QByteArray &data);
    void setWriteCallback(const DataFunction &func);
    void setHeaderCallback(const DataFunction &func);
    void setSeekCallback(const SeekFunction &func);
    void setProgressCallback(const ProgressFunction &func);

    // ========================================================================
    // 信号（异步模式）
    // ========================================================================

Q_SIGNALS:
    void finished();
    void error(NetworkError errorCode);
    void readyRead();
    void downloadProgress(qint64 bytesReceived, qint64 bytesTotal);
    void uploadProgress(qint64 bytesSent, qint64 bytesTotal);
    void stateChanged(ReplyState newState);
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

    // ========================================================================
    // 公共槽
    // ========================================================================

public Q_SLOTS:
    void deleteLater();

private:
    // ========================================================================
    // 私有实现（Pimpl 模式）
    // ========================================================================

    QCNetworkReplyPrivate *d_ptr;
    Q_DECLARE_PRIVATE(QCNetworkReply)

    // ========================================================================
    // 缓存集成私有方法
    // ========================================================================

    bool loadFromCache(bool ignoreExpiry);
    QMap<QByteArray, QByteArray> parseResponseHeaders();
};

} // namespace QCurl

#endif // QCNETWORKREPLY_H
