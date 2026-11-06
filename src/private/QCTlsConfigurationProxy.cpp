/**
 * @file
 * @brief 应用实际 HTTPS proxy 的 TLS 配置，保持与 origin 独立。
 */
#include "QCTlsConfigurationPolicy_p.h"
#include "QCTlsConfiguration_p.h"

namespace QCurl::Internal {

namespace {

bool configureVerification(CURL *easy,
                           const QCNetworkProxyConfig::ProxyTlsConfig &config,
                           TlsConfigurationResult &result)
{
    const auto policy = config.unsupportedSecurityPolicy();
    return TlsPolicy::security(result,
                               CurlOptions::setProxySslVerifyPeerWithTestHook(easy,
                                                                              config.verifyPeer()),
                               QCURL_CURL_OPTION(CURLOPT_PROXY_SSL_VERIFYPEER),
                               policy)
           && TlsPolicy::security(result,
                                  CurlOptions::setProxySslVerifyHostWithTestHook(easy,
                                                                                 config.verifyHost()),
                                  QCURL_CURL_OPTION(CURLOPT_PROXY_SSL_VERIFYHOST),
                                  policy);
}

} // namespace

TlsConfigurationResult QCTlsConfiguration::applyProxy(CURL *easy, const ProxyTlsInput &input)
{
    TlsConfigurationResult result;
    if (input.requestedType != QCNetworkProxyConfig::ProxyType::Https || !input.config.has_value()
        || input.effectiveCurlProxyType != CURLPROXY_HTTPS) {
        return result;
    }
    const auto &config = input.config.value();
    const auto policy  = config.unsupportedSecurityPolicy();
    const bool configured = configureVerification(easy, config, result)
                            && securityString(easy,
                                              QCURL_CURL_OPTION(CURLOPT_PROXY_CAINFO),
                                              config.caCertPath(),
                                              m_proxySslCaCertPathBytes,
                                              policy,
                                              result)
                            && TlsPolicy::minimumVersion(easy,
                                                         QCURL_CURL_OPTION(CURLOPT_PROXY_SSLVERSION),
                                                         config.minTlsVersion(),
                                                         policy,
                                                         QStringLiteral("proxy"),
                                                         result)
                            && securityString(easy,
                                              QCURL_CURL_OPTION(CURLOPT_PROXY_SSL_CIPHER_LIST),
                                              config.cipherList(),
                                              m_proxySslCipherListBytes,
                                              policy,
                                              result)
                            && securityString(easy,
                                              QCURL_CURL_OPTION(CURLOPT_PROXY_TLS13_CIPHERS),
                                              config.tls13Ciphers(),
                                              m_proxySslTls13CiphersBytes,
                                              policy,
                                              result);
    Q_UNUSED(configured);
    return result;
}

} // namespace QCurl::Internal
