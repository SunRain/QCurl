#include "CurlFeatureProbe.h"
#include "private/QCNetworkHttpVersion_p.h"

#include <curl/curl.h>

namespace QCurl::detail {

long toCurlHttpVersion(QCNetworkHttpVersion version) noexcept
{
    switch (version) {
        case QCNetworkHttpVersion::Http1_0:
            return CURL_HTTP_VERSION_1_0;
        case QCNetworkHttpVersion::Http1_1:
            return CURL_HTTP_VERSION_1_1;
        case QCNetworkHttpVersion::Http2:
            return CURL_HTTP_VERSION_2_0;
        case QCNetworkHttpVersion::Http2TLS:
            return CURL_HTTP_VERSION_2TLS;
        case QCNetworkHttpVersion::Http3:
            // 尝试 HTTP/3，失败则由 libcurl 自动降级
            return CURL_HTTP_VERSION_3;
        case QCNetworkHttpVersion::Http3Only:
            // 最低依赖已包含该枚举；仅使用 HTTP/3，失败则报错。
            return CURL_HTTP_VERSION_3ONLY;
        case QCNetworkHttpVersion::HttpAny:
            return CURL_HTTP_VERSION_NONE;
        default:
            return CURL_HTTP_VERSION_1_1;
    }
}

Internal::HttpVersionFacts sampleHttpVersionFacts()
{
    const auto &probe = CurlFeatureProbe::instance();
    return {probe.runtimeVersionNum(),
            (probe.runtimeFeatures() & CURL_VERSION_HTTP3) != 0,
            qgetenv("QCURL_REQUIRE_HTTP3").trimmed() == "1"};
}

} // namespace QCurl::detail
