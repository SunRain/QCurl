/**
 * @file
 * @brief 集中共同请求配置的生效性、优先级及无损数值转换。
 */
#include "QCNetworkHttpHeaders.h"
#include "QCNetworkProtocolPolicy_p.h"
#include "QCNetworkRequest.h"
#include "QCNetworkRequestPolicy_p.h"
#include "QCNetworkTimeoutConfig.h"
#include "private/QCNetworkHttpVersion_p.h"

#include <limits>
#include <utility>

namespace QCurl::Internal {
namespace {

/// 一次扫描收集 raw header 的存在性，空值不改变覆盖语义。
struct HeaderFacts
{
    bool authorization = false;
    bool referer       = false;
    bool encoding      = false;
    bool sensitive     = false;
};

HeaderFacts headerFacts(const QCNetworkRequest &request)
{
    HeaderFacts facts;
    for (const auto &name : request.rawHeaderList()) {
        const auto normalized = name.trimmed().toLower();
        facts.authorization |= normalized == httpheaders::kAuthorization.toLower();
        facts.referer |= normalized == httpheaders::kReferer.toLower();
        facts.encoding |= normalized == httpheaders::kAcceptEncoding.toLower();
        facts.sensitive |= normalized == httpheaders::kAuthorization.toLower()
                           || normalized == httpheaders::kProxyAuthorization.toLower()
                           || normalized == httpheaders::kCookie.toLower()
                           || normalized == httpheaders::kSetCookie.toLower();
    }
    return facts;
}

long httpAuthMethod(QCNetworkHttpAuthMethod method)
{
    switch (method) {
        case QCNetworkHttpAuthMethod::Basic:
            return static_cast<long>(CURLAUTH_BASIC);
        case QCNetworkHttpAuthMethod::Any:
            return static_cast<long>(CURLAUTH_ANY);
        case QCNetworkHttpAuthMethod::AnySafe:
            return static_cast<long>(CURLAUTH_ANYSAFE);
    }
    return static_cast<long>(CURLAUTH_BASIC);
}

long proxyType(QCNetworkProxyConfig::ProxyType type)
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
        case QCNetworkProxyConfig::ProxyType::None:
        case QCNetworkProxyConfig::ProxyType::Http:
            return CURLPROXY_HTTP;
    }
    return CURLPROXY_HTTP;
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

template<typename Target, typename Source>
[[nodiscard]] bool positiveNativeValue(Source value,
                                       std::optional<Target> &output,
                                       const char *category,
                                       QString &failure)
{
    bool invalid = value < 0;
    if constexpr (std::numeric_limits<Target>::digits < std::numeric_limits<Source>::digits) {
        invalid |= value > static_cast<Source>(std::numeric_limits<Target>::max());
    }
    if (invalid) {
        failure = QStringLiteral("请求配置无效：%1 超出原生非负取值范围")
                      .arg(QString::fromLatin1(category));
        return false;
    }
    if (value > 0) {
        output = static_cast<Target>(value);
    }
    return true;
}

[[nodiscard]] bool compileProtocols(const QCNetworkRequest &request,
                                    RequestPolicy &policy,
                                    QString &failure)
{
    QStringList protocols;
    if (!QCNetworkProtocolPolicy::validateCoreUrl(request.url(), &failure)
        || !QCNetworkProtocolPolicy::resolveInitialProtocols(request.allowedProtocols(),
                                                             &protocols,
                                                             &failure)) {
        return false;
    }
    policy.url               = request.url().toEncoded(QUrl::FullyEncoded);
    policy.protocols.initial = protocols.join(QLatin1Char(',')).toUtf8();
    if (request.followLocation()) {
        if (!QCNetworkProtocolPolicy::resolveRedirectProtocols(request.allowedRedirectProtocols(),
                                                               &protocols,
                                                               &failure)) {
            return false;
        }
        policy.protocols.redirect = protocols.join(QLatin1Char(',')).toUtf8();
    }
    return true;
}

[[nodiscard]] bool compileProxy(const QCNetworkRequest &request,
                                RequestPolicy &policy,
                                QString &failure)
{
    const auto proxy = request.proxyConfig();
    if (!proxy || proxy->type() == QCNetworkProxyConfig::ProxyType::None) {
        return true;
    }
    if (!proxy->isValid()) {
        failure = QStringLiteral("请求配置无效：显式代理需要主机名与非零端口");
        return false;
    }
    RequestProxyEndpoint endpoint;
    endpoint.requestedType = proxy->type();
    endpoint.type          = proxyType(proxy->type());
    endpoint.host          = proxy->hostName().toUtf8();
    endpoint.port          = static_cast<long>(proxy->port());
    if (!proxy->userName().isEmpty()) {
        endpoint.user = proxy->userName().toUtf8();
    }
    if (!proxy->password().isEmpty()) {
        endpoint.password = proxy->password().toUtf8();
    }
    endpoint.negotiateHttpAuth = (endpoint.user || endpoint.password)
                                 && (endpoint.type == CURLPROXY_HTTP
                                     || endpoint.type == CURLPROXY_HTTPS);
    policy.proxy               = std::move(endpoint);
    return true;
}

[[nodiscard]] bool compileTimeouts(const QCNetworkRequest &request,
                                   RequestPolicy &policy,
                                   QString &failure)
{
    const auto timeout = request.timeoutConfig();
    const auto connect = timeout.connectTimeout();
    const auto total   = timeout.totalTimeout();
    return (!connect
            || positiveNativeValue(connect->count(),
                                   policy.timeout.connectMilliseconds,
                                   "connect timeout",
                                   failure))
           && (!total
               || positiveNativeValue(total->count(),
                                      policy.timeout.totalMilliseconds,
                                      "total timeout",
                                      failure));
}

[[nodiscard]] bool compileNumbers(const QCNetworkRequest &request,
                                  RequestPolicy &policy,
                                  QString &failure)
{
    policy.redirect.follow = request.followLocation();
    if (policy.redirect.follow) {
        // 重定向上限的零值是有效上限，不是未设置。
        if (const auto maximum = request.maxRedirects()) {
            if (!positiveNativeValue(*maximum, policy.redirect.maximum, "max redirects", failure)) {
                return false;
            }
            policy.redirect.maximum = static_cast<long>(*maximum);
        }
        policy.redirect.post        = postRedirectValue(request.postRedirectPolicy());
        policy.redirect.autoReferer = request.autoRefererEnabled();
    }
    const auto download = request.maxDownloadBytesPerSec();
    const auto upload   = request.maxUploadBytesPerSec();
    return (!download
            || positiveNativeValue(*download,
                                   policy.transfer.downloadBytesPerSecond,
                                   "download rate",
                                   failure))
           && (!upload
               || positiveNativeValue(*upload,
                                      policy.transfer.uploadBytesPerSecond,
                                      "upload rate",
                                      failure));
}

RequestHeaderPolicy compileHeaders(const QCNetworkRequest &request,
                                   RequestPolicyDiagnostics &diagnostics)
{
    const auto facts = headerFacts(request);
    RequestHeaderPolicy policy;
    diagnostics.refererConflict = facts.referer && !request.referer().isEmpty();
    if (!facts.referer && !request.referer().isEmpty()) {
        policy.referer = request.referer().toUtf8();
    }
    diagnostics.encodingConflict = facts.encoding
                                   && (request.autoDecompressionEnabled()
                                       || !request.acceptedEncodings().isEmpty());
    if (!facts.encoding && request.autoDecompressionEnabled()) {
        policy.acceptEncoding = request.acceptedEncodings().join(QLatin1Char(',')).toUtf8();
    }
    const auto auth           = request.httpAuth();
    diagnostics.basicOverHttp = auth && auth->method() == QCNetworkHttpAuthMethod::Basic
                                && auth->warnIfBasicOverHttp()
                                && request.url().scheme().compare(QStringLiteral("http"),
                                                                  Qt::CaseInsensitive)
                                       == 0;
    bool unrestricted         = request.allowUnrestrictedSensitiveHeadersOnRedirect();
    if (auth && !facts.authorization) {
        policy.credentials = RequestHttpCredentials{auth->userName().toUtf8(),
                                                    auth->password().toUtf8(),
                                                    httpAuthMethod(auth->method())};
        unrestricted |= auth->allowUnrestrictedAuth();
    }
    policy.unrestrictedAuth = request.followLocation() && unrestricted
                              && (facts.sensitive || policy.credentials.has_value());
    return policy;
}

} // namespace

