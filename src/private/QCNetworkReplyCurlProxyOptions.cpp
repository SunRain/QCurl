/**
 * @file
 * @brief 应用认证、敏感头重定向策略与 proxy endpoint/credentials。
 */

#include "private/QCNetworkLogRedaction_p.h"
#include "private/QCReplyCurlConfiguration_p.h"

#include <QDebug>

namespace QCurl::Internal {
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

long curlProxyType(QCNetworkProxyConfig::ProxyType type)
{
    switch (type) {
        case QCNetworkProxyConfig::ProxyType::Https:
            return CURLPROXY_HTTPS;
        case QCNetworkProxyConfig::ProxyType::Socks4:
            return CURLPROXY_SOCKS4;
        case QCNetworkProxyConfig::ProxyType::Socks4A:
            return CURLPROXY_SOCKS4A;
        case QCNetworkProxyConfig::ProxyType::Socks5:
            return CURLPROXY_SOCKS5;
        case QCNetworkProxyConfig::ProxyType::Socks5Hostname:
            return CURLPROXY_SOCKS5_HOSTNAME;
        case QCNetworkProxyConfig::ProxyType::Http:
        case QCNetworkProxyConfig::ProxyType::None:
            return CURLPROXY_HTTP;
    }
    return CURLPROXY_HTTP;
}

} // namespace

bool QCReplyCurlConfiguration::configureHttpCredentials(CURL *easy,
                                                        const QCNetworkHttpAuthConfig &config,
                                                        ReplyCurlConfigurationResult &result)
{
    m_httpAuthUserBytes     = config.userName().toUtf8();
    m_httpAuthPasswordBytes = config.password().toUtf8();
    return requiredOption(easy,
                          QCURL_CURL_OPTION(CURLOPT_USERNAME),
                          m_httpAuthUserBytes.constData(),
                          result)
           && requiredOption(easy,
                             QCURL_CURL_OPTION(CURLOPT_PASSWORD),
                             m_httpAuthPasswordBytes.constData(),
                             result)
           && requiredOption(easy,
                             QCURL_CURL_OPTION(CURLOPT_HTTPAUTH),
                             curlHttpAuthMethod(config.method()),
                             result);
}

bool QCReplyCurlConfiguration::configureHttpAuthentication(CURL *easy,
                                                           bool hasExplicitAuthorization,
                                                           bool &hasSensitiveHeader,
                                                           bool &wantsUnrestrictedHeaders,
                                                           ReplyCurlConfigurationResult &result)
{
    const auto &request = m_plan.normalized.request;
    const auto auth     = request.httpAuth();
    if (!auth) {
        return true;
    }
    if (auth->method() == QCNetworkHttpAuthMethod::Basic && auth->warnIfBasicOverHttp()
        && request.url().scheme().compare(QStringLiteral("http"), Qt::CaseInsensitive) == 0) {
        QString message;
        QDebug(&message) << "QCNetworkReply: Basic authentication over HTTP is insecure, consider "
                            "HTTPS. url="
                         << QCNetworkLogRedaction::redactUrl(request.url(),
                                                             request.diagnosticQueryPolicy());
        result.warnings.append({ReplyCurlWarningKind::LogOnly, message.trimmed()});
    }
    if (hasExplicitAuthorization) {
        return true;
    }
    hasSensitiveHeader = true;
    if (auth->allowUnrestrictedAuth() && request.followLocation()) {
        wantsUnrestrictedHeaders = true;
    }
    return configureHttpCredentials(easy, *auth, result);
}

void QCReplyCurlConfiguration::configureSensitiveRedirectPolicy(
    CURL *easy,
    bool hasSensitiveHeader,
    bool wantsUnrestrictedHeaders,
    ReplyCurlConfigurationResult &result) const
{
    const auto &request = m_plan.normalized.request;
    if (!request.followLocation() || !wantsUnrestrictedHeaders || !hasSensitiveHeader) {
        return;
    }
    warn(result,
         QStringLiteral("安全风险：已启用跨站发送敏感头（CURLOPT_"
                        "UNRESTRICTED_AUTH=1），请确认重定向目标可信"));
    optionalOption(easy,
                   QCURL_CURL_OPTION(CURLOPT_UNRESTRICTED_AUTH),
                   CurlOptions::kEnabled,
                   result);
}

