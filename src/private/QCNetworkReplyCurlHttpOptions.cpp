/**
 * @file
 * @brief QCNetworkReply connection-pool and HTTP-version curl options.
 */

#include "CurlFeatureProbe.h"
#include "QCNetworkConnectionPoolManager_p.h"
#include "QCNetworkError.h"
#include "QCNetworkHttpVersion.h"
#include "QCNetworkReply_p.h"
#include "QCNetworkRequest.h"
#include "private/QCNetworkHttpVersion_p.h"
#include "private/QCNetworkReplyCurlOptions_p.h"

namespace QCurl::Internal::ReplyCurlOptions {

using namespace Detail;

namespace {

[[nodiscard]] bool resolveEffectiveHttpVersion(QCNetworkReplyPrivate *reply,
                                               QCNetworkHttpVersion requested,
                                               bool runtimeHasHttp3,
                                               bool requireHttp3,
                                               QCNetworkHttpVersion *effective)
{
    const bool requestsHttp3 = requested == QCNetworkHttpVersion::Http3
                               || requested == QCNetworkHttpVersion::Http3Only;
    if (requireHttp3 && requestsHttp3 && !runtimeHasHttp3) {
        reply->setError(NetworkError::InvalidRequest,
                        QStringLiteral("交付门禁 QCURL_REQUIRE_HTTP3=1：运行时 libcurl 不支持 "
                                       "HTTP/3（CURL_VERSION_HTTP3 缺失），请求被拒绝"));
        return false;
    }
    if (requested == QCNetworkHttpVersion::Http3Only && !runtimeHasHttp3) {
        reply->setError(
            NetworkError::InvalidRequest,
            QStringLiteral(
                "运行时 libcurl 不支持 HTTP/3（CURL_VERSION_HTTP3 缺失），Http3Only 无法执行"));
        return false;
    }
    if (requested == QCNetworkHttpVersion::Http3Only) {
#if !defined(CURL_HTTP_VERSION_3ONLY)
        appendCapabilityWarning(reply,
                                QStringLiteral(
                                    "当前构建的 libcurl 不支持 CURL_HTTP_VERSION_3ONLY，Http3Only "
                                    "将退化为 Http3（可能发生协议降级）"));
#endif
    } else if (requested == QCNetworkHttpVersion::Http3 && !runtimeHasHttp3) {
        *effective = QCNetworkHttpVersion::Http2TLS;
        appendCapabilityWarning(
            reply,
            QStringLiteral(
                "运行时 libcurl 不支持 HTTP/3（CURL_VERSION_HTTP3 缺失），已降级为 HTTP/2TLS"));
    }
    return true;
}

} // namespace

[[nodiscard]] bool configureReplyHttpVersionOptions(QCNetworkReplyPrivate *reply,
                                                    CURL *handle,
                                                    const QCNetworkRequest &request)
{
    const QCNetworkHttpVersion requested = request.httpVersion();
    QCNetworkHttpVersion effective       = requested;
    const bool runtimeHasHttp3           = (CurlFeatureProbe::instance().runtimeFeatures()
                                            & CURL_VERSION_HTTP3)
                                           != 0;
    const bool requireHttp3              = qgetenv("QCURL_REQUIRE_HTTP3").trimmed() == "1";
    if (!resolveEffectiveHttpVersion(reply, requested, runtimeHasHttp3, requireHttp3, &effective)) {
        return false;
    }

    return (effective == QCNetworkHttpVersion::Http1_1 && !request.isHttpVersionExplicit())
           || setRequiredCurlOption(reply,
                                    handle,
                                    CURLOPT_HTTP_VERSION,
                                    "CURLOPT_HTTP_VERSION",
                                    detail::toCurlHttpVersion(effective));
}

} // namespace QCurl::Internal::ReplyCurlOptions
