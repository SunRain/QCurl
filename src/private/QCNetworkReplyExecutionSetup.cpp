/**
 * @file
 * @brief 在原执行检查点借用 manager 配置、注册 trace 并回卷请求体。
 */

#include "QCNetworkAccessManager.h"
#include "QCNetworkReply_p.h"
#include "private/QCNetworkReplyConfigurationExecution_p.h"
#include "private/QCNetworkReplyExecution_p.h"

namespace QCurl::Internal {
namespace {

bool configureHttpStateFiles(QCNetworkReplyPrivate *reply,
                             CURL *easy,
                             const QCNetworkAccessManager *manager)
{
    if (!manager) {
        return true;
    }
    const auto config = manager->hstsAltSvcCacheConfig();
    return publishReplyCurlConfigurationResult(reply,
                                               reply->transferState->configuration
                                                   .applyHttpStateFiles(easy,
                                                                        config.hstsFilePath(),
                                                                        config.altSvcFilePath()));
}

bool configureDebugTrace(QCNetworkReplyPrivate *reply, CURL *easy)
{
    if (!easy || !reply->debugTraceEnabled || !reply->logger) {
        return true;
    }
    const CURLcode code = CurlOptions::setVerbose(easy, true);
    if (code != CURLE_OK) {
        reply->setError(NetworkError::InvalidRequest,
                        QStringLiteral("设置 CURLOPT_VERBOSE 失败（%1）")
                            .arg(QString::fromUtf8(curl_easy_strerror(code))));
        return false;
    }
    if (!setRequiredReplyCallbackOption(reply,
                                        easy,
                                        QCURL_CURL_OPTION(CURLOPT_DEBUGFUNCTION),
                                        QCNetworkReplyPrivate::curlDebugCallback)
        || !setRequiredReplyCallbackOption(reply,
                                           easy,
                                           QCURL_CURL_OPTION(CURLOPT_DEBUGDATA),
                                           reply)) {
        return false;
    }
    reply->transferState->debugCallbackConfigured = true;
    return true;
}

bool rewindBodySource(QCNetworkReplyPrivate *reply)
{
    auto &source = reply->transferState->bodySource;
    source.clearFailure();
    QString error;
    if (reply->retry.attemptCount() <= 0 || source.rewindForRetry(&error)) {
        return true;
    }
    reply->setError(NetworkError::InvalidRequest, error);
    return false;
}

} // namespace

bool QCNetworkReplyExecution::prepareNetwork(QCNetworkReply *reply, QCNetworkAccessManager *manager)
{
    auto *d             = reply->d_func();
    auto &configuration = d->transferState->configuration;
    CURL *easy          = d->curlManager.handle();
    if (manager) {
        configuration.inheritCookieFile(static_cast<int>(manager->cookieFileMode()),
                                        manager->cookieFilePath());
    }
    prepareCacheCollection(reply, manager);
    const bool configured = publishReplyCurlConfigurationResult(d,
                                                                configuration.applyCookieFile(easy))
                            && configureHttpStateFiles(d, easy, manager)
                            && configureDebugTrace(d, easy) && rewindBodySource(d);
    if (!configured) {
        Q_UNUSED(d->setState(ReplyState::Error));
    }
    return configured;
}

} // namespace QCurl::Internal
