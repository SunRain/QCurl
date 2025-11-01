#include "QCNetworkError.h"

QT_BEGIN_NAMESPACE

namespace QCurl {

QString errorString(NetworkError error)
{
    int errorCode = static_cast<int>(error);

    // HTTP 错误
    if (errorCode >= 400 && errorCode < 600) {
        switch (error) {
        case NetworkError::HttpBadRequest:
            return QStringLiteral("HTTP 400: 请求错误");
        case NetworkError::HttpUnauthorized:
            return QStringLiteral("HTTP 401: 未授权");
        case NetworkError::HttpForbidden:
            return QStringLiteral("HTTP 403: 禁止访问");
        case NetworkError::HttpNotFound:
            return QStringLiteral("HTTP 404: 资源未找到");
        case NetworkError::HttpMethodNotAllowed:
            return QStringLiteral("HTTP 405: 方法不允许");
        case NetworkError::HttpTimeout:
            return QStringLiteral("HTTP 408: 请求超时");
        case NetworkError::HttpInternalServerError:
            return QStringLiteral("HTTP 500: 服务器内部错误");
        case NetworkError::HttpBadGateway:
            return QStringLiteral("HTTP 502: 网关错误");
        case NetworkError::HttpServiceUnavailable:
            return QStringLiteral("HTTP 503: 服务不可用");
        case NetworkError::HttpGatewayTimeout:
            return QStringLiteral("HTTP 504: 网关超时");
        default:
            return QStringLiteral("HTTP %1 错误").arg(errorCode);
        }
    }

    // 其他错误
    switch (error) {
    case NetworkError::NoError:
        return QStringLiteral("无错误");
    case NetworkError::ConnectionRefused:
        return QStringLiteral("连接被拒绝");
    case NetworkError::ConnectionTimeout:
        return QStringLiteral("连接超时");
    case NetworkError::HostNotFound:
        return QStringLiteral("主机名解析失败");
    case NetworkError::SslHandshakeFailed:
        return QStringLiteral("SSL 握手失败");
    case NetworkError::TooManyRedirects:
        return QStringLiteral("重定向次数过多");
    case NetworkError::OperationCancelled:
        return QStringLiteral("操作已取消");
    case NetworkError::InvalidRequest:
        return QStringLiteral("无效的请求");
    case NetworkError::Unknown:
        return QStringLiteral("未知错误");
    default:
        // libcurl 错误
        if (errorCode >= static_cast<int>(NetworkError::CurlErrorBase)) {
            CURLcode curlCode = static_cast<CURLcode>(errorCode - static_cast<int>(NetworkError::CurlErrorBase));
            return QString::fromUtf8(curl_easy_strerror(curlCode));
        }
        return QStringLiteral("未知错误 (%1)").arg(errorCode);
    }
}

NetworkError fromCurlCode(CURLcode code) noexcept
{
    if (code == CURLE_OK) {
        return NetworkError::NoError;
    }

    // 常见错误映射
    switch (code) {
    case CURLE_COULDNT_RESOLVE_HOST:
        return NetworkError::HostNotFound;
    case CURLE_COULDNT_CONNECT:
        return NetworkError::ConnectionRefused;
    case CURLE_OPERATION_TIMEDOUT:
        return NetworkError::ConnectionTimeout;
    case CURLE_SSL_CONNECT_ERROR:
    case CURLE_PEER_FAILED_VERIFICATION:
        return NetworkError::SslHandshakeFailed;
    case CURLE_TOO_MANY_REDIRECTS:
        return NetworkError::TooManyRedirects;
    case CURLE_ABORTED_BY_CALLBACK:
        return NetworkError::OperationCancelled;
    case CURLE_URL_MALFORMAT:
        return NetworkError::InvalidRequest;
    default:
        // 其他 libcurl 错误映射到 CurlErrorBase + code
        return static_cast<NetworkError>(static_cast<int>(NetworkError::CurlErrorBase) + code);
    }
}

NetworkError fromHttpCode(long httpCode) noexcept
{
    if (httpCode >= 400 && httpCode < 600) {
        return static_cast<NetworkError>(httpCode);
    }
    return NetworkError::NoError;
}

bool isHttpError(NetworkError error) noexcept
{
    int errorCode = static_cast<int>(error);
    return errorCode >= 400 && errorCode < 600;
}

bool isCurlError(NetworkError error) noexcept
{
    int errorCode = static_cast<int>(error);

    // 检查是否在 CurlErrorBase 范围内（>= 1000）
    if (errorCode >= static_cast<int>(NetworkError::CurlErrorBase)) {
        return true;
    }

    // 检查是否是映射的 curl 错误（范围在 0-99，但不是 NoError）
    if (errorCode == 0) {
        return false;  // NoError
    }

    // 以下是从 fromCurlCode() 映射的错误码
    switch (error) {
    case NetworkError::HostNotFound:           // 6
    case NetworkError::ConnectionRefused:      // 7
    case NetworkError::ConnectionTimeout:      // 28
    case NetworkError::OperationCancelled:     // 42
    case NetworkError::TooManyRedirects:       // 47
    case NetworkError::SslHandshakeFailed:     // 60
    case NetworkError::InvalidRequest:         // 3
        return true;
    default:
        return false;
    }
}

} // namespace QCurl
QT_END_NAMESPACE
