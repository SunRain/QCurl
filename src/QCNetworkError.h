#ifndef QCNETWORKERROR_H
#define QCNETWORKERROR_H

#include <QString>
#include <curl/curl.h>

QT_BEGIN_NAMESPACE

namespace QCurl {

/**
 * @brief 网络错误码枚举
 *
 * 定义了高层次的网络错误类型，比直接使用 libcurl 错误码更易理解。
 * 保留了与 libcurl 错误码的映射关系。
 *
 * @par 错误分类
 * - 0-99: 无错误和通用错误
 * - 100-199: 连接错误
 * - 400-599: HTTP 状态码错误
 * - 1000+: libcurl 底层错误（CURLE_* + 1000）
 *
 */
enum class NetworkError {
    // 无错误
    NoError = 0,

    // 连接错误
    ConnectionRefused = 7,        ///< 连接被拒绝
    ConnectionTimeout = 28,       ///< 连接超时
    HostNotFound = 6,             ///< 主机名解析失败
    SslHandshakeFailed = 60,      ///< SSL 握手失败
    TooManyRedirects = 47,        ///< 重定向次数过多

    // HTTP 状态码错误（直接映射）
    HttpBadRequest = 400,         ///< HTTP 400 Bad Request
    HttpUnauthorized = 401,       ///< HTTP 401 Unauthorized
    HttpForbidden = 403,          ///< HTTP 403 Forbidden
    HttpNotFound = 404,           ///< HTTP 404 Not Found
    HttpMethodNotAllowed = 405,   ///< HTTP 405 Method Not Allowed
    HttpTimeout = 408,            ///< HTTP 408 Request Timeout
    HttpInternalServerError = 500,///< HTTP 500 Internal Server Error
    HttpBadGateway = 502,         ///< HTTP 502 Bad Gateway
    HttpServiceUnavailable = 503, ///< HTTP 503 Service Unavailable
    HttpGatewayTimeout = 504,     ///< HTTP 504 Gateway Timeout

    // libcurl 错误（基数 1000）
    CurlErrorBase = 1000,         ///< libcurl 错误起始值（CURLE_* + 1000）

    // 应用层错误
    OperationCancelled = 42,      ///< 操作被用户取消
    InvalidRequest = 3,           ///< 无效的请求（URL 格式错误等）
    Unknown = 99                  ///< 未知错误
};

/**
 * @brief 获取错误描述字符串
 *
 * @param error 错误码
 * @return QString 错误描述（中文）
 */
[[nodiscard]] QString errorString(NetworkError error);

/**
 * @brief 从 libcurl 错误码转换为 NetworkError
 *
 * @param code libcurl 的 CURLcode
 * @return NetworkError 对应的网络错误枚举
 */
[[nodiscard]] NetworkError fromCurlCode(CURLcode code) noexcept;

/**
 * @brief 从 HTTP 状态码转换为 NetworkError
 *
 * @param httpCode HTTP 状态码（如 404、500）
 * @return NetworkError 对应的网络错误枚举
 */
[[nodiscard]] NetworkError fromHttpCode(long httpCode) noexcept;

/**
 * @brief 检查错误是否为 HTTP 错误
 *
 * @param error 错误码
 * @return bool true 表示是 HTTP 4xx/5xx 错误
 */
[[nodiscard]] bool isHttpError(NetworkError error) noexcept;

/**
 * @brief 检查错误是否为 libcurl 底层错误
 *
 * @param error 错误码
 * @return bool true 表示是 libcurl 的 CURLE_* 错误
 */
[[nodiscard]] bool isCurlError(NetworkError error) noexcept;

} // namespace QCurl
QT_END_NAMESPACE

#endif // QCNETWORKERROR_H
