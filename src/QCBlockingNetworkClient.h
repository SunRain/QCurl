/**
 * @file
 * @brief 声明 Blocking Extras 的显式同步请求入口。
 */

#ifndef QCBLOCKINGNETWORKCLIENT_H
#define QCBLOCKINGNETWORKCLIENT_H

#include "QCBlockingNetworkResult.h"
#include "QCBlockingCookieStore.h"
#include "QCNetworkHttpMethod.h"
#include "QCNetworkRequest.h"

#include <QByteArray>
#include <QByteArrayView>
#include <QSharedDataPointer>
#include <QUrl>

#include <optional>

class QIODevice;

namespace QCurl {

class QCBlockingNetworkClientData;
class QCBlockingNetworkClientOptionsData;
class QCBlockingRequestOptionsData;

/**
 * @brief Blocking Extras 同步请求的传输进度快照。
 *
 * 该值类型只描述当前回调时刻的上传和下载字节计数；总字节数未知时沿用 libcurl 上报值。
 */
class QCURL_EXPORT QCTransferProgress
{
public:
    QCTransferProgress() = default;
    QCTransferProgress(qint64 bytesReceived, qint64 bytesTotal, qint64 bytesSent, qint64 uploadTotal);

    /// 返回已接收的响应体字节数。
    [[nodiscard]] qint64 bytesReceived() const noexcept;
    /// 返回预期响应体总字节数；底层传输未知时保持 libcurl 上报值。
    [[nodiscard]] qint64 bytesTotal() const noexcept;
    /// 返回已发送的请求体字节数。
    [[nodiscard]] qint64 bytesSent() const noexcept;
    /// 返回预期请求体总字节数；底层传输未知时保持 libcurl 上报值。
    [[nodiscard]] qint64 uploadTotal() const noexcept;

private:
    qint64 m_bytesReceived = 0;
    qint64 m_bytesTotal = -1;
    qint64 m_bytesSent = 0;
    qint64 m_uploadTotal = -1;
};

/**
 * @brief Blocking Extras 进度回调。
 *
 * 回调在执行阻塞请求的调用线程内同步触发，不会转发到其他事件线程。
 * 回调期间不要重入同一个 `QCBlockingNetworkClient` 实例或依赖当前请求阻塞住的事件循环。
 * `userData` 是调用者传给 `setProgressCallback()` 的原始指针；QCurl 不拥有、不释放，
 * 调用者必须保证它在请求结束前持续有效。回调不应抛出异常；返回 `false` 会中止当前阻塞请求，
 * 结果错误为 `NetworkError::OperationCancelled`。
 */
using QCBlockingProgressCallback = bool (*)(const QCTransferProgress &progress, void *userData);

class QCURL_EXPORT QCBlockingRequestOptions
{
public:
    QCBlockingRequestOptions();
    QCBlockingRequestOptions(const QCBlockingRequestOptions &other);
    QCBlockingRequestOptions(QCBlockingRequestOptions &&other) noexcept;
    ~QCBlockingRequestOptions();

    QCBlockingRequestOptions &operator=(const QCBlockingRequestOptions &other);
    QCBlockingRequestOptions &operator=(QCBlockingRequestOptions &&other) noexcept;

    /// 返回内存响应体上限；默认 16 MiB，负值 setter 会转换为无上限。
    [[nodiscard]] qint64 maxInMemoryBodyBytes() const noexcept;
    /// 设置内存响应体上限；`0` 只允许空响应体，负值表示无上限。
    void setMaxInMemoryBodyBytes(qint64 bytes) noexcept;

    /// 返回当前进度回调；空指针表示不启用阻塞进度回调。
    [[nodiscard]] QCBlockingProgressCallback progressCallback() const noexcept;
    /// 返回传给进度回调的用户指针；QCurl 不解引用、不拥有。
    [[nodiscard]] void *progressCallbackUserData() const noexcept;
    /// 设置阻塞请求进度回调；传入空指针会清除回调和用户指针。
    void setProgressCallback(QCBlockingProgressCallback callback,
                             void *userData = nullptr) noexcept;
    /// 返回随当前请求发送的 cookie 快照。
    [[nodiscard]] QCCookieSnapshot cookieSnapshot() const;
    /// 设置随当前请求发送的 cookie 快照。
    void setCookieSnapshot(const QCCookieSnapshot &cookies);

private:
    QSharedDataPointer<QCBlockingRequestOptionsData> d;
};

/**
 * @brief Blocking Extras 的同步请求客户端。
 *
 * 该客户端属于显式启用的 Blocking Extras surface，不属于默认 Core。
 * 它返回值结果，不返回 `QCNetworkReply *`，也不暴露 live manager。
 */
class QCURL_EXPORT QCBlockingNetworkClient
{
public:
    using RequestOptions = QCBlockingRequestOptions;

