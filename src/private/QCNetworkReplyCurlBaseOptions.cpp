/**
 * @file
 * @brief 应用请求 URL、重定向、网络路径与协议限制。
 */

#include "private/QCNetworkIpResolve_p.h"
#include "private/QCNetworkProtocolPolicy_p.h"
#include "private/QCReplyCurlConfiguration_p.h"

#include <QUrl>

namespace QCurl::Internal {
namespace {

std::optional<long> postRedirectValue(QCNetworkPostRedirectPolicy policy)
{
    switch (policy) {
        case QCNetworkPostRedirectPolicy::Default:
            return std::nullopt;
        case QCNetworkPostRedirectPolicy::KeepPost301:
            return CURL_REDIR_POST_301;
        case QCNetworkPostRedirectPolicy::KeepPost302:
            return CURL_REDIR_POST_302;
        case QCNetworkPostRedirectPolicy::KeepPost303:
            return CURL_REDIR_POST_303;
        case QCNetworkPostRedirectPolicy::KeepPostAll:
            return CURL_REDIR_POST_ALL;
    }
    return std::nullopt;
}

} // namespace

ReplyCurlConfigurationResult QCReplyCurlConfiguration::applyUrl(CURL *easy) const
{
    ReplyCurlConfigurationResult result;
    const auto &request = m_plan.normalized.request;
    QString error;
    if (!QCNetworkProtocolPolicy::validateCoreUrl(request.url(), &error)) {
        result.failure = error;
        return result;
    }
    const QByteArray bytes = request.url().toEncoded(QUrl::FullyEncoded);
    Q_UNUSED(requiredOption(easy, QCURL_CURL_OPTION(CURLOPT_URL), bytes.constData(), result));
    return result;
}

void QCReplyCurlConfiguration::configureRedirects(CURL *easy,
                                                  ReplyCurlConfigurationResult &result) const
{
    const auto &request = m_plan.normalized.request;
    if (!request.followLocation()) {
        return;
    }
    if (const auto limit = request.maxRedirects()) {
        optionalOption(easy,
                       QCURL_CURL_OPTION(CURLOPT_MAXREDIRS),
                       static_cast<long>(*limit),
                       result);
    }
    if (const auto value = postRedirectValue(request.postRedirectPolicy())) {
        optionalOption(easy, QCURL_CURL_OPTION(CURLOPT_POSTREDIR), *value, result);
    }
    if (request.autoRefererEnabled()) {
        optionalOption(easy, QCURL_CURL_OPTION(CURLOPT_AUTOREFERER), CurlOptions::kEnabled, result);
    }
}

bool QCReplyCurlConfiguration::configureSlist(CURL *easy,
                                              const QStringList &entries,
                                              CurlOptions::CurlSlistOwner &storage,
                                              CurlOptions::Option option,
                                              ReplyCurlConfigurationResult &result)
{
    QString error;
    curl_slist *list = nullptr;
    if (!CurlOptions::buildCurlSlist(entries, &list, &error, option.name)) {
        result.failure = error;
        return false;
    }
    CurlOptions::CurlSlistOwner candidate(list);
    if (!requiredOption(easy, option, candidate.get(), result)) {
        return false;
    }
    // nullptr 只解除本 easy 的列表，不删除 multi 域中已加载的 DNS 或连接。
    storage = std::move(candidate);
    return true;
}

#ifdef QCURL_ENABLE_ADVANCED_REQUEST_NETWORK_PATH_API

void QCReplyCurlConfiguration::configureLocalNetworkBinding(CURL *easy,
                                                            ReplyCurlConfigurationResult &result)
{
    const auto &request = m_plan.normalized.request;
    if (const auto timeout = request.happyEyeballsTimeout()) {
        optionalOption(easy,
                       QCURL_CURL_OPTION(CURLOPT_HAPPY_EYEBALLS_TIMEOUT_MS),
                       static_cast<long>(timeout->count()),
                       result);
    }
    if (const auto interface = request.networkInterface()) {
        m_interfaceBytes = interface->toUtf8();
        optionalOption(easy,
                       QCURL_CURL_OPTION(CURLOPT_INTERFACE),
                       m_interfaceBytes.constData(),
                       result);
    }
    if (const auto port = request.localPort()) {
        optionalOption(easy, QCURL_CURL_OPTION(CURLOPT_LOCALPORT), static_cast<long>(*port), result);
    }
    if (const auto range = request.localPortRange()) {
        optionalOption(easy,
                       QCURL_CURL_OPTION(CURLOPT_LOCALPORTRANGE),
                       static_cast<long>(*range),
                       result);
    }
}

void QCReplyCurlConfiguration::configureDnsTransport(CURL *easy,
                                                     ReplyCurlConfigurationResult &result)
{
    const auto &request = m_plan.normalized.request;
    if (const auto servers = request.dnsServers()) {
        m_dnsServersBytes = servers->join(QStringLiteral(",")).toUtf8();
        optionalOption(easy,
                       QCURL_CURL_OPTION(CURLOPT_DNS_SERVERS),
                       m_dnsServersBytes.constData(),
                       result);
    }
    if (const auto url = request.dohUrl()) {
        m_dohUrlBytes = url->toString().toUtf8();
        optionalOption(easy, QCURL_CURL_OPTION(CURLOPT_DOH_URL), m_dohUrlBytes.constData(), result);
    }
}

#endif

bool QCReplyCurlConfiguration::configureProtocolList(CURL *easy,
                                                     const QStringList &protocols,
                                                     QByteArray &storage,
                                                     CurlOptions::Option option,
                                                     const QString &unsupportedMessage,
                                                     ReplyCurlConfigurationResult &result)
{
    storage             = protocols.join(QStringLiteral(",")).toUtf8();
    const CURLcode code = CurlOptions::setWithTestHook(easy, option, storage.constData());
    if (CurlOptions::isCapabilityRelatedError(code)) {
        result.failure = unsupportedMessage.arg(QString::fromUtf8(curl_easy_strerror(code)));
        return false;
    }
    return required(code, option.name, result);
}

bool QCReplyCurlConfiguration::configureProtocolRestrictions(CURL *easy,
                                                             ReplyCurlConfigurationResult &result)
{
    const auto &request = m_plan.normalized.request;
    QStringList protocols;
    QString error;
    if (!QCNetworkProtocolPolicy::resolveInitialProtocols(request.allowedProtocols(),
                                                          &protocols,
                                                          &error)) {
        result.failure = error;
        return false;
    }
    if (!configureProtocolList(
            easy,
            protocols,
            m_allowedProtocolsBytes,
            QCURL_CURL_OPTION(CURLOPT_PROTOCOLS_STR),
            QStringLiteral("QCurl Core initial protocol allowlist could not be applied because "
                           "libcurl does not support CURLOPT_PROTOCOLS_STR (%1)"),
            result)) {
        return false;
    }
    if (!QCNetworkProtocolPolicy::resolveRedirectProtocols(request.allowedRedirectProtocols(),
                                                           &protocols,
                                                           &error)) {
        result.failure = error;
        return false;
    }
    return configureProtocolList(
        easy,
        protocols,
        m_allowedRedirectProtocolsBytes,
        QCURL_CURL_OPTION(CURLOPT_REDIR_PROTOCOLS_STR),
        QStringLiteral("QCurl Core redirect protocol allowlist could not be applied because "
                       "libcurl does not support CURLOPT_REDIR_PROTOCOLS_STR (%1)"),
        result);
}

ReplyCurlConfigurationResult QCReplyCurlConfiguration::applyNetworkPath(CURL *easy)
{
    ReplyCurlConfigurationResult result;
    const auto &request = m_plan.normalized.request;
    if (!required(CurlOptions::setEnabled(easy, CURLOPT_FOLLOWLOCATION, request.followLocation()),
                  QCURL_CURL_OPTION(CURLOPT_FOLLOWLOCATION).name,
                  result)) {
        return result;
    }
    configureRedirects(easy, result);
    if (!requiredOption(easy,
                        QCURL_CURL_OPTION(CURLOPT_IPRESOLVE),
                        curlIpResolveValue(request.ipResolve().value_or(QCNetworkIpResolve::Any)),
                        result)
        || !configureSlist(easy,
                           request.resolveOverride().value_or(QStringList{}),
                           m_resolveSlist,
                           QCURL_CURL_OPTION(CURLOPT_RESOLVE),
                           result)) {
        return result;
    }
#ifdef QCURL_ENABLE_ADVANCED_REQUEST_NETWORK_PATH_API
    configureLocalNetworkBinding(easy, result);
    if (!configureSlist(easy,
                        request.connectTo().value_or(QStringList{}),
                        m_connectToSlist,
                        QCURL_CURL_OPTION(CURLOPT_CONNECT_TO),
                        result)) {
        return result;
    }
    configureDnsTransport(easy, result);
#endif
    Q_UNUSED(configureProtocolRestrictions(easy, result));
    return result;
}

} // namespace QCurl::Internal
