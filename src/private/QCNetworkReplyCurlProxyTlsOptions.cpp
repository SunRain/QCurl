/**
 * @file
 * @brief QCNetworkReply HTTPS proxy TLS curl options.
 */

#include "QCNetworkError.h"
#include "QCNetworkProxyConfig.h"
#include "QCNetworkReply_p.h"
#include "QCNetworkSslConfig.h"
#include "private/QCNetworkReplyCurlOptions_p.h"

#include <optional>

namespace QCurl::Internal::ReplyCurlOptions {

using namespace Detail;

#ifdef CURLPROXY_HTTPS
namespace {

[[nodiscard]] bool configureProxyVerification(QCNetworkReplyPrivate *reply,
                                              CURL *handle,
                                              const QCNetworkProxyConfig::ProxyTlsConfig &config)
{
    const auto policy = config.unsupportedSecurityPolicy();
    return handleSecurityOptionResult(reply,
                                      Internal::CurlOptions::setProxySslVerifyPeerWithTestHook(
                                          handle,
                                          "CURLOPT_PROXY_SSL_VERIFYPEER",
                                          config.verifyPeer()),
                                      "CURLOPT_PROXY_SSL_VERIFYPEER",
                                      policy)
           && handleSecurityOptionResult(reply,
                                         Internal::CurlOptions::setProxySslVerifyHostWithTestHook(
                                             handle,
                                             "CURLOPT_PROXY_SSL_VERIFYHOST",
                                             config.verifyHost()),
                                         "CURLOPT_PROXY_SSL_VERIFYHOST",
                                         policy);
}

[[nodiscard]] bool configureProxyCaCertificate(QCNetworkReplyPrivate *reply,
                                               CURL *handle,
                                               const QCNetworkProxyConfig::ProxyTlsConfig &config)
{
    if (config.caCertPath().isEmpty()) {
        return true;
    }

    reply->proxySslCaCertPathBytes = config.caCertPath().toUtf8();
    return handleSecurityOptionResult(reply,
                                      curlEasySetoptWithTestHook(handle,
                                                                 CURLOPT_PROXY_CAINFO,
                                                                 "CURLOPT_PROXY_CAINFO",
                                                                 reply->proxySslCaCertPathBytes
                                                                     .constData()),
                                      "CURLOPT_PROXY_CAINFO",
                                      config.unsupportedSecurityPolicy());
}

[[nodiscard]] bool configureProxyMinimumTlsVersion(
    QCNetworkReplyPrivate *reply, CURL *handle, const QCNetworkProxyConfig::ProxyTlsConfig &config)
{
    if (!config.minTlsVersion().has_value()) {
        return true;
    }

    const std::optional<long> version = toCurlSslVersionMin(config.minTlsVersion().value());
    if (version.has_value()) {
        return handleSecurityOptionResult(reply,
                                          curlEasySetoptWithTestHook(handle,
                                                                     CURLOPT_PROXY_SSLVERSION,
                                                                     "CURLOPT_PROXY_SSLVERSION",
                                                                     version.value()),
                                          "CURLOPT_PROXY_SSLVERSION",
                                          config.unsupportedSecurityPolicy());
    }

    const QString message = QStringLiteral("不支持的 TLS 版本配置（proxy）");
    if (config.unsupportedSecurityPolicy() == QCUnsupportedSecurityOptionPolicy::Fail) {
        reply->setError(NetworkError::InvalidRequest, message);
        return false;
    }
    appendCapabilityWarning(reply, message);
    return true;
}

[[nodiscard]] bool configureProxyCipherOption(QCNetworkReplyPrivate *reply,
                                              CURL *handle,
                                              CURLoption option,
                                              const char *optionName,
                                              const QString &value,
                                              QByteArray *storage,
                                              QCUnsupportedSecurityOptionPolicy policy)
{
    if (value.isEmpty()) {
        return true;
    }

    *storage = value.toUtf8();
    return handleSecurityOptionResult(reply,
                                      curlEasySetoptWithTestHook(handle,
                                                                 option,
                                                                 optionName,
                                                                 storage->constData()),
                                      optionName,
                                      policy);
}

[[nodiscard]] bool configureProxyCiphers(QCNetworkReplyPrivate *reply,
                                         CURL *handle,
                                         const QCNetworkProxyConfig::ProxyTlsConfig &config)
{
    const auto policy = config.unsupportedSecurityPolicy();
    return configureProxyCipherOption(reply,
                                      handle,
                                      CURLOPT_PROXY_SSL_CIPHER_LIST,
                                      "CURLOPT_PROXY_SSL_CIPHER_LIST",
                                      config.cipherList(),
                                      &reply->proxySslCipherListBytes,
                                      policy)
           && configureProxyCipherOption(reply,
                                         handle,
                                         CURLOPT_PROXY_TLS13_CIPHERS,
                                         "CURLOPT_PROXY_TLS13_CIPHERS",
                                         config.tls13Ciphers(),
                                         &reply->proxySslTls13CiphersBytes,
                                         policy);
}

} // namespace
#endif

[[nodiscard]] bool configureReplyProxyTlsOptions(QCNetworkReplyPrivate *reply,
                                                 CURL *handle,
                                                 const QCNetworkProxyConfig &proxyConfig,
                                                 long curlProxyType)
{
#ifdef CURLPROXY_HTTPS
    const auto config = proxyConfig.tlsConfig();
    if (proxyConfig.type() != QCNetworkProxyConfig::ProxyType::Https || !config.has_value()
        || curlProxyType != CURLPROXY_HTTPS) {
        return true;
    }

    return configureProxyVerification(reply, handle, config.value())
           && configureProxyCaCertificate(reply, handle, config.value())
           && configureProxyMinimumTlsVersion(reply, handle, config.value())
           && configureProxyCiphers(reply, handle, config.value());
#else
    Q_UNUSED(reply)
    Q_UNUSED(handle)
    Q_UNUSED(proxyConfig)
    Q_UNUSED(curlProxyType)
    return true;
#endif
}

} // namespace QCurl::Internal::ReplyCurlOptions
