/**
 * @file
 * @brief QCNetworkReply method, body, header, and transfer-limit curl options.
 */

#include "CurlFeatureProbe.h"
#include "QCNetworkConnectionPoolManager_p.h"
#include "QCNetworkError.h"
#include "QCNetworkHttpHeaders.h"
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

struct HeaderFacts
{
    bool hasExplicitAuthorization  = false;
    bool hasExplicitReferer        = false;
    bool hasExplicitAcceptEncoding = false;
    bool hasSensitive              = false;
};

[[nodiscard]] bool setMethodFlag(QCNetworkReplyPrivate *reply,
                                 CURL *handle,
                                 bool enabled,
                                 QCurl::Internal::CurlOptions::Option option)
{
    return !enabled
           || setRequiredOption(reply,
                                Internal::CurlOptions::setEnabled(handle, option.id, true),
                                option.name);
}

[[nodiscard]] bool configureMethodSelection(QCNetworkReplyPrivate *reply,
                                            CURL *handle,
                                            const Internal::CurlPlan &plan)
{
    if (!setMethodFlag(reply, handle, plan.setNoBody, QCURL_CURL_OPTION(CURLOPT_NOBODY))
        || !setMethodFlag(reply, handle, plan.setHttpGet, QCURL_CURL_OPTION(CURLOPT_HTTPGET))
        || !setMethodFlag(reply, handle, plan.setPost, QCURL_CURL_OPTION(CURLOPT_POST))
        || !setMethodFlag(reply, handle, plan.setUpload, QCURL_CURL_OPTION(CURLOPT_UPLOAD))) {
        return false;
    }
    return plan.customRequest.isEmpty()
           || setRequiredCurlOption(reply,
                                    handle,
                                    QCURL_CURL_OPTION(CURLOPT_CUSTOMREQUEST),
                                    plan.customRequest.constData());
}

[[nodiscard]] bool configureInlineBody(QCNetworkReplyPrivate *reply,
                                       CURL *handle,
                                       const Internal::CurlPlan &plan,
                                       const Internal::RequestBody &body)
{
    return setRequiredCurlOption(reply,
                                 handle,
                                 QCURL_CURL_OPTION(CURLOPT_POSTFIELDS),
                                 body.inlineBytes.constData())
           && setRequiredCurlOption(reply,
                                    handle,
                                    QCURL_CURL_OPTION(CURLOPT_POSTFIELDSIZE_LARGE),
                                    static_cast<curl_off_t>(plan.bodySizeBytes));
}

[[nodiscard]] bool configureDeviceBody(QCNetworkReplyPrivate *reply,
                                       CURL *handle,
                                       const Internal::CurlPlan &plan)
{
    if (!reply->requestBodySource.device) {
        reply->setError(NetworkError::InvalidRequest,
                        QStringLiteral("request body source 需要有效的设备来源"));
        return false;
    }
    if (!setRequiredCurlOption(reply,
                               handle,
                               QCURL_CURL_OPTION(CURLOPT_READFUNCTION),
                               QCNetworkReplyPrivate::curlReadCallback)
        || !setRequiredCurlOption(reply, handle, QCURL_CURL_OPTION(CURLOPT_READDATA), reply)) {
        return false;
    }
    reply->readCallbackConfigured = true;

    const curl_off_t size = static_cast<curl_off_t>(reply->requestBodySource.sizeBytes);
    if (plan.setPost) {
        return setRequiredCurlOption(reply,
                                     handle,
                                     QCURL_CURL_OPTION(CURLOPT_POSTFIELDS),
                                     static_cast<const char *>(nullptr))
               && setRequiredCurlOption(reply,
                                        handle,
                                        QCURL_CURL_OPTION(CURLOPT_POSTFIELDSIZE_LARGE),
                                        size);
    }
    return !plan.setUpload
           || setRequiredCurlOption(reply,
                                    handle,
                                    QCURL_CURL_OPTION(CURLOPT_INFILESIZE_LARGE),
                                    size);
}

