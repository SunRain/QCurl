/**
 * @file
 * @brief 应用 method、body size、请求头与传输限速规则。
 */

#include "QCCurlHandleManager.h"
#include "QCNetworkHttpHeaders.h"
#include "private/QCReplyCurlConfiguration_p.h"

#include <limits>

namespace QCurl::Internal {
namespace {

void recordHeaderFact(const QByteArray &name, ReplyCurlHeaderFacts &facts)
{
    if (name == httpheaders::kAuthorization.toLower()) {
        facts.hasExplicitAuthorization = true;
        facts.hasSensitive             = true;
    } else if (name == httpheaders::kProxyAuthorization.toLower()
               || name == httpheaders::kCookie.toLower()
               || name == httpheaders::kSetCookie.toLower()) {
        facts.hasSensitive = true;
    } else if (name == httpheaders::kReferer.toLower()) {
        facts.hasExplicitReferer = true;
    } else if (name == httpheaders::kAcceptEncoding.toLower()) {
        facts.hasExplicitAcceptEncoding = true;
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

} // namespace

ReplyCurlConfigurationResult QCReplyCurlConfiguration::applyMethod(CURL *easy) const
{
    ReplyCurlConfigurationResult result;
    const auto setFlag = [&](bool enabled, CurlOptions::Option option) {
        return !enabled
               || required(CurlOptions::setEnabled(easy, option.id, true), option.name, result);
    };
    if (!setFlag(m_plan.setNoBody, QCURL_CURL_OPTION(CURLOPT_NOBODY))
        || !setFlag(m_plan.setHttpGet, QCURL_CURL_OPTION(CURLOPT_HTTPGET))
        || !setFlag(m_plan.setPost, QCURL_CURL_OPTION(CURLOPT_POST))
        || !setFlag(m_plan.setUpload, QCURL_CURL_OPTION(CURLOPT_UPLOAD))) {
        return result;
    }
    if (!m_plan.customRequest.isEmpty()) {
        Q_UNUSED(requiredOption(easy,
                                QCURL_CURL_OPTION(CURLOPT_CUSTOMREQUEST),
                                m_plan.customRequest.constData(),
                                result));
    }
    return result;
}

ReplyCurlConfigurationResult QCReplyCurlConfiguration::applyTransferBody(CURL *easy,
                                                                         qint64 bodySourceSize) const
{
    ReplyCurlConfigurationResult result;
    switch (m_plan.transferMode) {
        case CurlTransferMode::None:
            break;
        case CurlTransferMode::InlineBytes:
            if (requiredOption(easy,
                               QCURL_CURL_OPTION(CURLOPT_POSTFIELDS),
                               m_plan.normalized.body.inlineBytes.constData(),
                               result)) {
                Q_UNUSED(requiredOption(easy,
                                        QCURL_CURL_OPTION(CURLOPT_POSTFIELDSIZE_LARGE),
                                        static_cast<curl_off_t>(m_plan.bodySizeBytes),
                                        result));
            }
            break;
        case CurlTransferMode::RequestBodySource:
            if (m_plan.setPost) {
                if (requiredOption(easy,
                                   QCURL_CURL_OPTION(CURLOPT_POSTFIELDS),
                                   static_cast<const char *>(nullptr),
                                   result)) {
                    Q_UNUSED(requiredOption(easy,
                                            QCURL_CURL_OPTION(CURLOPT_POSTFIELDSIZE_LARGE),
                                            static_cast<curl_off_t>(bodySourceSize),
                                            result));
                }
            } else if (m_plan.setUpload) {
                Q_UNUSED(requiredOption(easy,
                                        QCURL_CURL_OPTION(CURLOPT_INFILESIZE_LARGE),
                                        static_cast<curl_off_t>(bodySourceSize),
                                        result));
            }
            break;
    }
    return result;
}

void QCReplyCurlConfiguration::configureExpectContinueTimeout(
    CURL *easy, ReplyCurlConfigurationResult &result) const
{
    const auto &normalized = m_plan.normalized;
    const auto timeout     = normalized.request.expect100ContinueTimeout();
    if (!timeout) {
        return;
    }
    if ((normalized.method != HttpMethod::Put && normalized.method != HttpMethod::Post)
        || !m_plan.hasRequestBody) {
        warn(result,
             QStringLiteral("请求配置：Expect: 100-continue timeout 仅对 PUT/POST 且有 body "
                            "生效，已忽略"));
        return;
    }
    const long long milliseconds = static_cast<long long>(timeout->count());
    if (milliseconds < 0) {
        warn(result, QStringLiteral("请求配置：Expect: 100-continue timeout 必须 >= 0（已忽略）"));
        return;
    }
    const long maximum = std::numeric_limits<long>::max();
    const long value   = milliseconds > static_cast<long long>(maximum)
                             ? maximum
                             : static_cast<long>(milliseconds);
    if (milliseconds > static_cast<long long>(maximum)) {
        warn(result,
             QStringLiteral("请求配置：Expect: 100-continue timeout 过大，已截断为 LONG_MAX ms"));
    }
    optionalOption(easy, QCURL_CURL_OPTION(CURLOPT_EXPECT_100_TIMEOUT_MS), value, result);
}

bool QCReplyCurlConfiguration::appendRequestHeaders(QCCurlHandleManager &handle,
                                                    ReplyCurlHeaderFacts &facts,
                                                    ReplyCurlConfigurationResult &result) const
{
    const auto &request = m_plan.normalized.request;
    for (const QByteArray &name : request.rawHeaderList()) {
        recordHeaderFact(name.trimmed().toLower(), facts);
        const QString line = QString::fromUtf8(name) + QStringLiteral(": ")
                             + QString::fromUtf8(request.rawHeader(name));
        if (!handle.appendHeader(line)) {
            result.failure = QStringLiteral("追加 CURLOPT_HTTPHEADER 失败");
            return false;
        }
    }
    return !handle.headerList()
           || requiredOption(handle.handle(),
                             QCURL_CURL_OPTION(CURLOPT_HTTPHEADER),
                             handle.headerList(),
                             result);
}

void QCReplyCurlConfiguration::configureReferer(CURL *easy,
                                                const ReplyCurlHeaderFacts &facts,
                                                ReplyCurlConfigurationResult &result)
{
    const auto &request = m_plan.normalized.request;
    if (!facts.hasExplicitReferer && !request.referer().isEmpty()) {
        m_refererBytes = request.referer().toUtf8();
        optionalOption(easy, QCURL_CURL_OPTION(CURLOPT_REFERER), m_refererBytes.constData(), result);
    } else if (facts.hasExplicitReferer && !request.referer().isEmpty()) {
        warn(result,
             QStringLiteral("请求配置冲突：已显式设置 Referer header，将忽略 "
                            "request.setReferer(...)"));
    }
}

void QCReplyCurlConfiguration::appendAcceptEncodingConflictWarning(
    const ReplyCurlHeaderFacts &facts, ReplyCurlConfigurationResult &result) const
{
    const auto &request = m_plan.normalized.request;
    if (facts.hasExplicitAcceptEncoding
        && (request.autoDecompressionEnabled() || !request.acceptedEncodings().isEmpty())) {
        warn(result,
             QStringLiteral("请求配置冲突：已显式设置 Accept-Encoding header，将忽略 "
                            "autoDecompression/acceptedEncodings（不会自动解压）"));
    }
}

void QCReplyCurlConfiguration::configureAcceptEncoding(CURL *easy,
                                                       const ReplyCurlHeaderFacts &facts,
                                                       ReplyCurlConfigurationResult &result)
{
    const auto &request = m_plan.normalized.request;
    if (facts.hasExplicitAcceptEncoding) {
        appendAcceptEncodingConflictWarning(facts, result);
        return;
    }
    if (!request.autoDecompressionEnabled()) {
        return;
    }
    const QStringList configured = request.acceptedEncodings();
    const QStringList encodings  = normalizedAcceptedEncodings(configured);
    if (!configured.isEmpty() && encodings.isEmpty()) {
        return;
    }
    m_acceptEncodingBytes = configured.isEmpty() ? QByteArray("")
                                                 : encodings.join(QLatin1Char(',')).toUtf8();
    optionalOption(easy,
                   QCURL_CURL_OPTION(CURLOPT_ACCEPT_ENCODING),
                   m_acceptEncodingBytes.constData(),
                   result);
}

void QCReplyCurlConfiguration::configureTransferLimits(CURL *easy,
                                                       ReplyCurlConfigurationResult &result) const
{
    const auto &request = m_plan.normalized.request;
    if (const auto limit = request.maxDownloadBytesPerSec(); limit && *limit > 0) {
        optionalOption(easy,
                       QCURL_CURL_OPTION(CURLOPT_MAX_RECV_SPEED_LARGE),
                       static_cast<curl_off_t>(*limit),
                       result);
    }
    if (const auto limit = request.maxUploadBytesPerSec(); limit && *limit > 0) {
        optionalOption(easy,
                       QCURL_CURL_OPTION(CURLOPT_MAX_SEND_SPEED_LARGE),
                       static_cast<curl_off_t>(*limit),
                       result);
    }
}

ReplyCurlConfigurationResult QCReplyCurlConfiguration::applyHeadersAndLimits(
    QCCurlHandleManager &handle, ReplyCurlHeaderFacts &facts)
{
    ReplyCurlConfigurationResult result;
    configureExpectContinueTimeout(handle.handle(), result);
    facts = {};
    if (!appendRequestHeaders(handle, facts, result)) {
        return result;
    }
    configureReferer(handle.handle(), facts, result);
    configureAcceptEncoding(handle.handle(), facts, result);
    configureTransferLimits(handle.handle(), result);
    return result;
}

ReplyCurlConfigurationResult QCReplyCurlConfiguration::mockConflictWarnings() const
{
    ReplyCurlHeaderFacts facts;
    for (const QByteArray &name : m_plan.normalized.request.rawHeaderList()) {
        recordHeaderFact(name.trimmed().toLower(), facts);
    }
    ReplyCurlConfigurationResult result;
    appendAcceptEncodingConflictWarning(facts, result);
    return result;
}

} // namespace QCurl::Internal
