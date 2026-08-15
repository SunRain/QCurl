/**
 * @file
 * @brief Internal QCNetworkReply curl option section contracts.
 */

#ifndef QCNETWORKREPLYCURLOPTIONS_P_H
#define QCNETWORKREPLYCURLOPTIONS_P_H

#include "QCCurlOptionAdapter_p.h"

#include <QByteArray>
#include <QString>
#include <QtGlobal>

#include <curl/curl.h>
#include <optional>

namespace QCurl {

class QCNetworkReplyPrivate;
class QCNetworkRequest;
class QCNetworkProxyConfig;
enum class QCNetworkTlsVersion;
enum class QCUnsupportedSecurityOptionPolicy;

namespace Internal {

struct CurlPlan;
struct NormalizedRequest;
struct RequestBody;

namespace ReplyCurlOptions {

namespace Detail {

[[nodiscard]] Q_DECL_HIDDEN bool isCapabilityRelatedCurlError(CURLcode code);
Q_DECL_HIDDEN void appendCapabilityWarning(QCNetworkReplyPrivate *reply, const QString &message);
[[nodiscard]] Q_DECL_HIDDEN bool setRequiredOption(QCNetworkReplyPrivate *reply,
                                                   CURLcode code,
                                                   const char *optionName);
[[nodiscard]] Q_DECL_HIDDEN bool handleSecurityOptionResult(QCNetworkReplyPrivate *reply,
                                                            CURLcode code,
                                                            const char *optionName,
                                                            QCUnsupportedSecurityOptionPolicy policy);

template<typename T>
CURLcode curlEasySetoptWithTestHook(CURL *handle, CURLoption option, const char *optionName, T value)
{
    return CurlOptions::setWithTestHook(handle, option, optionName, value);
}

template<typename T>
[[nodiscard]] bool setRequiredCurlOption(
    QCNetworkReplyPrivate *reply, CURL *handle, CURLoption option, const char *optionName, T value)
{
    return setRequiredOption(reply,
                             curlEasySetoptWithTestHook(handle, option, optionName, value),
                             optionName);
}

Q_DECL_HIDDEN bool setOptionalLongOption(QCNetworkReplyPrivate *reply,
                                         CURL *handle,
                                         CURLoption option,
                                         const char *optionName,
                                         long value);
Q_DECL_HIDDEN bool setOptionalStringOption(QCNetworkReplyPrivate *reply,
                                           CURL *handle,
                                           CURLoption option,
                                           const char *optionName,
                                           const QByteArray &value);
Q_DECL_HIDDEN bool setOptionalOffTOption(QCNetworkReplyPrivate *reply,
                                         CURL *handle,
                                         CURLoption option,
                                         const char *optionName,
                                         curl_off_t value);
[[nodiscard]] Q_DECL_HIDDEN bool disableProxyEnvironmentInheritance(QCNetworkReplyPrivate *reply,
                                                                    CURL *handle);

#ifdef QCURL_ENABLE_ADVANCED_REQUEST_NETWORK_PATH_API
[[nodiscard]] Q_DECL_HIDDEN bool buildSlistFromStrings(const QStringList &entries,
                                                       curl_slist **output,
                                                       QString *error,
                                                       const char *optionName);
#endif

Q_DECL_HIDDEN std::optional<long> toCurlSslVersionMin(QCNetworkTlsVersion version);

} // namespace Detail

[[nodiscard]] Q_DECL_HIDDEN bool configureReplyBaseAndNetworkOptions(
    QCNetworkReplyPrivate *reply,
    CURL *handle,
    const NormalizedRequest &normalized,
    const RequestBody &bodySpec);
[[nodiscard]] Q_DECL_HIDDEN bool configureReplyMethodHeadersAndLimits(
    QCNetworkReplyPrivate *reply,
    CURL *handle,
    const CurlPlan &plan,
    const NormalizedRequest &normalized,
    const RequestBody &bodySpec,
    bool *hasExplicitAuthorizationHeaderOut,
    bool *hasSensitiveHeaderOut);
[[nodiscard]] Q_DECL_HIDDEN bool configureReplyProxyAndHttpVersion(
    QCNetworkReplyPrivate *reply,
    CURL *handle,
    const NormalizedRequest &normalized,
    bool hasExplicitAuthorizationHeader,
    bool hasSensitiveHeader);
[[nodiscard]] Q_DECL_HIDDEN bool configureReplyProxyTlsOptions(
    QCNetworkReplyPrivate *reply,
    CURL *handle,
    const QCNetworkProxyConfig &proxyConfig,
    long curlProxyType);
[[nodiscard]] Q_DECL_HIDDEN bool configureReplyHttpVersionOptions(QCNetworkReplyPrivate *reply,
                                                                  CURL *handle,
                                                                  const QCNetworkRequest &request);
[[nodiscard]] Q_DECL_HIDDEN bool configureReplyTlsOptions(QCNetworkReplyPrivate *reply,
                                                          CURL *handle,
                                                          const QCNetworkRequest &request);
[[nodiscard]] Q_DECL_HIDDEN bool configureReplyTimeoutsAndCallbacks(QCNetworkReplyPrivate *reply,
                                                                    CURL *handle,
                                                                    const QCNetworkRequest &request);

} // namespace ReplyCurlOptions
} // namespace Internal
} // namespace QCurl

#endif // QCNETWORKREPLYCURLOPTIONS_P_H
