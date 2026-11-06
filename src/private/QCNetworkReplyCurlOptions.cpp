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

bool configureBaseAndBody(QCNetworkReplyPrivate *reply, CURL *easy)
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
                                                  configuration.applyHeadersAndLimits(
                                                      reply->curlManager));
}

bool configureProxyTlsAndTimeouts(QCNetworkReplyPrivate *reply, CURL *easy)
{
    using namespace Internal;
    auto &configuration = reply->transferState->configuration;
    auto &tls           = reply->transferState->tlsConfiguration;
    std::optional<ProxyTlsInput> proxyTls;
    if (!publishReplyCurlConfigurationResult(reply,
                                             configuration.applyAuthenticationAndProxy(easy,
                                                                                       proxyTls))) {
        return false;
    }
    if (proxyTls && !publishReplyTlsConfigurationResult(reply, tls.applyProxy(easy, *proxyTls))) {
        return false;
    }
    return publishReplyCurlConfigurationResult(reply, configuration.applyHttpVersion(easy))
           && publishReplyTlsConfigurationResult(
               reply,
               tls.applyEndpoint(easy,
                                 configuration.requestPolicy().endpointTls,
                                 configuration.plan().normalized.request.sslConfig()))
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
    return Internal::publishReplyCurlConfigurationResult(this,
                                                         transferState->configuration
                                                             .validationResult())
           && configureBaseAndBody(this, easy) && configureProxyTlsAndTimeouts(this, easy)
           && Internal::configureReplyResponseCallbacks(this, easy)
           && Internal::configureReplyTransferCallbacks(this, easy);
}

} // namespace QCurl
