/**
 * @file
 * @brief 按既有插点编排请求配置、设备、TLS 与 callback 绑定。
 */

#include "CurlFeatureProbe.h"
#include "QCNetworkReply_p.h"
#include "private/QCNetworkReplyConfigurationExecution_p.h"

#include <QDebug>

namespace QCurl {
namespace {

bool configureBaseAndBody(QCNetworkReplyPrivate *reply,
                          CURL *easy,
                          Internal::ReplyCurlHeaderFacts &facts)
{
    using namespace Internal;
    auto &configuration = reply->transferState->configuration;
    return publishReplyCurlConfigurationResult(reply, configuration.applyUrl(easy))
           && setRequiredReplyCallbackOption(reply, easy, QCURL_CURL_OPTION(CURLOPT_PRIVATE), reply)
           && publishReplyCurlConfigurationResult(reply, configuration.applyNetworkPath(easy))
           && prepareReplyBodySource(reply)
           && publishReplyCurlConfigurationResult(reply, configuration.applyMethod(easy))
           && configureReplyReadCallbacks(reply, easy)
           && publishReplyCurlConfigurationResult(
               reply,
               configuration.applyTransferBody(easy, reply->transferState->bodySource.sizeBytes()))
           && publishReplyCurlConfigurationResult(reply,
                                                  configuration
                                                      .applyHeadersAndLimits(reply->curlManager,
                                                                             facts));
}

bool configureProxyTlsAndTimeouts(QCNetworkReplyPrivate *reply,
                                  CURL *easy,
                                  const Internal::ReplyCurlHeaderFacts &facts)
{
    using namespace Internal;
    auto &configuration = reply->transferState->configuration;
    auto &tls           = reply->transferState->tlsConfiguration;
    std::optional<ProxyTlsInput> proxyTls;
    if (!publishReplyCurlConfigurationResult(reply,
                                             configuration.applyAuthenticationAndProxy(easy,
                                                                                       facts,
                                                                                       proxyTls))) {
        return false;
    }
    if (proxyTls && !publishReplyTlsConfigurationResult(reply, tls.applyProxy(easy, *proxyTls))) {
        return false;
    }
    return publishReplyCurlConfigurationResult(reply, configuration.applyHttpVersion(easy))
           && publishReplyTlsConfigurationResult(
               reply, tls.applyEndpoint(easy, configuration.plan().normalized.request.sslConfig()))
           && publishReplyCurlConfigurationResult(reply, configuration.applyTimeouts(easy));
}

} // namespace

bool QCNetworkReplyPrivate::configureCurlOptions()
{
    CURL *easy = curlManager.handle();
    if (!easy) {
        const QString diagnostic = curlManager.initializationError();
        qCritical().noquote() << "QCNetworkReply:" << diagnostic;
        setError(NetworkError::InvalidRequest,
                 diagnostic.isEmpty() ? QStringLiteral("curl easy handle initialization failed")
                                      : diagnostic);
        return false;
    }
    const auto availability = CurlFeatureProbe::instance().minimumRuntimeAvailability();
    if (!availability.supported) {
        setError(NetworkError::InvalidRequest, availability.reason);
        return false;
    }
    Internal::ReplyCurlHeaderFacts facts;
    return configureBaseAndBody(this, easy, facts)
           && configureProxyTlsAndTimeouts(this, easy, facts)
           && Internal::configureReplyResponseCallbacks(this, easy)
           && Internal::configureReplyTransferCallbacks(this, easy);
}

} // namespace QCurl
