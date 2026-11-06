/**
 * @file
 * @brief 应用请求 HTTP 版本及原有能力诊断。
 */

#include "private/QCNetworkHttpVersion_p.h"
#include "private/QCReplyCurlConfiguration_p.h"

namespace QCurl::Internal {

ReplyCurlConfigurationResult QCReplyCurlConfiguration::applyHttpVersion(CURL *easy) const
{
    ReplyCurlConfigurationResult result;
    const auto &request                  = m_plan.normalized.request;
    const QCNetworkHttpVersion requested = request.httpVersion();
    QCNetworkHttpVersion effective       = requested;
    QString error;
    QString warning;
    if (!detail::resolveHttpVersion(requested, &effective, &error, &warning)) {
        result.failure = error;
        return result;
    }
    if (!warning.isEmpty()) {
        warn(result, warning);
    }
    if (effective != QCNetworkHttpVersion::Http1_1 || request.isHttpVersionExplicit()) {
        Q_UNUSED(requiredOption(easy,
                                QCURL_CURL_OPTION(CURLOPT_HTTP_VERSION),
                                detail::toCurlHttpVersion(effective),
                                result));
    }
    return result;
}

} // namespace QCurl::Internal
