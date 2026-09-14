/**
 * @file
 * @brief Stable callback userdata for active curl multi transfers.
 */

#include "QCNetworkAccessManager.h"
#include "QCNetworkLogger.h"
#include "private/QCCurlMultiTransferRecord_p.h"
#include "private/QCCurlOptionAdapter_p.h"
#include "private/QCNetworkReplyBodySource_p.h"
#include "private/QCNetworkReplyCallbacks_p.h"
#include "private/QCNetworkReplyResponse_p.h"

#include <QDebug>

namespace QCurl {

namespace {

template<typename T>
[[nodiscard]] bool setRecordCallbackOption(CURL *easy,
                                           QString *errorMessage,
                                           QCurl::Internal::CurlOptions::Option option,
                                           T value)
{
    const CURLcode code = Internal::CurlOptions::setWithTestHook(easy, option, value);
    if (code == CURLE_OK) {
        return true;
    }
    if (errorMessage) {
        *errorMessage = QStringLiteral("设置 %1 失败（%2）")
                            .arg(QString::fromUtf8(option.name))
                            .arg(QString::fromUtf8(curl_easy_strerror(code)));
    }
    return false;
}

} // namespace

QCCurlMultiTransferRecord::QCCurlMultiTransferRecord(QCCurlHandleManager &&handle)
{
    m_handle.emplace(std::move(handle));
}

QCCurlMultiTransferRecord::~QCCurlMultiTransferRecord()
{
    clearObserver();
}

CURL *QCCurlMultiTransferRecord::handle() const noexcept
{
    return m_handle.has_value() ? m_handle->handle() : nullptr;
}

QCCurlHandleManager QCCurlMultiTransferRecord::takeHandle() noexcept
{
    return std::move(m_handle.value());
}

void QCCurlMultiTransferRecord::setToken(quintptr token) noexcept
{
    m_token = token;
}

quintptr QCCurlMultiTransferRecord::token() const noexcept
{
    return m_token;
}

void QCCurlMultiTransferRecord::bindObserver(
    const QSharedPointer<QCNetworkReplyTransferState> &state, QCNetworkReply *observer) noexcept
{
    m_transferState = state;
    m_observer      = observer;
}

void QCCurlMultiTransferRecord::clearObserver() noexcept
{
    m_observer.clear();
}

QCNetworkReply *QCCurlMultiTransferRecord::observer() const noexcept
{
    return m_observer.data();
}

bool QCCurlMultiTransferRecord::ownsTransferState(
    const QSharedPointer<QCNetworkReplyTransferState> &state) const noexcept
{
    return m_transferState && m_transferState.data() == state.data();
}

void QCCurlMultiTransferRecord::bindCompletionHandler(CompletionHandler handler)
{
    m_completionHandler = std::move(handler);
}

bool QCCurlMultiTransferRecord::hasCompletionHandler() const noexcept
{
    return static_cast<bool>(m_completionHandler);
}

QCCurlMultiTransferRecord::CompletionHandler QCCurlMultiTransferRecord::takeCompletionHandler() noexcept
{
    return std::move(m_completionHandler);
}

void QCCurlMultiTransferRecord::bindPersistentCompletionHandler(PersistentCompletionHandler handler)
{
    m_persistentCompletionHandler = std::move(handler);
}

bool QCCurlMultiTransferRecord::hasPersistentCompletionHandler() const noexcept
{
    return static_cast<bool>(m_persistentCompletionHandler);
}

QCCurlMultiTransferRecord::PersistentCompletionHandler
QCCurlMultiTransferRecord::takePersistentCompletionHandler() noexcept
{
    return std::move(m_persistentCompletionHandler);
}

bool QCCurlMultiTransferRecord::bindPrivate(QString *errorMessage)
{
    CURL *easy = handle();
    if (!easy) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("multi transfer record 的 easy handle 无效");
        }
        return false;
    }

    const CURLcode code = Internal::CurlOptions::setWithTestHook(easy,
                                                                 QCURL_CURL_OPTION(CURLOPT_PRIVATE),
                                                                 static_cast<void *>(this));
    if (code == CURLE_OK) {
        return true;
    }

    if (errorMessage) {
        *errorMessage = QStringLiteral("设置 CURLOPT_PRIVATE 失败（%1）")
                            .arg(QString::fromUtf8(curl_easy_strerror(code)));
    }
    return false;
}

