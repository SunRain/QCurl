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
    const auto decision = decideHttpVersionPolicy(requestPolicy().httpVersion,
                                                  detail::sampleHttpVersionFacts());
    if (!decision.failure.isEmpty()) {
        result.failure = decision.failure;
        return result;
    }
    if (decision.downgraded) {
        warn(result, QStringLiteral("运行时 libcurl 不支持 HTTP/3，已降级为 HTTP/2TLS"));
    }
    if (decision.nativeVersion) {
        Q_UNUSED(requiredOption(easy,
                                QCURL_CURL_OPTION(CURLOPT_HTTP_VERSION),
                                *decision.nativeVersion,
                                result));
    }
    return result;
}

} // namespace QCurl::Internal
