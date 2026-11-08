/**
 * @file
 * @brief 持有进入 curl multi 的 easy handle 与回调上下文。
 */

#ifndef QCCURLMULTITRANSFERRECORD_P_H
#define QCCURLMULTITRANSFERRECORD_P_H

#include "QCCurlHandleManager.h"
#include "QCNetworkReply.h"
#include "private/QCNetworkReplyTransferState_p.h"

#include <QPointer>
#include <QSharedPointer>
#include <QString>

#include <curl/curl.h>
#include <functional>
#include <optional>

namespace QCurl {

/**
 * @brief 为活动传输持有稳定的回调上下文与独占 easy handle。
 *
 * record 在 add 前创建，直到 detach 并解除回调后才归还 handle。
 * reply 仅作为弱观察者；共享状态覆盖原生回调和选项借用存储的使用期。
 */
class Q_DECL_HIDDEN QCCurlMultiTransferRecord final
{
public:
    // 有效 handle 已 detach 并解除 record 回调；无效 handle 仅通知不可恢复的隔离失败。
    using CompletionHandler
        = std::function<void(QCCurlHandleManager &&handle, CURLcode result, long httpStatus)>;
    using PersistentCompletionHandler
        = std::function<void(quintptr token, CURLcode result, long httpStatus)>;

    explicit QCCurlMultiTransferRecord(QCCurlHandleManager &&handle);
    ~QCCurlMultiTransferRecord();

    QCCurlMultiTransferRecord(const QCCurlMultiTransferRecord &)            = delete;
    QCCurlMultiTransferRecord &operator=(const QCCurlMultiTransferRecord &) = delete;

    [[nodiscard]] CURL *handle() const noexcept;
    [[nodiscard]] QCCurlHandleManager takeHandle() noexcept;
    void setToken(quintptr token) noexcept;
    [[nodiscard]] quintptr token() const noexcept;

    void bindDomain(std::shared_ptr<QCCurlMultiDomain> domain) noexcept;
    [[nodiscard]] const std::shared_ptr<QCCurlMultiDomain> &domain() const noexcept;

    void bindObserver(const QSharedPointer<QCNetworkReplyTransferState> &state,
                      QCNetworkReply *observer) noexcept;
    void clearObserver() noexcept;
    [[nodiscard]] QCNetworkReply *observer() const noexcept;
    /// 返回与 reply 共享的状态；只允许所属线程读取或修改其中的原生借用数据。
    [[nodiscard]] const QSharedPointer<QCNetworkReplyTransferState> &transferState() const noexcept;
    /// add 成功提交后发布非 owning handle 观察值。
    void publishActiveHandle() noexcept;
    /// detach 成功或进入隔离时撤销观察值，不改变 handle 的所有权。
    void clearActiveHandle() noexcept;

    void bindCompletionHandler(CompletionHandler handler);
    [[nodiscard]] bool hasCompletionHandler() const noexcept;
    [[nodiscard]] CompletionHandler takeCompletionHandler() noexcept;

    void bindPersistentCompletionHandler(PersistentCompletionHandler handler);
    [[nodiscard]] bool hasPersistentCompletionHandler() const noexcept;
    [[nodiscard]] PersistentCompletionHandler takePersistentCompletionHandler() noexcept;

    /// 从共享 state 读取配置，仅为实际启用的回调绑定本 record。
    [[nodiscard]] bool bindCallbacks(QString *errorMessage);
    /// detach 或安全回滚后解除回调，防止后续 easy cleanup 访问已释放的 record。
    [[nodiscard]] bool unbindCallbacks(QString *errorMessage);
    [[nodiscard]] bool bindPrivate(QString *errorMessage);

    static int prerequisiteCallback(void *userdata, char *, char *, int, int);
    static int socketOptionCallback(void *userdata, curl_socket_t socket, curlsocktype purpose);
    static size_t writeCallback(char *ptr, size_t size, size_t nmemb, void *userdata);
    static size_t headerCallback(char *ptr, size_t size, size_t nmemb, void *userdata);
    static size_t readCallback(char *ptr, size_t size, size_t nmemb, void *userdata);
    static int seekCallback(void *userdata, curl_off_t offset, int origin);
    static int progressCallback(
        void *userdata, curl_off_t dltotal, curl_off_t dlnow, curl_off_t ultotal, curl_off_t ulnow);
    static int debugCallback(
        CURL *handle, curl_infotype type, char *data, size_t size, void *userdata);

private:
    [[nodiscard]] bool bindReceiveIdleCallback(QString *errorMessage);
    std::optional<QCCurlHandleManager> m_handle;
    std::shared_ptr<QCCurlMultiDomain> m_domain;
    quintptr m_token = 0;
    QSharedPointer<QCNetworkReplyTransferState> m_transferState;
    QPointer<QCNetworkReply> m_observer;
    CompletionHandler m_completionHandler;
    PersistentCompletionHandler m_persistentCompletionHandler;
};

} // namespace QCurl

#endif // QCCURLMULTITRANSFERRECORD_P_H
