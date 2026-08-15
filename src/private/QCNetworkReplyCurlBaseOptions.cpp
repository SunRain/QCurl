/**
 * @file
 * @brief QCNetworkReply base and network-path curl options.
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
#include "private/QCNetworkProtocolPolicy_p.h"
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

[[nodiscard]] bool configureBaseIdentity(QCNetworkReplyPrivate *reply,
                                         CURL *handle,
                                         const QCNetworkRequest &request)
{
    QString protocolError;
    if (!QCNetworkProtocolPolicy::validateCoreUrl(request.url(), &protocolError)) {
        reply->setError(NetworkError::InvalidRequest, protocolError);
        return false;
    }

    const QByteArray urlBytes = request.url().toString().toUtf8();
    return setRequiredCurlOption(reply, handle, CURLOPT_URL, "CURLOPT_URL", urlBytes.constData())
           && setRequiredCurlOption(reply, handle, CURLOPT_PRIVATE, "CURLOPT_PRIVATE", reply)
           && setRequiredOption(reply,
                                Internal::CurlOptions::setEnabled(handle,
                                                                  CURLOPT_FOLLOWLOCATION,
                                                                  request.followLocation()),
                                "CURLOPT_FOLLOWLOCATION");
}

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

void configureRedirects(QCNetworkReplyPrivate *reply, CURL *handle, const QCNetworkRequest &request)
{
    if (!request.followLocation()) {
        return;
    }

    if (const auto maxRedirects = request.maxRedirects(); maxRedirects.has_value()) {
        setOptionalLongOption(reply,
                              handle,
                              CURLOPT_MAXREDIRS,
                              "CURLOPT_MAXREDIRS",
                              static_cast<long>(maxRedirects.value()));
    }
    if (const auto value = postRedirectValue(request.postRedirectPolicy()); value.has_value()) {
        setOptionalLongOption(reply, handle, CURLOPT_POSTREDIR, "CURLOPT_POSTREDIR", value.value());
    }
    if (request.autoRefererEnabled()) {
        setOptionalLongOption(reply,
                              handle,
                              CURLOPT_AUTOREFERER,
                              "CURLOPT_AUTOREFERER",
                              Internal::CurlOptions::kEnabled);
    }
}

long curlIpResolveValue(QCNetworkIpResolve value)
{
    switch (value) {
        case QCNetworkIpResolve::Any:
            return CURL_IPRESOLVE_WHATEVER;
        case QCNetworkIpResolve::Ipv4:
            return CURL_IPRESOLVE_V4;
        case QCNetworkIpResolve::Ipv6:
            return CURL_IPRESOLVE_V6;
    }
    return CURL_IPRESOLVE_WHATEVER;
}

void configureIpResolution(QCNetworkReplyPrivate *reply,
                           CURL *handle,
                           const QCNetworkRequest &request)
{
    if (const auto value = request.ipResolve(); value.has_value()) {
        setOptionalLongOption(reply,
                              handle,
                              CURLOPT_IPRESOLVE,
                              "CURLOPT_IPRESOLVE",
                              curlIpResolveValue(value.value()));
    }
}

#ifdef QCURL_ENABLE_ADVANCED_REQUEST_NETWORK_PATH_API

void configureLocalNetworkBinding(QCNetworkReplyPrivate *reply,
                                  CURL *handle,
                                  const QCNetworkRequest &request)
{
    if (const auto timeout = request.happyEyeballsTimeout(); timeout.has_value()) {
        setOptionalLongOption(reply,
                              handle,
                              CURLOPT_HAPPY_EYEBALLS_TIMEOUT_MS,
                              "CURLOPT_HAPPY_EYEBALLS_TIMEOUT_MS",
                              static_cast<long>(timeout->count()));
    }
    if (const auto interface = request.networkInterface(); interface.has_value()) {
        reply->interfaceBytes = interface->toUtf8();
        setOptionalStringOption(reply,
                                handle,
                                CURLOPT_INTERFACE,
                                "CURLOPT_INTERFACE",
                                reply->interfaceBytes);
    }
    if (const auto port = request.localPort(); port.has_value()) {
        setOptionalLongOption(reply,
                              handle,
                              CURLOPT_LOCALPORT,
                              "CURLOPT_LOCALPORT",
                              static_cast<long>(*port));
    }
    if (const auto range = request.localPortRange(); range.has_value()) {
        setOptionalLongOption(reply,
                              handle,
                              CURLOPT_LOCALPORTRANGE,
                              "CURLOPT_LOCALPORTRANGE",
                              static_cast<long>(*range));
    }
}

[[nodiscard]] bool configureSlistOption(QCNetworkReplyPrivate *reply,
                                        CURL *handle,
                                        const QStringList &entries,
                                        curl_slist **storage,
                                        CURLoption option,
                                        const char *optionName)
{
    QString error;
    if (!buildSlistFromStrings(entries, storage, &error, optionName)) {
        reply->setError(NetworkError::InvalidRequest, error);
        return false;
    }
    if (!*storage
        || setRequiredOption(reply,
                             curlEasySetoptWithTestHook(handle, option, optionName, *storage),
                             optionName)) {
        return true;
    }

    curl_slist_free_all(*storage);
    *storage = nullptr;
    return false;
}

void configureDnsTransport(QCNetworkReplyPrivate *reply,
                           CURL *handle,
                           const QCNetworkRequest &request)
{
    if (const auto servers = request.dnsServers(); servers.has_value()) {
        reply->dnsServersBytes = servers->join(QStringLiteral(",")).toUtf8();
        setOptionalStringOption(reply,
                                handle,
                                CURLOPT_DNS_SERVERS,
                                "CURLOPT_DNS_SERVERS",
                                reply->dnsServersBytes);
    }
    if (const auto dohUrl = request.dohUrl(); dohUrl.has_value()) {
        reply->dohUrlBytes = dohUrl->toString().toUtf8();
        setOptionalStringOption(reply,
                                handle,
                                CURLOPT_DOH_URL,
                                "CURLOPT_DOH_URL",
                                reply->dohUrlBytes);
    }
}

[[nodiscard]] bool configureAdvancedNetworkPath(QCNetworkReplyPrivate *reply,
                                                CURL *handle,
                                                const QCNetworkRequest &request)
{
    configureLocalNetworkBinding(reply, handle, request);
    if (const auto entries = request.resolveOverride();
        entries.has_value()
        && !configureSlistOption(reply,
                                 handle,
                                 entries.value(),
                                 &reply->resolveSlist,
                                 CURLOPT_RESOLVE,
                                 "CURLOPT_RESOLVE")) {
        return false;
    }
    if (const auto entries = request.connectTo(); entries.has_value()
                                                  && !configureSlistOption(reply,
                                                                           handle,
                                                                           entries.value(),
                                                                           &reply->connectToSlist,
                                                                           CURLOPT_CONNECT_TO,
                                                                           "CURLOPT_CONNECT_TO")) {
        return false;
    }
    configureDnsTransport(reply, handle, request);
    return true;
}

#endif

[[nodiscard]] bool configureProtocolList(QCNetworkReplyPrivate *reply,
                                         CURL *handle,
                                         const QStringList &protocols,
                                         QByteArray *storage,
                                         CURLoption option,
                                         const char *optionName,
                                         const QString &unsupportedMessage,
                                         QCUnsupportedSecurityOptionPolicy policy)
{
    *storage            = protocols.join(QStringLiteral(",")).toUtf8();
    const CURLcode code = curlEasySetoptWithTestHook(handle,
                                                     option,
                                                     optionName,
                                                     storage->constData());
    if (code == CURLE_OK) {
        return true;
    }
    if (isCapabilityRelatedCurlError(code)) {
        const QString message = unsupportedMessage.arg(QString::fromUtf8(curl_easy_strerror(code)));
        if (policy == QCUnsupportedSecurityOptionPolicy::Fail) {
            reply->setError(NetworkError::InvalidRequest, message);
            return false;
        }
        appendCapabilityWarning(reply, message);
        return true;
    }

    reply->setError(NetworkError::InvalidRequest,
                    QStringLiteral("设置 %1 失败（%2）")
                        .arg(QString::fromUtf8(optionName),
                             QString::fromUtf8(curl_easy_strerror(code))));
    return false;
}

[[nodiscard]] bool configureProtocolRestrictions(QCNetworkReplyPrivate *reply,
                                                 CURL *handle,
                                                 const QCNetworkRequest &request)
{
    QStringList allowedProtocols;
    QString protocolError;
    if (!QCNetworkProtocolPolicy::resolveInitialProtocols(request.allowedProtocols(),
                                                          &allowedProtocols,
                                                          &protocolError)) {
        reply->setError(NetworkError::InvalidRequest, protocolError);
        return false;
    }

    if (!configureProtocolList(
            reply,
            handle,
            allowedProtocols,
            &reply->allowedProtocolsBytes,
            CURLOPT_PROTOCOLS_STR,
            "CURLOPT_PROTOCOLS_STR",
            QStringLiteral(
                "QCurl Core initial protocol allowlist could not be applied because libcurl does "
                "not support CURLOPT_PROTOCOLS_STR (%1)"),
            QCUnsupportedSecurityOptionPolicy::Fail)) {
        return false;
    }

    QStringList redirectProtocols;
    if (!QCNetworkProtocolPolicy::resolveRedirectProtocols(request.allowedRedirectProtocols(),
                                                           &redirectProtocols,
                                                           &protocolError)) {
        reply->setError(NetworkError::InvalidRequest, protocolError);
        return false;
    }

    if (!configureProtocolList(
            reply,
            handle,
            redirectProtocols,
            &reply->allowedRedirectProtocolsBytes,
            CURLOPT_REDIR_PROTOCOLS_STR,
            "CURLOPT_REDIR_PROTOCOLS_STR",
            QStringLiteral(
                "QCurl Core redirect protocol allowlist could not be applied because libcurl does "
                "not support CURLOPT_REDIR_PROTOCOLS_STR (%1)"),
            QCUnsupportedSecurityOptionPolicy::Fail)) {
        return false;
    }

    return true;
}

[[nodiscard]] bool configureBodySource(QCNetworkReplyPrivate *reply,
                                       const Internal::RequestBody &bodySpec,
                                       const Internal::NormalizedRequest &normalized)
{
    QString error;
    if (!Internal::prepareReplyBodySource(reply, bodySpec, &error)) {
        reply->setError(NetworkError::InvalidRequest, error);
        return false;
    }

    if (reply->requestBodySource.device && reply->qObject()) {
        QObject::connect(reply->requestBodySource.device,
                         &QIODevice::readyRead,
                         reply->qObject(),
                         [reply]() { Q_UNUSED(reply->resumeSendFromRequestBodySourceIfNeeded()); });
    }
    if (reply->requestBodySource.device && normalized.method != HttpMethod::Post
        && normalized.method != HttpMethod::Put) {
        reply->setError(NetworkError::InvalidRequest,
                        QStringLiteral("request body source 仅支持 PUT/POST（当前方法未支持）"));
        return false;
    }
    if (reply->requestBodySource.device && normalized.request.retryPolicy().isEnabled()
        && !reply->requestBodySource.seekable) {
        reply->setError(NetworkError::InvalidRequest,
                        QStringLiteral(
                            "request body source: non-seekable body 不支持自动重试（需要重发 "
                            "body；请关闭 retryPolicy 或使用 seekable 来源）"));
        return false;
    }
    return true;
}

} // namespace

[[nodiscard]] bool configureReplyBaseAndNetworkOptions(QCNetworkReplyPrivate *reply,
                                                       CURL *handle,
                                                       const Internal::NormalizedRequest &normalized,
                                                       const Internal::RequestBody &bodySpec)
{
    if (!configureBaseIdentity(reply, handle, normalized.request)) {
        return false;
    }
    configureRedirects(reply, handle, normalized.request);
    configureIpResolution(reply, handle, normalized.request);
#ifdef QCURL_ENABLE_ADVANCED_REQUEST_NETWORK_PATH_API
    if (!configureAdvancedNetworkPath(reply, handle, normalized.request)) {
        return false;
    }
#endif
    return configureProtocolRestrictions(reply, handle, normalized.request)
           && configureBodySource(reply, bodySpec, normalized);
}

} // namespace QCurl::Internal::ReplyCurlOptions
