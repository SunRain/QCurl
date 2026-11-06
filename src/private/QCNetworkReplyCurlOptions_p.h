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

namespace Internal {

struct CurlPlan;
struct NormalizedRequest;
struct RequestBody;
struct TlsConfigurationResult;

namespace ReplyCurlOptions {

namespace Detail {

Q_DECL_HIDDEN void appendCapabilityWarning(QCNetworkReplyPrivate *reply, const QString &message);
[[nodiscard]] Q_DECL_HIDDEN bool setRequiredOption(QCNetworkReplyPrivate *reply,
                                                   CURLcode code,
                                                   const char *optionName);
template<typename T>
CURLcode curlEasySetoptWithTestHook(CURL *handle,
                                    QCurl::Internal::CurlOptions::Option option,
                                    T value)
{
    return CurlOptions::setWithTestHook(handle, option, value);
}

template<typename T>
[[nodiscard]] bool setRequiredCurlOption(QCNetworkReplyPrivate *reply,
                                         CURL *handle,
                                         QCurl::Internal::CurlOptions::Option option,
                                         T value)
{
    return setRequiredOption(reply, curlEasySetoptWithTestHook(handle, option, value), option.name);
}

Q_DECL_HIDDEN bool setOptionalLongOption(QCNetworkReplyPrivate *reply,
                                         CURL *handle,
                                         QCurl::Internal::CurlOptions::Option option,
                                         long value);
Q_DECL_HIDDEN bool setOptionalStringOption(QCNetworkReplyPrivate *reply,
                                           CURL *handle,
                                           QCurl::Internal::CurlOptions::Option option,
                                           const QByteArray &value);
Q_DECL_HIDDEN bool setOptionalOffTOption(QCNetworkReplyPrivate *reply,
                                         CURL *handle,
                                         QCurl::Internal::CurlOptions::Option option,
                                         curl_off_t value);
[[nodiscard]] Q_DECL_HIDDEN bool disableProxyEnvironmentInheritance(QCNetworkReplyPrivate *reply,
                                                                    CURL *handle);

[[nodiscard]] Q_DECL_HIDDEN bool buildSlistFromStrings(const QStringList &entries,
                                                       curl_slist **output,
                                                       QString *error,
                                                       const char *optionName);

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
/// 按原顺序发布全部 TLS warning，再消费首次 fatal；不重解释策略或原生码。
[[nodiscard]] Q_DECL_HIDDEN bool publishTlsConfigurationResult(QCNetworkReplyPrivate *reply,
                                                               const TlsConfigurationResult &result);
[[nodiscard]] Q_DECL_HIDDEN bool configureReplyHttpVersionOptions(QCNetworkReplyPrivate *reply,
                                                                  CURL *handle,
                                                                  const QCNetworkRequest &request);
[[nodiscard]] Q_DECL_HIDDEN bool configureReplyTimeoutsAndCallbacks(QCNetworkReplyPrivate *reply,
                                                                    CURL *handle,
                                                                    const QCNetworkRequest &request);

} // namespace ReplyCurlOptions
} // namespace Internal
} // namespace QCurl

#endif // QCNETWORKREPLYCURLOPTIONS_P_H