    enum class ApplicationThreadPolicy {
        Reject,
        AllowForCliOrTests,
    };

    class QCURL_EXPORT Options
    {
    public:
        Options();
        Options(const Options &other);
        Options(Options &&other) noexcept;
        ~Options();

        Options &operator=(const Options &other);
        Options &operator=(Options &&other) noexcept;

        [[nodiscard]] ApplicationThreadPolicy applicationThreadPolicy() const noexcept;
        void setApplicationThreadPolicy(ApplicationThreadPolicy policy) noexcept;

    private:
        QSharedDataPointer<QCBlockingNetworkClientOptionsData> d;
    };

    QCBlockingNetworkClient();
    explicit QCBlockingNetworkClient(Options options);
    QCBlockingNetworkClient(const QCBlockingNetworkClient &other);
    QCBlockingNetworkClient(QCBlockingNetworkClient &&other) noexcept;
    ~QCBlockingNetworkClient();

    QCBlockingNetworkClient &operator=(const QCBlockingNetworkClient &other);
    QCBlockingNetworkClient &operator=(QCBlockingNetworkClient &&other) noexcept;

    [[nodiscard]] Options options() const;
    void setOptions(const Options &options);

    [[nodiscard]] QCBlockingNetworkResult get(
        const QCNetworkRequest &request,
        const QCBlockingRequestOptions &requestOptions = {}) const;
    [[nodiscard]] QCBlockingNetworkResult head(
        const QCNetworkRequest &request,
        const QCBlockingRequestOptions &requestOptions = {}) const;
    [[nodiscard]] QCBlockingNetworkResult deleteResource(
        const QCNetworkRequest &request,
        const QCBlockingRequestOptions &requestOptions = {}) const;
    [[nodiscard]] QCBlockingNetworkResult post(
        const QCNetworkRequest &request,
        const QByteArray &body,
        const QCBlockingRequestOptions &requestOptions = {}) const;
    [[nodiscard]] QCBlockingNetworkResult put(
        const QCNetworkRequest &request,
        const QByteArray &body,
        const QCBlockingRequestOptions &requestOptions = {}) const;
    [[nodiscard]] QCBlockingNetworkResult patch(
        const QCNetworkRequest &request,
        const QByteArray &body,
        const QCBlockingRequestOptions &requestOptions = {}) const;
    [[nodiscard]] QCBlockingNetworkResult post(
        const QCNetworkRequest &request,
        QIODevice *body,
        std::optional<qint64> sizeBytes = std::nullopt,
        const QCBlockingRequestOptions &requestOptions = {}) const;
    [[nodiscard]] QCBlockingNetworkResult put(
        const QCNetworkRequest &request,
        QIODevice *body,
        std::optional<qint64> sizeBytes = std::nullopt,
        const QCBlockingRequestOptions &requestOptions = {}) const;
    [[nodiscard]] QCBlockingNetworkResult sendCustomRequest(
        const QCNetworkRequest &request,
        QByteArrayView method,
        const QCBlockingRequestOptions &requestOptions = {}) const;
    [[nodiscard]] QCBlockingNetworkResult sendCustomRequest(
        const QCNetworkRequest &request,
        QByteArrayView method,
        const QByteArray &body,
        const QCBlockingRequestOptions &requestOptions = {}) const;
    [[nodiscard]] QCBlockingNetworkResult downloadToDevice(
        const QCNetworkRequest &request,
        QIODevice *output,
        const QCBlockingRequestOptions &requestOptions = {}) const;

private:
    [[nodiscard]] QCBlockingNetworkResult perform(const QCNetworkRequest &request,
                                                  HttpMethod method,
                                                  const QByteArray &body,
                                                  const QCBlockingRequestOptions &requestOptions =
                                                      {}) const;
    [[nodiscard]] QCBlockingNetworkResult perform(const QCNetworkRequest &request,
                                                  HttpMethod method,
                                                  QIODevice *body,
                                                  std::optional<qint64> sizeBytes,
                                                  const QCBlockingRequestOptions
                                                      &requestOptions) const;
    [[nodiscard]] QCBlockingNetworkResult performCustom(const QCNetworkRequest &request,
                                                        QByteArrayView method,
                                                        const QByteArray &body,
                                                        const QCBlockingRequestOptions
                                                            &requestOptions) const;
    [[nodiscard]] bool applicationThreadRejected() const;

    QSharedDataPointer<QCBlockingNetworkClientData> d;
};

} // namespace QCurl

#endif // QCBLOCKINGNETWORKCLIENT_H
