/**
 * @file
 * @brief Configures per-execution curl options and request body state.
 */

#include "QCNetworkAccessManager.h"
#include "QCNetworkReply.h"
#include "QCNetworkReply_p.h"
#include "private/QCCurlOptionAdapter_p.h"
#include "private/QCNetworkReplyBodySource_p.h"
#include "private/QCNetworkReplyExecution_p.h"

namespace QCurl::Internal {
namespace {

template<typename T>
[[nodiscard]] bool setRequiredExecutionOption(
    QCNetworkReplyPrivate *reply, CURL *handle, CURLoption option, const char *optionName, T value)
{
    const CURLcode code = CurlOptions::setWithTestHook(handle, option, optionName, value);
    if (code == CURLE_OK) {
        return true;
    }

    reply->setError(NetworkError::InvalidRequest,
                    QStringLiteral("设置 %1 失败（%2）")
                        .arg(QString::fromUtf8(optionName))
                        .arg(QString::fromUtf8(curl_easy_strerror(code))));
    return false;
}

void inheritManagerCookieConfig(QCNetworkReplyPrivate *reply, const QCNetworkAccessManager *manager)
{
    if (!manager || reply->cookieMode != 0 || !reply->cookieFilePath.isEmpty()) {
        return;
    }

    const auto mode = manager->cookieFileMode();
    const auto path = manager->cookieFilePath();
    if (mode != QCNetworkAccessManager::NotOpen && !path.isEmpty()) {
        reply->cookieMode     = static_cast<int>(mode);
        reply->cookieFilePath = path;
    }
}

[[nodiscard]] bool configureCookiePersistence(QCNetworkReplyPrivate *reply, CURL *handle)
{
    if (!handle || reply->cookieMode == 0 || reply->cookieFilePath.isEmpty()) {
        return true;
    }

    const QByteArray cookiePath = reply->cookieFilePath.toUtf8();
    const int readMode          = static_cast<int>(QCNetworkAccessManager::ReadOnly);
    const int writeMode         = static_cast<int>(QCNetworkAccessManager::WriteOnly);
    if ((reply->cookieMode & readMode)
        && !setRequiredExecutionOption(reply,
                                       handle,
                                       CURLOPT_COOKIEFILE,
                                       "CURLOPT_COOKIEFILE",
                                       cookiePath.constData())) {
        return false;
    }
    return !(reply->cookieMode & writeMode)
           || setRequiredExecutionOption(reply,
                                         handle,
                                         CURLOPT_COOKIEJAR,
                                         "CURLOPT_COOKIEJAR",
                                         cookiePath.constData());
}

[[nodiscard]] bool configureHttpStateFiles(QCNetworkReplyPrivate *reply,
                                           CURL *handle,
                                           const QCNetworkAccessManager *manager)
{
    if (!handle || !manager) {
        return true;
    }

    const auto config           = manager->hstsAltSvcCacheConfig();
    reply->hstsCachePathBytes   = config.hstsFilePath().toUtf8();
    reply->altSvcCachePathBytes = config.altSvcFilePath().toUtf8();
    if (!reply->hstsCachePathBytes.isEmpty()
        && !setRequiredExecutionOption(reply,
                                       handle,
                                       CURLOPT_HSTS,
                                       "CURLOPT_HSTS",
                                       reply->hstsCachePathBytes.constData())) {
        return false;
    }
    return reply->altSvcCachePathBytes.isEmpty()
           || setRequiredExecutionOption(reply,
                                         handle,
                                         CURLOPT_ALTSVC,
                                         "CURLOPT_ALTSVC",
                                         reply->altSvcCachePathBytes.constData());
}

[[nodiscard]] bool configureDebugTrace(QCNetworkReplyPrivate *reply, CURL *handle)
{
    if (!handle || !reply->debugTraceEnabled || !reply->logger) {
        return true;
    }

    const CURLcode result = CurlOptions::setVerbose(handle, true);
    if (result != CURLE_OK) {
        reply->setError(NetworkError::InvalidRequest,
                        QStringLiteral("设置 CURLOPT_VERBOSE 失败（%1）")
                            .arg(QString::fromUtf8(curl_easy_strerror(result))));
        return false;
    }
    if (!setRequiredExecutionOption(reply,
                                    handle,
                                    CURLOPT_DEBUGFUNCTION,
                                    "CURLOPT_DEBUGFUNCTION",
                                    QCNetworkReplyPrivate::curlDebugCallback)
        || !setRequiredExecutionOption(reply, handle, CURLOPT_DEBUGDATA, "CURLOPT_DEBUGDATA", reply)) {
        return false;
    }
    reply->debugCallbackConfigured = true;
    return true;
}

[[nodiscard]] bool prepareBodySource(QCNetworkReplyPrivate *reply)
{
    clearReplyBodySourceError(reply->requestBodySource);
    QString error;
    if (rewindReplyBodySourceForRetry(reply, &error)) {
        return true;
    }
    reply->setError(NetworkError::InvalidRequest, error);
    return false;
}

} // namespace

bool QCNetworkReplyExecution::prepareNetwork(QCNetworkReply *reply, QCNetworkAccessManager *manager)
{
    auto *d      = reply->d_func();
    CURL *handle = d->curlManager.handle();
    inheritManagerCookieConfig(d, manager);

    const bool configured = configureCookiePersistence(d, handle)
                            && configureHttpStateFiles(d, handle, manager)
                            && configureDebugTrace(d, handle) && prepareBodySource(d);
    if (!configured) {
        Q_UNUSED(d->setState(ReplyState::Error));
    }
    return configured;
}

} // namespace QCurl::Internal
