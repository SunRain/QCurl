/**
 * @file
 * @brief QCNetworkReply endpoint TLS curl options.
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

[[nodiscard]] bool configurePeerVerification(QCNetworkReplyPrivate *reply,
                                             CURL *handle,
                                             const QCNetworkSslConfig &config)
{
    return setRequiredOption(reply,
                             Internal::CurlOptions::setSslVerifyPeer(handle, config.verifyPeer()),
                             QCURL_CURL_OPTION(CURLOPT_SSL_VERIFYPEER).name)
           && setRequiredOption(reply,
                                Internal::CurlOptions::setSslVerifyHost(handle, config.verifyHost()),
                                QCURL_CURL_OPTION(CURLOPT_SSL_VERIFYHOST).name);
}

[[nodiscard]] bool configureCaCertificate(QCNetworkReplyPrivate *reply,
                                          CURL *handle,
                                          const QCNetworkSslConfig &config)
{
    if (!config.caCertPath().isEmpty()) {
        reply->sslCaCertPathBytes = config.caCertPath().toUtf8();
        return setRequiredCurlOption(reply,
                                     handle,
                                     QCURL_CURL_OPTION(CURLOPT_CAINFO),
                                     reply->sslCaCertPathBytes.constData());
    }

    static const char *systemCaPaths[] = {
        "/etc/ssl/certs/ca-certificates.crt",
        "/etc/pki/tls/certs/ca-bundle.crt",
        "/etc/ssl/cert.pem",
        "/usr/local/share/certs/ca-root-nss.crt",
        nullptr,
    };
    for (int i = 0; systemCaPaths[i] != nullptr; ++i) {
        const QFileInfo candidate{QLatin1StringView(systemCaPaths[i])};
        if (candidate.exists() && candidate.isReadable()) {
            return setRequiredCurlOption(reply,
                                         handle,
                                         QCURL_CURL_OPTION(CURLOPT_CAINFO),
                                         systemCaPaths[i]);
        }
    }

    reply->sslCaCertPathBytes.clear();
    return setRequiredCurlOption(reply,
                                 handle,
                                 QCURL_CURL_OPTION(CURLOPT_CAINFO),
                                 static_cast<const char *>(nullptr));
}

[[nodiscard]] bool setNullableTlsStringOption(QCNetworkReplyPrivate *reply,
                                              CURL *handle,
                                              QCurl::Internal::CurlOptions::Option option,
                                              const QString &value,
                                              QByteArray *storage)
{
    if (value.isEmpty()) {
        storage->clear();
        return setRequiredCurlOption(reply,
                                     handle,
                                     option,
                                     static_cast<const char *>(nullptr));
    }

    *storage = value.toUtf8();
    return setRequiredCurlOption(reply, handle, option, storage->constData());
}

[[nodiscard]] bool configureClientCredentials(QCNetworkReplyPrivate *reply,
                                              CURL *handle,
                                              const QCNetworkSslConfig &config)
{
    return setNullableTlsStringOption(reply,
                                      handle,
                                      QCURL_CURL_OPTION(CURLOPT_SSLCERT),
                                      config.clientCertPath(),
                                      &reply->sslClientCertPathBytes)
           && setNullableTlsStringOption(reply,
                                         handle,
                                         QCURL_CURL_OPTION(CURLOPT_SSLKEY),
                                         config.clientKeyPath(),
                                         &reply->sslClientKeyPathBytes)
           && setNullableTlsStringOption(reply,
                                         handle,
                                         QCURL_CURL_OPTION(CURLOPT_KEYPASSWD),
                                         config.clientKeyPassword(),
                                         &reply->sslClientKeyPasswordBytes);
}

[[nodiscard]] bool configurePinnedPublicKey(QCNetworkReplyPrivate *reply,
                                            CURL *handle,
                                            const QCNetworkSslConfig &config)
{
    if (config.pinnedPublicKey().isEmpty()) {
        return true;
    }

    reply->sslPinnedPublicKeyBytes = config.pinnedPublicKey().toUtf8();
    return handleSecurityOptionResult(reply,
                                      curlEasySetoptWithTestHook(handle,
                                                                 QCURL_CURL_OPTION(
                                                                     CURLOPT_PINNEDPUBLICKEY),
                                                                 reply->sslPinnedPublicKeyBytes
                                                                     .constData()),
                                      QCURL_CURL_OPTION(CURLOPT_PINNEDPUBLICKEY).name,
                                      config.unsupportedSecurityPolicy());
}

[[nodiscard]] bool configureMinimumTlsVersion(QCNetworkReplyPrivate *reply,
                                              CURL *handle,
                                              const QCNetworkSslConfig &config)
{
    if (!config.minTlsVersion().has_value()) {
        return true;
    }

    const std::optional<long> version = toCurlSslVersionMin(config.minTlsVersion().value());
    if (version.has_value()) {
        return handleSecurityOptionResult(reply,
                                          curlEasySetoptWithTestHook(handle,
                                                                     QCURL_CURL_OPTION(
                                                                         CURLOPT_SSLVERSION),
                                                                     version.value()),
                                          QCURL_CURL_OPTION(CURLOPT_SSLVERSION).name,
                                          config.unsupportedSecurityPolicy());
    }

    const QString message = QStringLiteral("不支持的 TLS 版本配置（ssl）");
    if (config.unsupportedSecurityPolicy() == QCUnsupportedSecurityOptionPolicy::Fail) {
        reply->setError(NetworkError::InvalidRequest, message);
        return false;
    }
    appendCapabilityWarning(reply, message);
    return true;
}

[[nodiscard]] bool configureSecurityStringOption(QCNetworkReplyPrivate *reply,
                                                 CURL *handle,
                                                 QCurl::Internal::CurlOptions::Option option,
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
                                                                 storage->constData()),
                                      option.name,
                                      policy);
}

[[nodiscard]] bool configureCipherOptions(QCNetworkReplyPrivate *reply,
                                          CURL *handle,
                                          const QCNetworkSslConfig &config)
{
    const auto policy = config.unsupportedSecurityPolicy();
    return configureSecurityStringOption(reply,
                                         handle,
                                         QCURL_CURL_OPTION(CURLOPT_SSL_CIPHER_LIST),
                                         config.cipherList(),
                                         &reply->sslCipherListBytes,
                                         policy)
           && configureSecurityStringOption(reply,
                                            handle,
                                            QCURL_CURL_OPTION(CURLOPT_TLS13_CIPHERS),
                                            config.tls13Ciphers(),
                                            &reply->sslTls13CiphersBytes,
                                            policy);
}

} // namespace

[[nodiscard]] bool configureReplyTlsOptions(QCNetworkReplyPrivate *reply,
                                            CURL *handle,
                                            const QCNetworkRequest &request)
{
    const QCNetworkSslConfig config = request.sslConfig();
    return configurePeerVerification(reply, handle, config)
           && configureCaCertificate(reply, handle, config)
           && configureClientCredentials(reply, handle, config)
           && configurePinnedPublicKey(reply, handle, config)
           && configureMinimumTlsVersion(reply, handle, config)
           && configureCipherOptions(reply, handle, config);
}

} // namespace QCurl::Internal::ReplyCurlOptions