[[nodiscard]] bool configureTransferBody(QCNetworkReplyPrivate *reply,
                                         CURL *handle,
                                         const Internal::CurlPlan &plan,
                                         const Internal::RequestBody &body)
{
    switch (plan.transferMode) {
        case Internal::CurlTransferMode::None:
            return true;
        case Internal::CurlTransferMode::InlineBytes:
            return configureInlineBody(reply, handle, plan, body);
        case Internal::CurlTransferMode::RequestBodySource:
            return configureDeviceBody(reply, handle, plan);
    }
    return false;
}

void configureExpectContinueTimeout(QCNetworkReplyPrivate *reply,
                                    CURL *handle,
                                    const Internal::CurlPlan &plan,
                                    const Internal::NormalizedRequest &normalized)
{
    const auto timeout = normalized.request.expect100ContinueTimeout();
    if (!timeout.has_value()) {
        return;
    }
    const bool supportedMethod = normalized.method == HttpMethod::Put
                                 || normalized.method == HttpMethod::Post;
    if (!supportedMethod || !plan.hasRequestBody) {
        appendCapabilityWarning(
            reply,
            QStringLiteral(
                "请求配置：Expect: 100-continue timeout 仅对 PUT/POST 且有 body 生效，已忽略"));
        return;
    }

    const long long milliseconds = static_cast<long long>(timeout->count());
    if (milliseconds < 0) {
        appendCapabilityWarning(reply,
                                QStringLiteral(
                                    "请求配置：Expect: 100-continue timeout 必须 >= 0（已忽略）"));
        return;
    }
    const long maximum = std::numeric_limits<long>::max();
    const long value   = milliseconds > static_cast<long long>(maximum)
                             ? maximum
                             : static_cast<long>(milliseconds);
    if (milliseconds > static_cast<long long>(maximum)) {
        appendCapabilityWarning(
            reply,
            QStringLiteral("请求配置：Expect: 100-continue timeout 过大，已截断为 LONG_MAX ms"));
    }
    setOptionalLongOption(reply, handle, QCURL_CURL_OPTION(CURLOPT_EXPECT_100_TIMEOUT_MS), value);
}

void recordHeaderFact(const QByteArray &normalizedName, HeaderFacts *facts)
{
    if (normalizedName == QCurl::httpheaders::kAuthorization.toLower()) {
        facts->hasExplicitAuthorization = true;
        facts->hasSensitive             = true;
    } else if (normalizedName == QCurl::httpheaders::kProxyAuthorization.toLower()
               || normalizedName == QCurl::httpheaders::kCookie.toLower()
               || normalizedName == QCurl::httpheaders::kSetCookie.toLower()) {
        facts->hasSensitive = true;
    } else if (normalizedName == QCurl::httpheaders::kReferer.toLower()) {
        facts->hasExplicitReferer = true;
    } else if (normalizedName == QCurl::httpheaders::kAcceptEncoding.toLower()) {
        facts->hasExplicitAcceptEncoding = true;
    }
}

[[nodiscard]] bool appendRequestHeaders(QCNetworkReplyPrivate *reply,
                                        CURL *handle,
                                        const QCNetworkRequest &request,
                                        HeaderFacts *facts)
{
    for (const QByteArray &headerName : request.rawHeaderList()) {
        recordHeaderFact(headerName.trimmed().toLower(), facts);
        const QString headerLine = QString::fromUtf8(headerName) + QStringLiteral(": ")
                                   + QString::fromUtf8(request.rawHeader(headerName));
        if (!reply->curlManager.appendHeader(headerLine)) {
            reply->setError(NetworkError::InvalidRequest,
                            QStringLiteral("追加 CURLOPT_HTTPHEADER 失败"));
            return false;
        }
    }

    return !reply->curlManager.headerList()
           || setRequiredCurlOption(reply,
                                    handle,
                                    QCURL_CURL_OPTION(CURLOPT_HTTPHEADER),
                                    reply->curlManager.headerList());
}

void configureReferer(QCNetworkReplyPrivate *reply,
                      CURL *handle,
                      const QCNetworkRequest &request,
                      const HeaderFacts &facts)
{
    if (!facts.hasExplicitReferer && !request.referer().isEmpty()) {
        reply->refererBytes = request.referer().toUtf8();
        setOptionalStringOption(reply,
                                handle,
                                QCURL_CURL_OPTION(CURLOPT_REFERER),
                                reply->refererBytes);
    } else if (facts.hasExplicitReferer && !request.referer().isEmpty()) {
        appendCapabilityWarning(
            reply,
            QStringLiteral(
                "请求配置冲突：已显式设置 Referer header，将忽略 request.setReferer(...)"));
    }
}

