/**
 * @file
 * @brief 在原有插点绑定认证、重定向、range 与代理策略。
 */
#include "private/QCNetworkLogRedaction_p.h"
#include "private/QCReplyCurlConfiguration_p.h"

#include <QDebug>

namespace QCurl::Internal {

bool QCReplyCurlConfiguration::configureHttpCredentials(CURL *easy,
                                                        const RequestHttpCredentials &credentials,
                                                        ReplyCurlConfigurationResult &result) const
{
    return requiredOption(easy,
                          QCURL_CURL_OPTION(CURLOPT_USERNAME),
                          credentials.user.constData(),
                          result)
           && requiredOption(easy,
                             QCURL_CURL_OPTION(CURLOPT_PASSWORD),
                             credentials.password.constData(),
                             result)
           && requiredOption(easy, QCURL_CURL_OPTION(CURLOPT_HTTPAUTH), credentials.method, result);
}

bool QCReplyCurlConfiguration::configureHttpAuthentication(
    CURL *easy, ReplyCurlConfigurationResult &result) const
{
    const auto &headers = requestPolicy().headers;
    if (m_policy.diagnostics.basicOverHttp) {
        const auto &request = m_plan.normalized.request;
        QString message;
        QDebug(&message) << "QCNetworkReply: Basic authentication over HTTP is insecure, consider "
                            "HTTPS. url="
                         << QCNetworkLogRedaction::redactUrl(request.url(),
                                                             request.diagnosticQueryPolicy());
        result.warnings.append({ReplyCurlWarningKind::LogOnly, message.trimmed()});
    }
    return !headers.credentials || configureHttpCredentials(easy, *headers.credentials, result);
}

bool QCReplyCurlConfiguration::configureSensitiveRedirectPolicy(
    CURL *easy, ReplyCurlConfigurationResult &result) const
{
    if (!requestPolicy().headers.unrestrictedAuth) {
        return true;
    }
    warn(result,
         QStringLiteral("安全风险：已启用跨站发送敏感头（CURLOPT_"
                        "UNRESTRICTED_AUTH=1），请确认重定向目标可信"));
    return requiredOption(easy,
                          QCURL_CURL_OPTION(CURLOPT_UNRESTRICTED_AUTH),
                          CurlOptions::kEnabled,
                          result);
}

bool QCReplyCurlConfiguration::configureRange(CURL *easy, ReplyCurlConfigurationResult &result) const
{
    const auto &range = requestPolicy().transfer.range;
    return !range
           || requiredOption(easy, QCURL_CURL_OPTION(CURLOPT_RANGE), range->constData(), result);
}

bool QCReplyCurlConfiguration::configureProxyEndpoint(CURL *easy,
                                                      const RequestProxyEndpoint &proxy,
                                                      ReplyCurlConfigurationResult &result) const
{
    return requiredOption(easy, QCURL_CURL_OPTION(CURLOPT_PROXY), proxy.host.constData(), result)
           && requiredOption(easy, QCURL_CURL_OPTION(CURLOPT_PROXYPORT), proxy.port, result)
           && requiredOption(easy, QCURL_CURL_OPTION(CURLOPT_PROXYTYPE), proxy.type, result)
           && requiredOption(easy, QCURL_CURL_OPTION(CURLOPT_NOPROXY), "", result);
}

bool QCReplyCurlConfiguration::configureProxyCredentials(CURL *easy,
                                                         const RequestProxyEndpoint &proxy,
                                                         ReplyCurlConfigurationResult &result) const
{
    return (!proxy.user
            || requiredOption(easy,
                              QCURL_CURL_OPTION(CURLOPT_PROXYUSERNAME),
                              proxy.user->constData(),
                              result))
           && (!proxy.password
               || requiredOption(easy,
                                 QCURL_CURL_OPTION(CURLOPT_PROXYPASSWORD),
                                 proxy.password->constData(),
                                 result))
           && (!proxy.negotiateHttpAuth
               || requiredOption(easy,
                                 QCURL_CURL_OPTION(CURLOPT_PROXYAUTH),
                                 static_cast<long>(CURLAUTH_ANY),
                                 result));
}

bool QCReplyCurlConfiguration::configureProxy(CURL *easy,
                                              std::optional<ProxyTlsInput> &proxyTls,
                                              ReplyCurlConfigurationResult &result) const
{
    const auto &proxy = requestPolicy().proxy;
    if (!proxy) {
        return requiredOption(easy, QCURL_CURL_OPTION(CURLOPT_PROXY), "", result);
    }
    if (!configureProxyEndpoint(easy, *proxy, result)
        || !configureProxyCredentials(easy, *proxy, result)) {
        return false;
    }
    proxyTls = ProxyTlsInput{proxy->requestedType,
                             proxy->type,
                             m_plan.normalized.request.proxyConfig()->tlsConfig()};
    return true;
}

ReplyCurlConfigurationResult QCReplyCurlConfiguration::applyAuthenticationAndProxy(
    CURL *easy, std::optional<ProxyTlsInput> &proxyTls)
{
    ReplyCurlConfigurationResult result;
    proxyTls.reset();
    const bool configured = configureHttpAuthentication(easy, result)
                            && configureSensitiveRedirectPolicy(easy, result)
                            && configureRange(easy, result)
                            && configureProxy(easy, proxyTls, result);
    Q_UNUSED(configured);
    return result;
}

} // namespace QCurl::Internal
