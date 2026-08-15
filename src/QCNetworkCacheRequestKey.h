/**
 * @file
 * @brief 声明结构化 HTTP 缓存请求键。
 */

#ifndef QCNETWORKCACHEREQUESTKEY_H
#define QCNETWORKCACHEREQUESTKEY_H

#include "QCGlobal.h"
#include "QCNetworkHttpMethod.h"

#include <QByteArray>
#include <QMap>
#include <QSharedDataPointer>
#include <QUrl>

namespace QCurl {

class QCNetworkCacheRequestKeyData;

/**
 * @brief 描述缓存查询所需的请求身份与 Vary 上下文。
 *
 * URL 会移除 fragment、userinfo 和默认端口；请求头名称按小写保存。
 * `cachePartitionKey` 是调用方提供的 opaque 隔离标识，缓存实现不得持久化其原值。
 */
class QCURL_EXPORT QCNetworkCacheRequestKey
{
public:
    QCNetworkCacheRequestKey();
    QCNetworkCacheRequestKey(HttpMethod method, const QUrl &url);
    QCNetworkCacheRequestKey(const QCNetworkCacheRequestKey &other);
    QCNetworkCacheRequestKey(QCNetworkCacheRequestKey &&other) noexcept;
    ~QCNetworkCacheRequestKey();

    QCNetworkCacheRequestKey &operator=(const QCNetworkCacheRequestKey &other);
    QCNetworkCacheRequestKey &operator=(QCNetworkCacheRequestKey &&other) noexcept;

    [[nodiscard]] HttpMethod method() const noexcept;
    void setMethod(HttpMethod method);

    [[nodiscard]] QUrl normalizedUrl() const;
    void setUrl(const QUrl &url);

    [[nodiscard]] QMap<QByteArray, QByteArray> requestHeaders() const;
    void setRequestHeaders(const QMap<QByteArray, QByteArray> &headers);
    void setRequestHeader(const QByteArray &name, const QByteArray &value);
    [[nodiscard]] QByteArray requestHeader(const QByteArray &name) const;

    /// 标记无法可靠取得实际出站值的请求头；涉及该维度的 Vary 条目必须 fail closed。
    void markRequestHeaderUnavailable(const QByteArray &name);
    [[nodiscard]] bool isRequestHeaderAvailable(const QByteArray &name) const;

    [[nodiscard]] QByteArray cachePartitionKey() const;
    void setCachePartitionKey(const QByteArray &partitionKey);

    [[nodiscard]] bool hasAuthenticationContext() const noexcept;
    void setAuthenticationContext(bool authenticated) noexcept;

private:
    QSharedDataPointer<QCNetworkCacheRequestKeyData> d;
};

} // namespace QCurl

#endif // QCNETWORKCACHEREQUESTKEY_H
