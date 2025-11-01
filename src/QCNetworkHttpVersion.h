#ifndef QCNETWORKHTTPVERSION_H
#define QCNETWORKHTTPVERSION_H

#include "QCurlConfig.h"
#include <curl/curl.h>

QT_BEGIN_NAMESPACE

namespace QCurl {

/**
 * @brief HTTP 协议版本枚举
 *
 * 用于指定请求使用的 HTTP 版本。libcurl 8.17.0 支持 HTTP/1.0、HTTP/1.1、
 * HTTP/2 和 HTTP/3。
 *
 * @par HTTP/3 支持要求
 * - libcurl >= 7.66.0
 * - 编译时支持 nghttp3/ngtcp2
 * - 服务器支持 HTTP/3 (QUIC)
 *
 * @par 示例：强制使用 HTTP/2
 * @code
 * QCNetworkRequest request(url);
 * request.setHttpVersion(QCNetworkHttpVersion::Http2);
 * @endcode
 *
 * @par 示例：尝试 HTTP/3，失败则降级
 * @code
 * request.setHttpVersion(QCNetworkHttpVersion::Http3);
 * @endcode
 *
 * @par 示例：仅使用 HTTP/3，不降级
 * @code
 * request.setHttpVersion(QCNetworkHttpVersion::Http3Only);
 * @endcode
 *
 */
enum class QCNetworkHttpVersion {
    Http1_0,      ///< HTTP/1.0
    Http1_1,      ///< HTTP/1.1（默认）
    Http2,        ///< HTTP/2（需要 libcurl >= 7.33.0 + nghttp2）
    Http2TLS,     ///< HTTP/2 over TLS（自动协商）
    Http3,        ///< HTTP/3（需要 libcurl >= 7.66.0 + nghttp3）尝试 HTTP/3，失败则降级
    Http3Only,    ///< 仅 HTTP/3，失败则报错（v2.17.0）
    HttpAny       ///< 让 libcurl 自动选择最优版本
};

/**
 * @brief 将 QCNetworkHttpVersion 转换为 libcurl 常量
 *
 * @param version HTTP 版本枚举
 * @return long libcurl 的 CURL_HTTP_VERSION_* 常量
 */
[[nodiscard]] long toCurlHttpVersion(QCNetworkHttpVersion version) noexcept;

} // namespace QCurl
QT_END_NAMESPACE

#endif // QCNETWORKHTTPVERSION_H
