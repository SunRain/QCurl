/**
 * @file
 * @brief 维护活动 curl multi 传输的回调上下文与共享状态。
 */

#include "QCNetworkAccessManager.h"
#include "QCNetworkLogger.h"
#include "private/QCCurlMultiTransferRecord_p.h"
#include "private/QCCurlOptionAdapter_p.h"
#include "private/QCNetworkReplyCallbacks_p.h"

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
    // 即使仍持有未注册的 handle，cleanup 也必须先于 callback backing state 的析构。
    m_handle.reset();
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

void QCCurlMultiTransferRecord::bindDomain(std::shared_ptr<QCCurlMultiDomain> domain) noexcept
{
    m_domain = std::move(domain);
}

const std::shared_ptr<QCCurlMultiDomain> &QCCurlMultiTransferRecord::domain() const noexcept
{
    return m_domain;
}

void QCCurlMultiTransferRecord::bindObserver(
    const QSharedPointer<QCNetworkReplyTransferState> &state, QCNetworkReply *observer) noexcept
{
    m_transferState = state;
    m_observer      = observer;
    // detached handle 也借用 POSTFIELDS/RESOLVE 等状态，queued completion 不能先释放这些数据。
    m_handle->retainTransferState(state);
}

void QCCurlMultiTransferRecord::clearObserver() noexcept
{
    if (m_transferState) {
        m_transferState->receiveIdle.stop();
        m_transferState->trace.stop();
    }
    m_observer.clear();
}

QCNetworkReply *QCCurlMultiTransferRecord::observer() const noexcept
{
    return m_observer.data();
}

const QSharedPointer<QCNetworkReplyTransferState> &QCCurlMultiTransferRecord::transferState()
    const noexcept
{
    return m_transferState;
}

void QCCurlMultiTransferRecord::publishActiveHandle() noexcept
{
    if (m_transferState) {
        m_transferState->activeEasyHandle = handle();
    }
}

void QCCurlMultiTransferRecord::clearActiveHandle() noexcept
{
    if (m_transferState) {
        m_transferState->receiveIdle.stop();
        m_transferState->activeEasyHandle = nullptr;
    }
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

bool QCCurlMultiTransferRecord::bindCallbacks(QString *errorMessage)
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

    if (m_transferState->readCallbackConfigured
        && (!setOption(QCURL_CURL_OPTION(CURLOPT_READFUNCTION),
                       &QCCurlMultiTransferRecord::readCallback)
            || !setOption(QCURL_CURL_OPTION(CURLOPT_READDATA), static_cast<void *>(this)))) {
        return false;
    }

    if (m_transferState->debugCallbackConfigured
        && (!setOption(QCURL_CURL_OPTION(CURLOPT_DEBUGFUNCTION),
                       &QCCurlMultiTransferRecord::debugCallback)
            || !setOption(QCURL_CURL_OPTION(CURLOPT_DEBUGDATA), static_cast<void *>(this)))) {
        return false;
    }

    // detach 为 cleanup 禁用 progress；每次 admission 必须在回调绑定后重新启用。
    return setOption(QCURL_CURL_OPTION(CURLOPT_NOPROGRESS), Internal::CurlOptions::kDisabled)
           && bindReceiveIdleCallback(errorMessage) && bindPrivate(errorMessage);
}

bool QCCurlMultiTransferRecord::unbindCallbacks(QString *errorMessage)
{
    if (!handle() || !m_transferState) {
        return true;
    }

    // cleanup 也可能调用 progress/header/debug；先关闭函数，再解除全部 userdata。
    const auto setOption = [this, errorMessage](Internal::CurlOptions::Option option, auto value) {
        // setup 的故障注入不代表 rollback 也失败；回滚以原生返回码裁决。
        const CURLcode code = curl_easy_setopt(handle(), option.id, value);
        if (code != CURLE_OK && errorMessage) {
            *errorMessage = QStringLiteral("解除 %1 失败（%2）")
                                .arg(QString::fromUtf8(option.name),
                                     QString::fromUtf8(curl_easy_strerror(code)));
        }
        return code == CURLE_OK;
    };
    return setOption(QCURL_CURL_OPTION(CURLOPT_VERBOSE), Internal::CurlOptions::kDisabled)
           && setOption(QCURL_CURL_OPTION(CURLOPT_NOPROGRESS), Internal::CurlOptions::kEnabled)
           && setOption(QCURL_CURL_OPTION(CURLOPT_WRITEFUNCTION),
                        static_cast<curl_write_callback>(nullptr))
           && setOption(QCURL_CURL_OPTION(CURLOPT_HEADERFUNCTION),
                        static_cast<curl_write_callback>(nullptr))
           && setOption(QCURL_CURL_OPTION(CURLOPT_SEEKFUNCTION),
                        static_cast<curl_seek_callback>(nullptr))
           && setOption(QCURL_CURL_OPTION(CURLOPT_XFERINFOFUNCTION),
                        static_cast<curl_xferinfo_callback>(nullptr))
           && setOption(QCURL_CURL_OPTION(CURLOPT_READFUNCTION),
                        static_cast<curl_read_callback>(nullptr))
           && setOption(QCURL_CURL_OPTION(CURLOPT_DEBUGFUNCTION),
                        static_cast<curl_debug_callback>(nullptr))
           && setOption(QCURL_CURL_OPTION(CURLOPT_PREREQFUNCTION),
                        static_cast<curl_prereq_callback>(nullptr))
           && setOption(QCURL_CURL_OPTION(CURLOPT_PREREQDATA), nullptr)
           && setOption(QCURL_CURL_OPTION(CURLOPT_SOCKOPTFUNCTION),
                        static_cast<curl_sockopt_callback>(nullptr))
           && setOption(QCURL_CURL_OPTION(CURLOPT_SOCKOPTDATA), nullptr)
           && setOption(QCURL_CURL_OPTION(CURLOPT_WRITEDATA), nullptr)
           && setOption(QCURL_CURL_OPTION(CURLOPT_HEADERDATA), nullptr)
           && setOption(QCURL_CURL_OPTION(CURLOPT_SEEKDATA), nullptr)
           && setOption(QCURL_CURL_OPTION(CURLOPT_XFERINFODATA), nullptr)
           && setOption(QCURL_CURL_OPTION(CURLOPT_READDATA), nullptr)
           && setOption(QCURL_CURL_OPTION(CURLOPT_DEBUGDATA), nullptr)
           && setOption(QCURL_CURL_OPTION(CURLOPT_PRIVATE), nullptr);
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
    auto *record = static_cast<QCCurlMultiTransferRecord *>(userdata);
    auto *state  = record ? record->m_transferState.data() : nullptr;
    if (!state) {
        return 0;
    }
    if (state->lifecycle.isTerminal()) {
        return 0;
    }

    if (!state->debugTraceEnabled) {
        return 0;
    }

    const auto logger = state->logger;
    if (!logger) {
        return 0;
    }

    const auto callback
        = Internal::inspectTraceCallback(handle,
                                         record->handle(),
                                         QByteArrayView(data, static_cast<qsizetype>(size)));
    const QString message = state->trace.consume(type, callback);
    if (!message.isEmpty()) {
        static_cast<void>(logger->log(NetworkLogLevel::Debug, QStringLiteral("Trace"), message));
    }
    return 0;
}

} // namespace QCurl
