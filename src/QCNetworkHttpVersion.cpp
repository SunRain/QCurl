#include "QCNetworkHttpVersion.h"

QT_BEGIN_NAMESPACE

namespace QCurl {

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
#ifdef CURL_HTTP_VERSION_3
        // 尝试 HTTP/3，失败则由 libcurl 自动降级
        return CURL_HTTP_VERSION_3;
#else
        // 如果 libcurl 不支持 HTTP/3，降级到 HTTP/2
        return CURL_HTTP_VERSION_2TLS;
#endif
    case QCNetworkHttpVersion::Http3Only:
#ifdef CURL_HTTP_VERSION_3ONLY
        // 仅使用 HTTP/3，失败则报错（不降级）
        return CURL_HTTP_VERSION_3ONLY;
#elif defined(CURL_HTTP_VERSION_3)
        // 如果没有 3ONLY，使用普通 HTTP/3
        return CURL_HTTP_VERSION_3;
#else
        // 如果 libcurl 不支持 HTTP/3，降级到 HTTP/2
        return CURL_HTTP_VERSION_2TLS;
#endif
    case QCNetworkHttpVersion::HttpAny:
        return CURL_HTTP_VERSION_NONE;
    default:
        return CURL_HTTP_VERSION_1_1;
    }
}

} // namespace QCurl
QT_END_NAMESPACE