RequestPolicyResult compileRequestPolicy(const QCNetworkRequest &request)
{
    RequestPolicyResult result;
    RequestPolicy policy;
    policy.headers = compileHeaders(request, result.diagnostics);
    if (!compileProtocols(request, policy, result.failure)
        || !compileProxy(request, policy, result.failure)
        || !compileTimeouts(request, policy, result.failure)
        || !compileNumbers(request, policy, result.failure)) {
        return result;
    }
    if (request.rangeStart() >= 0 && request.rangeEnd() > request.rangeStart()) {
        policy.transfer.range = QByteArray::number(request.rangeStart()) + '-'
                                + QByteArray::number(request.rangeEnd());
    }
    const auto tls     = request.sslConfig();
    policy.endpointTls = {tls.verifyPeer(), tls.verifyHost(), tls.caCertPath()};
    policy.httpVersion = {request.httpVersion(), request.isHttpVersionExplicit()};
    result.policy      = std::move(policy);
    return result;
}

HttpVersionDecision decideHttpVersionPolicy(const HttpVersionIntent &intent,
                                            const HttpVersionFacts &facts)
{
    if (!intent.explicitVersion) {
        return {};
    }
    auto effective = intent.requested;
    if (effective == QCNetworkHttpVersion::Http3Only
        && (facts.runtimeVersion < 0x075800 || !facts.hasHttp3)) {
        return {std::nullopt,
                QStringLiteral(
                    "Http3Only 需要编译及运行时 libcurl >= 7.88.0 且支持 HTTP/3；禁止降级"),
                false};
    }
    bool downgraded = false;
    if (effective == QCNetworkHttpVersion::Http3 && !facts.hasHttp3) {
        if (facts.requireHttp3) {
            return {std::nullopt,
                    QStringLiteral(
                        "QCURL_REQUIRE_HTTP3=1：运行时 libcurl 不支持 HTTP/3，请求被拒绝"),
                    false};
        }
        effective  = QCNetworkHttpVersion::Http2TLS;
        downgraded = true;
    }
    return {detail::toCurlHttpVersion(effective), {}, downgraded};
}

} // namespace QCurl::Internal
