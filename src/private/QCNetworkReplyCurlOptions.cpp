/**
 * @file
 * @brief QCNetworkReply libcurl option configuration.
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

namespace QCurl::Internal::ReplyCurlOptions::Detail {

bool isCapabilityRelatedCurlError(CURLcode code)
{
    return QCurl::Internal::isReplyCapabilityRelatedCurlError(code);
}

void appendCapabilityWarning(QCNetworkReplyPrivate *d, const QString &message)
{
    QCurl::Internal::appendReplyCapabilityWarning(d, message);
}

[[nodiscard]] bool setRequiredOption(QCNetworkReplyPrivate *d, CURLcode code, const char *optionName)
{
    if (code == CURLE_OK) {
        return true;
    }

    d->setError(NetworkError::InvalidRequest,
                QStringLiteral("设置 %1 失败（%2）")
                    .arg(QString::fromUtf8(optionName))
                    .arg(QString::fromUtf8(curl_easy_strerror(code))));
    return false;
}

[[nodiscard]] bool handleSecurityOptionResult(QCNetworkReplyPrivate *reply,
                                              CURLcode code,
                                              const char *optionName,
                                              QCUnsupportedSecurityOptionPolicy policy)
{
    if (code == CURLE_OK) {
        return true;
    }

    const QString curlError = QString::fromUtf8(curl_easy_strerror(code));
    if (isCapabilityRelatedCurlError(code)) {
        const QString message = QStringLiteral("libcurl 不支持 %1（%2）")
                                    .arg(QString::fromUtf8(optionName), curlError);
        if (policy == QCUnsupportedSecurityOptionPolicy::Fail) {
            reply->setError(NetworkError::InvalidRequest, message);
            return false;
        }
        appendCapabilityWarning(reply, message);
        return true;
    }

    reply->setError(NetworkError::InvalidRequest,
                    QStringLiteral("设置 %1 失败（%2）")
                        .arg(QString::fromUtf8(optionName), curlError));
    return false;
}

bool setOptionalLongOption(QCNetworkReplyPrivate *d,
                           CURL *handle,
                           QCurl::Internal::CurlOptions::Option option,
                           long value)
{
    if (!handle) {
        return false;
    }

    const CURLcode rc = curlEasySetoptWithTestHook(handle, option, value);
    if (rc == CURLE_OK) {
        return true;
    }

    if (isCapabilityRelatedCurlError(rc)) {
        appendCapabilityWarning(d,
                                QStringLiteral("libcurl 不支持 %1（%2）")
                                    .arg(QString::fromUtf8(option.name))
                                    .arg(QString::fromUtf8(curl_easy_strerror(rc))));
        return false;
    }

    appendCapabilityWarning(d,
                            QStringLiteral("设置 %1 失败（%2）")
                                .arg(QString::fromUtf8(option.name))
                                .arg(QString::fromUtf8(curl_easy_strerror(rc))));
    return false;
}

bool setOptionalStringOption(QCNetworkReplyPrivate *d,
                             CURL *handle,
                             QCurl::Internal::CurlOptions::Option option,
                             const QByteArray &value)
{
    if (!handle) {
        return false;
    }

    const CURLcode rc = curlEasySetoptWithTestHook(handle, option, value.constData());
    if (rc == CURLE_OK) {
        return true;
    }

    if (isCapabilityRelatedCurlError(rc)) {
        appendCapabilityWarning(d,
                                QStringLiteral("libcurl 不支持 %1（%2）")
                                    .arg(QString::fromUtf8(option.name))
                                    .arg(QString::fromUtf8(curl_easy_strerror(rc))));
        return false;
    }

    appendCapabilityWarning(d,
                            QStringLiteral("设置 %1 失败（%2）")
                                .arg(QString::fromUtf8(option.name))
                                .arg(QString::fromUtf8(curl_easy_strerror(rc))));
    return false;
}

bool setOptionalOffTOption(QCNetworkReplyPrivate *d,
                           CURL *handle,
                           QCurl::Internal::CurlOptions::Option option,
                           curl_off_t value)
{
    if (!handle) {
        return false;
    }

    const CURLcode rc = curlEasySetoptWithTestHook(handle, option, value);
    if (rc == CURLE_OK) {
        return true;
    }

    if (isCapabilityRelatedCurlError(rc)) {
        appendCapabilityWarning(d,
                                QStringLiteral("libcurl 不支持 %1（%2）")
                                    .arg(QString::fromUtf8(option.name))
                                    .arg(QString::fromUtf8(curl_easy_strerror(rc))));
        return false;
    }

    appendCapabilityWarning(d,
                            QStringLiteral("设置 %1 失败（%2）")
                                .arg(QString::fromUtf8(option.name))
                                .arg(QString::fromUtf8(curl_easy_strerror(rc))));
    return false;
}

[[nodiscard]] bool disableProxyEnvironmentInheritance(QCNetworkReplyPrivate *reply, CURL *handle)
{
    reply->proxyEnvironmentDisabled = true;
    reply->proxyHostBytes.clear();
    reply->proxyUserBytes.clear();
    reply->proxyPasswordBytes.clear();
    return setRequiredCurlOption(reply, handle, QCURL_CURL_OPTION(CURLOPT_PROXY), "");
}

#ifdef QCURL_ENABLE_ADVANCED_REQUEST_NETWORK_PATH_API
[[nodiscard]] bool buildSlistFromStrings(const QStringList &entries,
                                         curl_slist **output,
                                         QString *error,
                                         const char *optionName)
{
    return QCurl::Internal::CurlOptions::buildCurlSlist(entries, output, error, optionName);
}

#endif // QCURL_ENABLE_ADVANCED_REQUEST_NETWORK_PATH_API

std::optional<long> toCurlSslVersionMin(QCNetworkTlsVersion version)
{
    switch (version) {
        case QCNetworkTlsVersion::Default:
            return std::nullopt;
        case QCNetworkTlsVersion::Tls1_0:
            return static_cast<long>(CURL_SSLVERSION_TLSv1);
        case QCNetworkTlsVersion::Tls1_1:
            return static_cast<long>(CURL_SSLVERSION_TLSv1_1);
        case QCNetworkTlsVersion::Tls1_2:
            return static_cast<long>(CURL_SSLVERSION_TLSv1_2);
        case QCNetworkTlsVersion::Tls1_3:
#ifdef CURL_SSLVERSION_TLSv1_3
            return static_cast<long>(CURL_SSLVERSION_TLSv1_3);
#else
            return std::nullopt;
#endif
    }
    return std::nullopt;
}

} // namespace QCurl::Internal::ReplyCurlOptions::Detail

namespace QCurl {

bool QCNetworkReplyPrivate::configureCurlOptions()
{
    const auto &plan                = curlPlan;
    const auto &normalized          = curlPlan.normalized;
    const QCNetworkRequest &request = normalized.request;
    const auto &bodySpec            = normalized.body;

    CURL *handle = curlManager.handle();
    if (!handle) {
        const QString diagnostic = curlManager.initializationError();
        qCritical().noquote() << "QCNetworkReply:" << diagnostic;
        setError(NetworkError::InvalidRequest,
                 diagnostic.isEmpty() ? QStringLiteral("curl easy handle initialization failed")
                                      : diagnostic);
        return false;
    }

    const auto minimumRuntimeAvailability = CurlFeatureProbe::instance().minimumRuntimeAvailability();
    if (!minimumRuntimeAvailability.supported) {
        setError(NetworkError::InvalidRequest, minimumRuntimeAvailability.reason);
        return false;
    }

    if (!Internal::ReplyCurlOptions::configureReplyBaseAndNetworkOptions(this,
                                                                         handle,
                                                                         normalized,
                                                                         bodySpec)) {
        return false;
    }
    bool hasExplicitAuthorizationHeader = false;
    bool hasSensitiveHeader             = false;
    if (!Internal::ReplyCurlOptions::configureReplyMethodHeadersAndLimits(
            this,
            handle,
            plan,
            normalized,
            bodySpec,
            &hasExplicitAuthorizationHeader,
            &hasSensitiveHeader)) {
        return false;
    }
    if (!Internal::ReplyCurlOptions::configureReplyProxyAndHttpVersion(
            this, handle, normalized, hasExplicitAuthorizationHeader, hasSensitiveHeader)) {
        return false;
    }
    if (!Internal::ReplyCurlOptions::configureReplyTlsOptions(this, handle, request)) {
        return false;
    }
    if (!Internal::ReplyCurlOptions::configureReplyTimeoutsAndCallbacks(this, handle, request)) {
        return false;
    }

    return true;
}

} // namespace QCurl