QStringList normalizedAcceptedEncodings(const QStringList &encodings)
{
    QStringList result;
    result.reserve(encodings.size());
    for (const QString &encoding : encodings) {
        const QString trimmed = encoding.trimmed();
        if (!trimmed.isEmpty()) {
            result.append(trimmed);
        }
    }
    return result;
}

void configureAcceptEncoding(QCNetworkReplyPrivate *reply,
                             CURL *handle,
                             const QCNetworkRequest &request,
                             const HeaderFacts &facts)
{
    if (facts.hasExplicitAcceptEncoding) {
        if (request.autoDecompressionEnabled() || !request.acceptedEncodings().isEmpty()) {
            appendCapabilityWarning(reply,
                                    QStringLiteral(
                                        "请求配置冲突：已显式设置 Accept-Encoding header，将忽略 "
                                        "autoDecompression/acceptedEncodings（不会自动解压）"));
        }
        return;
    }
    if (!request.autoDecompressionEnabled()) {
        return;
    }

    const QStringList configuredEncodings = request.acceptedEncodings();
    const QStringList encodings           = normalizedAcceptedEncodings(configuredEncodings);
    if (!configuredEncodings.isEmpty() && encodings.isEmpty()) {
        return;
    }
    reply->acceptEncodingBytes = configuredEncodings.isEmpty()
                                     ? QByteArray("")
                                     : encodings.join(QLatin1Char(',')).toUtf8();
    setOptionalStringOption(reply,
                            handle,
                            QCURL_CURL_OPTION(CURLOPT_ACCEPT_ENCODING),
                            reply->acceptEncodingBytes);
}

void configureTransferLimits(QCNetworkReplyPrivate *reply,
                             CURL *handle,
                             const QCNetworkRequest &request)
{
    if (const auto limit = request.maxDownloadBytesPerSec();
        limit.has_value() && limit.value() > 0) {
        setOptionalOffTOption(reply,
                              handle,
                              QCURL_CURL_OPTION(CURLOPT_MAX_RECV_SPEED_LARGE),
                              static_cast<curl_off_t>(limit.value()));
    }
    if (const auto limit = request.maxUploadBytesPerSec(); limit.has_value() && limit.value() > 0) {
        setOptionalOffTOption(reply,
                              handle,
                              QCURL_CURL_OPTION(CURLOPT_MAX_SEND_SPEED_LARGE),
                              static_cast<curl_off_t>(limit.value()));
    }
}

} // namespace

[[nodiscard]] bool configureReplyMethodHeadersAndLimits(QCNetworkReplyPrivate *reply,
                                                        CURL *handle,
                                                        const Internal::CurlPlan &plan,
                                                        const Internal::NormalizedRequest &normalized,
                                                        const Internal::RequestBody &bodySpec,
                                                        bool *hasExplicitAuthorizationHeaderOut,
                                                        bool *hasSensitiveHeaderOut)
{
    if (!configureMethodSelection(reply, handle, plan)
        || !configureTransferBody(reply, handle, plan, bodySpec)) {
        return false;
    }
    configureExpectContinueTimeout(reply, handle, plan, normalized);

    HeaderFacts facts;
    if (!appendRequestHeaders(reply, handle, normalized.request, &facts)) {
        return false;
    }
    configureReferer(reply, handle, normalized.request, facts);
    configureAcceptEncoding(reply, handle, normalized.request, facts);
    configureTransferLimits(reply, handle, normalized.request);

    if (hasExplicitAuthorizationHeaderOut) {
        *hasExplicitAuthorizationHeaderOut = facts.hasExplicitAuthorization;
    }
    if (hasSensitiveHeaderOut) {
        *hasSensitiveHeaderOut = facts.hasSensitive;
    }
    return true;
}

} // namespace QCurl::Internal::ReplyCurlOptions
