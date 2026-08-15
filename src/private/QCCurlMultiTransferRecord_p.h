/**
 * @file
 * @brief Owns an easy handle after it enters a curl multi handle.
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
 * @brief Stable callback userdata and owner for an active multi transfer.
 *
 * The record is created before `curl_multi_add_handle()` and remains the
 * callback userdata until the easy handle has been detached.  The reply is
 * only a fallible observer. Callback and option-backing state is owned by
 * this record independently of the observer lifetime.
 */
class Q_DECL_HIDDEN QCCurlMultiTransferRecord final
{
public:
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

    void bindObserver(const QSharedPointer<QCNetworkReplyTransferState> &state,
                      QCNetworkReply *observer) noexcept;
    void clearObserver() noexcept;
    [[nodiscard]] QCNetworkReply *observer() const noexcept;
    [[nodiscard]] bool ownsTransferState(
        const QSharedPointer<QCNetworkReplyTransferState> &state) const noexcept;

    void bindCompletionHandler(CompletionHandler handler);
    [[nodiscard]] bool hasCompletionHandler() const noexcept;
    [[nodiscard]] CompletionHandler takeCompletionHandler() noexcept;

    void bindPersistentCompletionHandler(PersistentCompletionHandler handler);
    [[nodiscard]] bool hasPersistentCompletionHandler() const noexcept;
    [[nodiscard]] PersistentCompletionHandler takePersistentCompletionHandler() noexcept;

    /**
     * @brief Install this record as every libcurl callback data pointer.
     *
     * Callback functions are configured while the reply still owns the easy
     * handle.  The data pointers are rebound immediately before multi add.
     */
    [[nodiscard]] bool bindCallbacks(bool readCallbackConfigured,
                                     bool debugCallbackConfigured,
                                     QString *errorMessage);
    [[nodiscard]] bool bindPrivate(QString *errorMessage);

    static size_t writeCallback(char *ptr, size_t size, size_t nmemb, void *userdata);
    static size_t headerCallback(char *ptr, size_t size, size_t nmemb, void *userdata);
    static size_t readCallback(char *ptr, size_t size, size_t nmemb, void *userdata);
    static int seekCallback(void *userdata, curl_off_t offset, int origin);
    static int progressCallback(
        void *userdata, curl_off_t dltotal, curl_off_t dlnow, curl_off_t ultotal, curl_off_t ulnow);
    static int debugCallback(
        CURL *handle, curl_infotype type, char *data, size_t size, void *userdata);

private:
    std::optional<QCCurlHandleManager> m_handle;
    quintptr m_token = 0;
    QSharedPointer<QCNetworkReplyTransferState> m_transferState;
    QPointer<QCNetworkReply> m_observer;
    CompletionHandler m_completionHandler;
    PersistentCompletionHandler m_persistentCompletionHandler;
};

} // namespace QCurl

#endif // QCCURLMULTITRANSFERRECORD_P_H
