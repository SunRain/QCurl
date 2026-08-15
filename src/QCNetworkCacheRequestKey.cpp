#include "QCNetworkCacheRequestKey.h"

#include <QSet>
#include <QSharedData>

namespace QCurl {
namespace {

[[nodiscard]] QUrl normalizedCacheUrl(const QUrl &source)
{
    QUrl url = source.adjusted(QUrl::NormalizePathSegments | QUrl::RemoveFragment);
    url.setScheme(url.scheme().toLower());
    url.setHost(url.host().toLower());
    url.setUserInfo(QString());

    const int port = url.port();
    if ((url.scheme() == QLatin1StringView("http") && port == 80)
        || (url.scheme() == QLatin1StringView("https") && port == 443)) {
        url.setPort(-1);
    }
    return url;
}

[[nodiscard]] QByteArray normalizedHeaderName(const QByteArray &name)
{
    return name.trimmed().toLower();
}

} // namespace

/**
 * @brief 保存结构化缓存请求键的隐式共享数据。
 */
class QCNetworkCacheRequestKeyData : public QSharedData
{
public:
    HttpMethod method = HttpMethod::Get;
    QUrl normalizedUrl;
    QMap<QByteArray, QByteArray> requestHeaders;
    QSet<QByteArray> unavailableRequestHeaders;
    QByteArray cachePartitionKey;
    bool authenticationContext = false;
};

QCNetworkCacheRequestKey::QCNetworkCacheRequestKey()
    : d(new QCNetworkCacheRequestKeyData)
{}

QCNetworkCacheRequestKey::QCNetworkCacheRequestKey(HttpMethod method, const QUrl &url)
    : d(new QCNetworkCacheRequestKeyData)
{
    d->method        = method;
    d->normalizedUrl = normalizedCacheUrl(url);
}

QCNetworkCacheRequestKey::QCNetworkCacheRequestKey(const QCNetworkCacheRequestKey &other) = default;

QCNetworkCacheRequestKey::QCNetworkCacheRequestKey(
    QCNetworkCacheRequestKey &&other) noexcept = default;

QCNetworkCacheRequestKey::~QCNetworkCacheRequestKey() = default;

QCNetworkCacheRequestKey &QCNetworkCacheRequestKey::operator=(
    const QCNetworkCacheRequestKey &other) = default;

QCNetworkCacheRequestKey &QCNetworkCacheRequestKey::operator=(
    QCNetworkCacheRequestKey &&other) noexcept = default;

HttpMethod QCNetworkCacheRequestKey::method() const noexcept
{
    return d->method;
}

void QCNetworkCacheRequestKey::setMethod(HttpMethod method)
{
    d->method = method;
}

QUrl QCNetworkCacheRequestKey::normalizedUrl() const
{
    return d->normalizedUrl;
}

void QCNetworkCacheRequestKey::setUrl(const QUrl &url)
{
    d->normalizedUrl = normalizedCacheUrl(url);
}

QMap<QByteArray, QByteArray> QCNetworkCacheRequestKey::requestHeaders() const
{
    return d->requestHeaders;
}

void QCNetworkCacheRequestKey::setRequestHeaders(const QMap<QByteArray, QByteArray> &headers)
{
    d->requestHeaders.clear();
    for (auto it = headers.cbegin(); it != headers.cend(); ++it) {
        setRequestHeader(it.key(), it.value());
    }
}

void QCNetworkCacheRequestKey::setRequestHeader(const QByteArray &name, const QByteArray &value)
{
    const QByteArray normalizedName = normalizedHeaderName(name);
    if (normalizedName.isEmpty()) {
        return;
    }
    d->unavailableRequestHeaders.remove(normalizedName);
    d->requestHeaders.insert(normalizedName, value.trimmed());
}

QByteArray QCNetworkCacheRequestKey::requestHeader(const QByteArray &name) const
{
    return d->requestHeaders.value(normalizedHeaderName(name));
}

void QCNetworkCacheRequestKey::markRequestHeaderUnavailable(const QByteArray &name)
{
    const QByteArray normalizedName = normalizedHeaderName(name);
    if (normalizedName.isEmpty()) {
        return;
    }
    d->requestHeaders.remove(normalizedName);
    d->unavailableRequestHeaders.insert(normalizedName);
}

bool QCNetworkCacheRequestKey::isRequestHeaderAvailable(const QByteArray &name) const
{
    return !d->unavailableRequestHeaders.contains(normalizedHeaderName(name));
}

QByteArray QCNetworkCacheRequestKey::cachePartitionKey() const
{
    return d->cachePartitionKey;
}

void QCNetworkCacheRequestKey::setCachePartitionKey(const QByteArray &partitionKey)
{
    d->cachePartitionKey = partitionKey;
}

bool QCNetworkCacheRequestKey::hasAuthenticationContext() const noexcept
{
    return d->authenticationContext;
}

void QCNetworkCacheRequestKey::setAuthenticationContext(bool authenticated) noexcept
{
    d->authenticationContext = authenticated;
}

} // namespace QCurl
