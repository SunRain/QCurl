/**
 * @file
 * @brief 应用 endpoint TLS，并持有其原生选项字节存储。
 */
#include "QCTlsConfigurationPolicy_p.h"
#include "QCTlsConfiguration_p.h"

namespace QCurl::Internal {

bool QCTlsConfiguration::requiredString(CURL *easy,
                                        CurlOptions::Option option,
                                        const QString &value,
                                        QByteArray &storage,
                                        TlsConfigurationResult &result)
{
    storage           = value.toUtf8();
    const char *bytes = value.isEmpty() ? nullptr : storage.constData();
    return TlsPolicy::required(result, CurlOptions::setWithTestHook(easy, option, bytes), option);
}

bool QCTlsConfiguration::securityString(CURL *easy,
                                        CurlOptions::Option option,
                                        const QString &value,
                                        QByteArray &storage,
                                        QCUnsupportedSecurityOptionPolicy policy,
                                        TlsConfigurationResult &result)
{
    if (value.isEmpty()) {
        return true;
    }
    storage = value.toUtf8();
    return TlsPolicy::security(result,
                               CurlOptions::setWithTestHook(easy, option, storage.constData()),
                               option,
                               policy);
}

bool QCTlsConfiguration::configureCa(CURL *easy,
                                     const RequestEndpointTlsPolicy &common,
                                     TlsConfigurationResult &result)
{
    return common.caCertPath.isEmpty()
           || requiredString(easy,
                             QCURL_CURL_OPTION(CURLOPT_CAINFO),
                             common.caCertPath,
                             m_sslCaCertPathBytes,
                             result);
}

bool QCTlsConfiguration::configureCredentials(CURL *easy,
                                              const QCNetworkSslConfig &config,
                                              TlsConfigurationResult &result)
{
    return requiredString(easy,
                          QCURL_CURL_OPTION(CURLOPT_SSLCERT),
                          config.clientCertPath(),
                          m_sslClientCertPathBytes,
                          result)
           && requiredString(easy,
                             QCURL_CURL_OPTION(CURLOPT_SSLKEY),
                             config.clientKeyPath(),
                             m_sslClientKeyPathBytes,
                             result)
           && requiredString(easy,
                             QCURL_CURL_OPTION(CURLOPT_KEYPASSWD),
                             config.clientKeyPassword(),
                             m_sslClientKeyPasswordBytes,
                             result);
}

TlsConfigurationResult QCTlsConfiguration::applyEndpoint(CURL *easy,
                                                         const RequestEndpointTlsPolicy &common,
                                                         const QCNetworkSslConfig &config)
{
    TlsConfigurationResult result;
    const auto policy = config.unsupportedSecurityPolicy();
    const bool configured
        = TlsPolicy::required(result,
                              CurlOptions::setSslVerifyPeerWithTestHook(easy, common.verifyPeer),
                              QCURL_CURL_OPTION(CURLOPT_SSL_VERIFYPEER))
          && TlsPolicy::required(result,
                                 CurlOptions::setSslVerifyHostWithTestHook(easy, common.verifyHost),
                                 QCURL_CURL_OPTION(CURLOPT_SSL_VERIFYHOST))
          && configureCa(easy, common, result) && configureCredentials(easy, config, result)
          && securityString(easy,
                            QCURL_CURL_OPTION(CURLOPT_PINNEDPUBLICKEY),
                            config.pinnedPublicKey(),
                            m_sslPinnedPublicKeyBytes,
                            policy,
                            result)
          && TlsPolicy::minimumVersion(easy,
                                       QCURL_CURL_OPTION(CURLOPT_SSLVERSION),
                                       config.minTlsVersion(),
                                       policy,
                                       QStringLiteral("ssl"),
                                       result)
          && securityString(easy,
                            QCURL_CURL_OPTION(CURLOPT_SSL_CIPHER_LIST),
                            config.cipherList(),
                            m_sslCipherListBytes,
                            policy,
                            result)
          && securityString(easy,
                            QCURL_CURL_OPTION(CURLOPT_TLS13_CIPHERS),
                            config.tls13Ciphers(),
                            m_sslTls13CiphersBytes,
                            policy,
                            result);
    Q_UNUSED(configured);
    return result;
}

} // namespace QCurl::Internal
