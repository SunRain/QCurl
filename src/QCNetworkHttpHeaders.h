/**
 * @file
 * @brief 为 raw header API 提供常用 HTTP 头名，不限制自定义字段。
 */

#ifndef QCNETWORKHTTPHEADERS_H
#define QCNETWORKHTTPHEADERS_H

#include "QCGlobal.h"

#include <QByteArray>

/// 常用 HTTP 头的规范显示名称；内部小写索引须显式归一化。
namespace QCurl::httpheaders {

inline const QByteArray kAccept             = QByteArrayLiteral("Accept");
inline const QByteArray kAcceptEncoding     = QByteArrayLiteral("Accept-Encoding");
inline const QByteArray kAuthorization      = QByteArrayLiteral("Authorization");
inline const QByteArray kCacheControl       = QByteArrayLiteral("Cache-Control");
inline const QByteArray kContentEncoding    = QByteArrayLiteral("Content-Encoding");
inline const QByteArray kContentLength      = QByteArrayLiteral("Content-Length");
inline const QByteArray kContentRange       = QByteArrayLiteral("Content-Range");
inline const QByteArray kContentType        = QByteArrayLiteral("Content-Type");
inline const QByteArray kCookie             = QByteArrayLiteral("Cookie");
inline const QByteArray kETag               = QByteArrayLiteral("ETag");
inline const QByteArray kIdempotencyKey     = QByteArrayLiteral("Idempotency-Key");
inline const QByteArray kIfModifiedSince    = QByteArrayLiteral("If-Modified-Since");
inline const QByteArray kIfNoneMatch        = QByteArrayLiteral("If-None-Match");
inline const QByteArray kLastModified       = QByteArrayLiteral("Last-Modified");
inline const QByteArray kPragma             = QByteArrayLiteral("Pragma");
inline const QByteArray kProxyAuthorization = QByteArrayLiteral("Proxy-Authorization");
inline const QByteArray kRange              = QByteArrayLiteral("Range");
inline const QByteArray kReferer            = QByteArrayLiteral("Referer");
inline const QByteArray kRetryAfter         = QByteArrayLiteral("Retry-After");
inline const QByteArray kSetCookie          = QByteArrayLiteral("Set-Cookie");
inline const QByteArray kUserAgent          = QByteArrayLiteral("User-Agent");

} // namespace QCurl::httpheaders

#endif // QCNETWORKHTTPHEADERS_H