bool QCCurlMultiTransferRecord::bindCallbacks(bool readCallbackConfigured,
                                              bool debugCallbackConfigured,
                                              QString *errorMessage)
{
    CURL *easy = handle();
    if (!easy || !m_transferState) {
        if (errorMessage) {
            *errorMessage = QStringLiteral(
                "multi transfer record 缺少 easy handle 或 transfer state");
        }
        return false;
    }

    auto setOption = [easy, errorMessage](QCurl::Internal::CurlOptions::Option option, auto value) {
        return setRecordCallbackOption(easy, errorMessage, option, value);
    };

    if (!setOption(QCURL_CURL_OPTION(CURLOPT_WRITEFUNCTION),
                   &QCCurlMultiTransferRecord::writeCallback)
        || !setOption(QCURL_CURL_OPTION(CURLOPT_WRITEDATA), static_cast<void *>(this))
        || !setOption(QCURL_CURL_OPTION(CURLOPT_HEADERFUNCTION),
                      &QCCurlMultiTransferRecord::headerCallback)
        || !setOption(QCURL_CURL_OPTION(CURLOPT_HEADERDATA), static_cast<void *>(this))
        || !setOption(QCURL_CURL_OPTION(CURLOPT_SEEKFUNCTION),
                      &QCCurlMultiTransferRecord::seekCallback)
        || !setOption(QCURL_CURL_OPTION(CURLOPT_SEEKDATA), static_cast<void *>(this))
        || !setOption(QCURL_CURL_OPTION(CURLOPT_XFERINFOFUNCTION),
                      &QCCurlMultiTransferRecord::progressCallback)
        || !setOption(QCURL_CURL_OPTION(CURLOPT_XFERINFODATA), static_cast<void *>(this))) {
        return false;
    }

    if (readCallbackConfigured
        && (!setOption(QCURL_CURL_OPTION(CURLOPT_READFUNCTION),
                       &QCCurlMultiTransferRecord::readCallback)
            || !setOption(QCURL_CURL_OPTION(CURLOPT_READDATA), static_cast<void *>(this)))) {
        return false;
    }

    if (debugCallbackConfigured
        && (!setOption(QCURL_CURL_OPTION(CURLOPT_DEBUGFUNCTION),
                       &QCCurlMultiTransferRecord::debugCallback)
            || !setOption(QCURL_CURL_OPTION(CURLOPT_DEBUGDATA), static_cast<void *>(this)))) {
        return false;
    }

    return bindPrivate(errorMessage);
}

size_t QCCurlMultiTransferRecord::writeCallback(char *ptr, size_t size, size_t nmemb, void *userdata)
{
    auto *record = static_cast<QCCurlMultiTransferRecord *>(userdata);
    if (!record) {
        return 0;
    }
    return Internal::writeReplyCurlCallback(ptr,
                                            size,
                                            nmemb,
                                            record->m_transferState.data(),
                                            record->m_observer,
                                            record->handle());
}

size_t QCCurlMultiTransferRecord::headerCallback(char *ptr,
                                                 size_t size,
                                                 size_t nmemb,
                                                 void *userdata)
{
    auto *record = static_cast<QCCurlMultiTransferRecord *>(userdata);
    return record
               ? Internal::headerReplyCurlCallback(ptr, size, nmemb, record->m_transferState.data())
               : 0;
}

size_t QCCurlMultiTransferRecord::readCallback(char *ptr, size_t size, size_t nmemb, void *userdata)
{
    Internal::ReplyCurlCallbackScope callbackScope;
    auto *record = static_cast<QCCurlMultiTransferRecord *>(userdata);
    if (!record) {
        return 0;
    }
    return Internal::readReplyBodySourceCallback(ptr,
                                                 size,
                                                 nmemb,
                                                 record->m_transferState.data(),
                                                 record->m_observer);
}

int QCCurlMultiTransferRecord::seekCallback(void *userdata, curl_off_t offset, int origin)
{
    auto *record = static_cast<QCCurlMultiTransferRecord *>(userdata);
    return record ? Internal::seekReplyBodySourceCallback(record->m_transferState.data(),
                                                          offset,
                                                          origin)
                  : CURL_SEEKFUNC_FAIL;
}

int QCCurlMultiTransferRecord::progressCallback(
    void *userdata, curl_off_t dltotal, curl_off_t dlnow, curl_off_t ultotal, curl_off_t ulnow)
{
    auto *record = static_cast<QCCurlMultiTransferRecord *>(userdata);
    return record ? Internal::progressReplyCurlCallback(record->m_transferState.data(),
                                                        record->m_observer,
                                                        dltotal,
                                                        dlnow,
                                                        ultotal,
                                                        ulnow)
                  : 1;
}

int QCCurlMultiTransferRecord::debugCallback(
    CURL *handle, curl_infotype type, char *data, size_t size, void *userdata)
{
    Q_UNUSED(handle);

    auto *record = static_cast<QCCurlMultiTransferRecord *>(userdata);
    auto *state  = record ? record->m_transferState.data() : nullptr;
    if (!state) {
        return 0;
    }
    if (state->state == ReplyState::Cancelled || state->state == ReplyState::Error) {
        return 0;
    }

    if (!state->debugTraceEnabled) {
        return 0;
    }

    const auto logger = state->logger;
    if (!logger) {
        return 0;
    }

    const QString message
        = Internal::formatReplyDebugTraceMessage(type, QByteArray(data, static_cast<int>(size)));
    if (!message.isEmpty()) {
        static_cast<void>(logger->log(NetworkLogLevel::Debug, QStringLiteral("Trace"), message));
    }
    return 0;
}

} // namespace QCurl
