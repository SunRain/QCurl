/**
 * @file
 * @brief QCNetworkReply authentication, proxy, and HTTP-version curl options.
 */

#include "CurlFeatureProbe.h"
#include "QCNetworkConnectionPoolManager_p.h"
#include "QCNetworkError.h"
#include "QCNetworkHttpVersion.h"
#include "QCNetworkProxyConfig.h"
#include "QCNetworkReply_p.h"
#include "QCNetworkRequest.h"
#include "QCNetworkRetryPolicy.h"
#include "QCNetworkSslConfig.h"
#include "QCNetworkTimeoutConfig.h"
#include "private/QCNetworkHttpVersion_p.h"
#include "private/QCNetworkLogRedaction_p.h"
#include "private/QCNetworkReplyCurlOptions_p.h"
#include "private/QCNetworkReplyRuntime_p.h"

#include <QDebug>
#include <QFileInfo>
#include <QIODevice>
#include <QUrl>

#include <limits>
#include <optional>

namespace QCurl::Internal::ReplyCurlOptions {

using namespace Detail;

namespace {

unsigned long curlHttpAuthMethod(QCNetworkHttpAuthMethod method)
{
    switch (method) {
        case QCNetworkHttpAuthMethod::Basic:
            return CURLAUTH_BASIC;
        case QCNetworkHttpAuthMethod::Any:
            return CURLAUTH_ANY;
        case QCNetworkHttpAuthMethod::AnySafe:
            return CURLAUTH_ANYSAFE;
    }
    return CURLAUTH_BASIC;
}

[[nodiscard]] bool configureHttpCredentials(QCNetworkReplyPrivate *reply,
                                            CURL *handle,
                                            const QCNetworkHttpAuthConfig &config)
{
    reply->httpAuthUserBytes     = config.userName().toUtf8();
    reply->httpAuthPasswordBytes = config.password().toUtf8();
    return setRequiredCurlOption(reply,
                                 handle,
                                 QCURL_CURL_OPTION(CURLOPT_USERNAME),
                                 reply->httpAuthUserBytes.constData())
           && setRequiredCurlOption(reply,
                                    handle,
                                    QCURL_CURL_OPTION(CURLOPT_PASSWORD),
                                    reply->httpAuthPasswordBytes.constData())
           && setRequiredCurlOption(reply,
                                    handle,
                                    QCURL_CURL_OPTION(CURLOPT_HTTPAUTH),
                                    curlHttpAuthMethod(config.method()));
}

[[nodiscard]] bool configureHttpAuthentication(QCNetworkReplyPrivate *reply,
                                               CURL *handle,
                                               const QCNetworkRequest &request,
                                               bool hasExplicitAuthorization,
                                               bool *hasSensitiveHeader,
                                               bool *wantsUnrestrictedHeaders)
{
    const auto auth = request.httpAuth();
    if (!auth.has_value()) {
        return true;
    }
    const auto &config = auth.value();
    if (config.method() == QCNetworkHttpAuthMethod::Basic && config.warnIfBasicOverHttp()
        && request.url().scheme().compare(QStringLiteral("http"), Qt::CaseInsensitive) == 0) {
        qWarning() << "QCNetworkReply: Basic authentication over HTTP is insecure, consider "
                      "HTTPS. url="
                   << QCNetworkLogRedaction::redactUrl(request.url());
    }
    if (hasExplicitAuthorization) {
        return true;
    }

    *hasSensitiveHeader = true;
    if (config.allowUnrestrictedAuth() && request.followLocation()) {
        *wantsUnrestrictedHeaders = true;
    }
    return configureHttpCredentials(reply, handle, config);
}

void configureSensitiveRedirectPolicy(QCNetworkReplyPrivate *reply,
                                      CURL *handle,
                                      const QCNetworkRequest &request,
                                      bool hasSensitiveHeader,
                                      bool wantsUnrestrictedHeaders)
{
    if (!request.followLocation() || !wantsUnrestrictedHeaders || !hasSensitiveHeader) {
        return;
    }

    appendCapabilityWarning(reply,
                            QStringLiteral("安全风险：已启用跨站发送敏感头（CURLOPT_"
                                           "UNRESTRICTED_AUTH=1），请确认重定向目标可信"));
    setOptionalLongOption(reply,
                          handle,
                          QCURL_CURL_OPTION(CURLOPT_UNRESTRICTED_AUTH),
                          Internal::CurlOptions::kEnabled);
}

[[nodiscard]] bool configureRange(QCNetworkReplyPrivate *reply,
                                  CURL *handle,
                                  const QCNetworkRequest &request)
{
    if (request.rangeStart() < 0 || request.rangeEnd() <= request.rangeStart()) {
        return true;
    }

    const QByteArray range
        = QStringLiteral("%1-%2").arg(request.rangeStart()).arg(request.rangeEnd()).toUtf8();
    return setRequiredCurlOption(reply, handle, QCURL_CURL_OPTION(CURLOPT_RANGE), range.constData());
}

[[nodiscard]] bool resolveCurlProxyType(QCNetworkReplyPrivate *reply,
                                        const QCNetworkProxyConfig &config,
                                        long *curlProxyType)
{
#ifdef CURLPROXY_HTTPS
    Q_UNUSED(reply)
#endif
    switch (config.type()) {
        case QCNetworkProxyConfig::ProxyType::Http:
            *curlProxyType = CURLPROXY_HTTP;
            return true;
        case QCNetworkProxyConfig::ProxyType::Https:
#ifdef CURLPROXY_HTTPS
            *curlProxyType = CURLPROXY_HTTPS;
            return true;
#else
            if (const auto tls = config.tlsConfig();
                tls.has_value()
                && tls->unsupportedSecurityPolicy() == QCUnsupportedSecurityOptionPolicy::Fail) {
                reply->setError(
                    NetworkError::InvalidRequest,
                    QStringLiteral(
                        "当前构建的 libcurl 不支持 HTTPS 代理（CURLPROXY_HTTPS 未定义）"));
                return false;
            }
            *curlProxyType = CURLPROXY_HTTP;
            appendCapabilityWarning(reply,
                                    QStringLiteral(
                                        "当前构建的 libcurl 不支持 HTTPS 代理（CURLPROXY_HTTPS "
                                        "未定义），已按 HTTP 代理处理"));
            return true;
#endif
        case QCNetworkProxyConfig::ProxyType::Socks4:
            *curlProxyType = CURLPROXY_SOCKS4;
            return true;
        case QCNetworkProxyConfig::ProxyType::Socks4A:
            *curlProxyType = CURLPROXY_SOCKS4A;
            return true;
        case QCNetworkProxyConfig::ProxyType::Socks5:
            *curlProxyType = CURLPROXY_SOCKS5;
            return true;
        case QCNetworkProxyConfig::ProxyType::Socks5Hostname:
            *curlProxyType = CURLPROXY_SOCKS5_HOSTNAME;
            return true;
        case QCNetworkProxyConfig::ProxyType::None:
            *curlProxyType = CURLPROXY_HTTP;
            return true;
    }
    *curlProxyType = CURLPROXY_HTTP;
    return true;
}

[[nodiscard]] bool configureProxyEndpoint(QCNetworkReplyPrivate *reply,
                                          CURL *handle,
                                          const QCNetworkProxyConfig &config,
                                          long *curlProxyType)
{
    reply->proxyEnvironmentDisabled = false;
    reply->proxyHostBytes           = config.hostName().toUtf8();
    if (!setRequiredCurlOption(reply,
                               handle,
                               QCURL_CURL_OPTION(CURLOPT_PROXY),
                               reply->proxyHostBytes.constData())) {
        return false;
    }
    if (config.port() > 0
        && !setRequiredCurlOption(reply,
                                  handle,
                                  QCURL_CURL_OPTION(CURLOPT_PROXYPORT),
                                  static_cast<long>(config.port()))) {
        return false;
    }
    return resolveCurlProxyType(reply, config, curlProxyType)
           && setRequiredCurlOption(reply,
                                    handle,
                                    QCURL_CURL_OPTION(CURLOPT_PROXYTYPE),
                                    *curlProxyType);
}

[[nodiscard]] bool configureProxyCredentials(QCNetworkReplyPrivate *reply,
                                             CURL *handle,
                                             const QCNetworkProxyConfig &config)
{
    reply->proxyUserBytes     = config.userName().toUtf8();
    reply->proxyPasswordBytes = config.password().toUtf8();
    if (!config.userName().isEmpty()
        && !setRequiredCurlOption(reply,
                                  handle,
                                  QCURL_CURL_OPTION(CURLOPT_PROXYUSERNAME),
                                  reply->proxyUserBytes.constData())) {
        return false;
    }
    if (!config.password().isEmpty()
        && !setRequiredCurlOption(reply,
                                  handle,
                                  QCURL_CURL_OPTION(CURLOPT_PROXYPASSWORD),
                                  reply->proxyPasswordBytes.constData())) {
        return false;
    }
    return (config.userName().isEmpty() && config.password().isEmpty())
           || setRequiredCurlOption(reply,
                                    handle,
                                    QCURL_CURL_OPTION(CURLOPT_PROXYAUTH),
                                    CURLAUTH_ANY);
}

[[nodiscard]] bool configureValidProxy(QCNetworkReplyPrivate *reply,
                                       CURL *handle,
                                       const QCNetworkProxyConfig &config)
{
    long curlProxyType = CURLPROXY_HTTP;
    return configureProxyEndpoint(reply, handle, config, &curlProxyType)
           && configureProxyCredentials(reply, handle, config)
           && configureReplyProxyTlsOptions(reply, handle, config, curlProxyType);
}

[[nodiscard]] bool configureProxy(QCNetworkReplyPrivate *reply,
                                  CURL *handle,
                                  const QCNetworkRequest &request)
{
    const auto proxy = request.proxyConfig();
    if (!proxy.has_value() || proxy->type() == QCNetworkProxyConfig::ProxyType::None) {
        return disableProxyEnvironmentInheritance(reply, handle);
    }
    if (!proxy->isValid()) {
        qWarning() << "QCNetworkReply: invalid proxy configuration ignored";
        return disableProxyEnvironmentInheritance(reply, handle);
    }
    return configureValidProxy(reply, handle, proxy.value());
}

} // namespace

[[nodiscard]] bool configureReplyProxyAndHttpVersion(QCNetworkReplyPrivate *reply,
                                                     CURL *handle,
                                                     const Internal::NormalizedRequest &normalized,
                                                     bool hasExplicitAuthorizationHeader,
                                                     bool hasSensitiveHeader)
{
    const QCNetworkRequest &request = normalized.request;
    bool wantsUnrestrictedHeaders   = request.allowUnrestrictedSensitiveHeadersOnRedirect();
    if (!configureHttpAuthentication(reply,
                                     handle,
                                     request,
                                     hasExplicitAuthorizationHeader,
                                     &hasSensitiveHeader,
                                     &wantsUnrestrictedHeaders)) {
        return false;
    }
    configureSensitiveRedirectPolicy(reply,
                                     handle,
                                     request,
                                     hasSensitiveHeader,
                                     wantsUnrestrictedHeaders);
    return configureRange(reply, handle, request) && configureProxy(reply, handle, request)
           && configureReplyHttpVersionOptions(reply, handle, request);
}

} // namespace QCurl::Internal::ReplyCurlOptions
