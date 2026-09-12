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
#if LIBCURL_VERSION_NUM >= 0x075800
            // 仅使用 HTTP/3，失败则报错（不降级）
            return CURL_HTTP_VERSION_3ONLY;
#else
            return -1;
#endif
        case QCNetworkHttpVersion::HttpAny:
            return CURL_HTTP_VERSION_NONE;
        default:
            return CURL_HTTP_VERSION_1_1;
    }
}

bool resolveHttpVersion(QCNetworkHttpVersion requested,
                        QCNetworkHttpVersion *effective,
                        QString *error,
                        QString *warning)
{
    *effective = requested;
    error->clear();
    warning->clear();
    if (requested != QCNetworkHttpVersion::Http3 && requested != QCNetworkHttpVersion::Http3Only) {
        return true;
    }
    const auto &probe   = CurlFeatureProbe::instance();
    const bool hasHttp3 = (probe.runtimeFeatures() & CURL_VERSION_HTTP3) != 0;
    if (requested == QCNetworkHttpVersion::Http3Only
        && (toCurlHttpVersion(requested) < 0 || probe.runtimeVersionNum() < 0x075800 || !hasHttp3)) {
        *error = QStringLiteral(
            "Http3Only 需要编译及运行时 libcurl >= 7.88.0 且支持 HTTP/3；禁止降级");
        return false;
    }
    if (!hasHttp3 && qgetenv("QCURL_REQUIRE_HTTP3").trimmed() == "1") {
        *error = QStringLiteral("QCURL_REQUIRE_HTTP3=1：运行时 libcurl 不支持 HTTP/3，请求被拒绝");
        return false;
    }
    if (!hasHttp3) {
        *effective = QCNetworkHttpVersion::Http2TLS;
        *warning   = QStringLiteral("运行时 libcurl 不支持 HTTP/3，已降级为 HTTP/2TLS");
    }
    return true;
}

} // namespace QCurl::detail