bool QCReplyCurlConfiguration::configureRange(CURL *easy, ReplyCurlConfigurationResult &result) const
{
    const auto &request = m_plan.normalized.request;
    if (request.rangeStart() < 0 || request.rangeEnd() <= request.rangeStart()) {
        return true;
    }
    const QByteArray range
        = QStringLiteral("%1-%2").arg(request.rangeStart()).arg(request.rangeEnd()).toUtf8();
    return requiredOption(easy, QCURL_CURL_OPTION(CURLOPT_RANGE), range.constData(), result);
}

bool QCReplyCurlConfiguration::configureProxyEndpoint(CURL *easy,
                                                      const QCNetworkProxyConfig &config,
                                                      long &effectiveType,
                                                      ReplyCurlConfigurationResult &result)
{
    m_proxyHostBytes = config.hostName().toUtf8();
    if (!requiredOption(easy,
                        QCURL_CURL_OPTION(CURLOPT_PROXY),
                        m_proxyHostBytes.constData(),
                        result)) {
        return false;
    }
    if (config.port() > 0
        && !requiredOption(easy,
                           QCURL_CURL_OPTION(CURLOPT_PROXYPORT),
                           static_cast<long>(config.port()),
                           result)) {
        return false;
    }
    effectiveType = curlProxyType(config.type());
    return requiredOption(easy, QCURL_CURL_OPTION(CURLOPT_PROXYTYPE), effectiveType, result);
}

bool QCReplyCurlConfiguration::configureProxyCredentials(CURL *easy,
                                                         const QCNetworkProxyConfig &config,
                                                         ReplyCurlConfigurationResult &result)
{
    m_proxyUserBytes     = config.userName().toUtf8();
    m_proxyPasswordBytes = config.password().toUtf8();
    if (!config.userName().isEmpty()
        && !requiredOption(easy,
                           QCURL_CURL_OPTION(CURLOPT_PROXYUSERNAME),
                           m_proxyUserBytes.constData(),
                           result)) {
        return false;
    }
    if (!config.password().isEmpty()
        && !requiredOption(easy,
                           QCURL_CURL_OPTION(CURLOPT_PROXYPASSWORD),
                           m_proxyPasswordBytes.constData(),
                           result)) {
        return false;
    }
    return (config.userName().isEmpty() && config.password().isEmpty())
           || requiredOption(easy, QCURL_CURL_OPTION(CURLOPT_PROXYAUTH), CURLAUTH_ANY, result);
}

bool QCReplyCurlConfiguration::configureProxy(CURL *easy,
                                              std::optional<ProxyTlsInput> &proxyTls,
                                              ReplyCurlConfigurationResult &result)
{
    const auto proxy = m_plan.normalized.request.proxyConfig();
    if (proxy && proxy->type() != QCNetworkProxyConfig::ProxyType::None && proxy->isValid()) {
        long effectiveType = CURLPROXY_HTTP;
        if (!configureProxyEndpoint(easy, *proxy, effectiveType, result)
            || !configureProxyCredentials(easy, *proxy, result)) {
            return false;
        }
        proxyTls = ProxyTlsInput{proxy->type(), effectiveType, proxy->tlsConfig()};
        return true;
    }
    if (proxy && proxy->type() != QCNetworkProxyConfig::ProxyType::None && !proxy->isValid()) {
        result.warnings.append(
            {ReplyCurlWarningKind::LogOnly,
             QStringLiteral("QCNetworkReply: invalid proxy configuration ignored")});
    }
    m_proxyHostBytes.clear();
    m_proxyUserBytes.clear();
    m_proxyPasswordBytes.clear();
    return requiredOption(easy, QCURL_CURL_OPTION(CURLOPT_PROXY), "", result);
}

ReplyCurlConfigurationResult QCReplyCurlConfiguration::applyAuthenticationAndProxy(
    CURL *easy, const ReplyCurlHeaderFacts &facts, std::optional<ProxyTlsInput> &proxyTls)
{
    ReplyCurlConfigurationResult result;
    proxyTls.reset();
    bool sensitive    = facts.hasSensitive;
    bool unrestricted = m_plan.normalized.request.allowUnrestrictedSensitiveHeadersOnRedirect();
    if (!configureHttpAuthentication(easy,
                                     facts.hasExplicitAuthorization,
                                     sensitive,
                                     unrestricted,
                                     result)) {
        return result;
    }
    configureSensitiveRedirectPolicy(easy, sensitive, unrestricted, result);
    if (configureRange(easy, result)) {
        Q_UNUSED(configureProxy(easy, proxyTls, result));
    }
    return result;
}

} // namespace QCurl::Internal
