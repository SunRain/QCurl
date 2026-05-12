#include "QCNetworkCache.h"

#include <QSharedData>
#include <QStringList>

#include <utility>

namespace QCurl {

/// QCNetworkCacheMetadata 的共享存储，避免按值传递时复制 headers。
class QCNetworkCacheMetadataData : public QSharedData
{
public:
    QUrl url;
    QMap<QByteArray, QByteArray> headers;
    QDateTime expirationDate;
    QDateTime lastModified;
    QDateTime creationDate;
    qint64 size = 0;
};

/// 缓存查询结果的共享存储，status 负责区分未命中和空 body 命中。
class QCNetworkCacheLookupResultData : public QSharedData
{
public:
    QCNetworkCacheLookupStatus status = QCNetworkCacheLookupStatus::Miss;
    QCNetworkCacheMetadata metadata;
    QByteArray body;
};

QCNetworkCacheMetadata::QCNetworkCacheMetadata()
    : d(new QCNetworkCacheMetadataData)
{}

QCNetworkCacheMetadata::QCNetworkCacheMetadata(const QCNetworkCacheMetadata &other) = default;

QCNetworkCacheMetadata::QCNetworkCacheMetadata(QCNetworkCacheMetadata &&other) noexcept = default;

QCNetworkCacheMetadata::~QCNetworkCacheMetadata() = default;

QCNetworkCacheMetadata &QCNetworkCacheMetadata::operator=(const QCNetworkCacheMetadata &other) = default;

QCNetworkCacheMetadata &QCNetworkCacheMetadata::operator=(QCNetworkCacheMetadata &&other) noexcept = default;

QUrl QCNetworkCacheMetadata::url() const
{
    return d->url;
}

void QCNetworkCacheMetadata::setUrl(const QUrl &url)
{
    d->url = url;
}

QMap<QByteArray, QByteArray> QCNetworkCacheMetadata::headers() const
{
    return d->headers;
}

void QCNetworkCacheMetadata::setHeaders(const QMap<QByteArray, QByteArray> &headers)
{
    d->headers = headers;
}

void QCNetworkCacheMetadata::setHeader(const QByteArray &name, const QByteArray &value)
{
    d->headers.insert(name, value);
}

QDateTime QCNetworkCacheMetadata::expirationDate() const
{
    return d->expirationDate;
}

void QCNetworkCacheMetadata::setExpirationDate(const QDateTime &expirationDate)
{
    d->expirationDate = expirationDate;
}

QDateTime QCNetworkCacheMetadata::lastModified() const
{
    return d->lastModified;
}

void QCNetworkCacheMetadata::setLastModified(const QDateTime &lastModified)
{
    d->lastModified = lastModified;
}

QDateTime QCNetworkCacheMetadata::creationDate() const
{
    return d->creationDate;
}

void QCNetworkCacheMetadata::setCreationDate(const QDateTime &creationDate)
{
    d->creationDate = creationDate;
}

qint64 QCNetworkCacheMetadata::size() const
{
    return d->size;
}

void QCNetworkCacheMetadata::setSize(qint64 size)
{
    d->size = size;
}

bool QCNetworkCacheMetadata::isValid() const
{
    return d->expirationDate.isNull() || QDateTime::currentDateTime() < d->expirationDate;
}

QCNetworkCacheLookupResult::QCNetworkCacheLookupResult()
    : d(new QCNetworkCacheLookupResultData)
{}

QCNetworkCacheLookupResult::QCNetworkCacheLookupResult(
    const QCNetworkCacheLookupResult &other) = default;

QCNetworkCacheLookupResult::QCNetworkCacheLookupResult(
    QCNetworkCacheLookupResult &&other) noexcept = default;

QCNetworkCacheLookupResult::~QCNetworkCacheLookupResult() = default;

QCNetworkCacheLookupResult &QCNetworkCacheLookupResult::operator=(
    const QCNetworkCacheLookupResult &other) = default;

QCNetworkCacheLookupResult &QCNetworkCacheLookupResult::operator=(
    QCNetworkCacheLookupResult &&other) noexcept = default;

QCNetworkCacheLookupStatus QCNetworkCacheLookupResult::status() const
{
    return d->status;
}

void QCNetworkCacheLookupResult::setStatus(QCNetworkCacheLookupStatus status)
{
    d->status = status;
}

QCNetworkCacheMetadata QCNetworkCacheLookupResult::metadata() const
{
    return d->metadata;
}

void QCNetworkCacheLookupResult::setMetadata(const QCNetworkCacheMetadata &metadata)
{
    d->metadata = metadata;
}

QByteArray QCNetworkCacheLookupResult::body() const
{
    return d->body;
}

void QCNetworkCacheLookupResult::setBody(const QByteArray &body)
{
    d->body = body;
}

bool QCNetworkCacheLookupResult::hit() const
{
    return d->status != QCNetworkCacheLookupStatus::Miss;
}

QDateTime QCNetworkCache::parseExpirationDate(const QMap<QByteArray, QByteArray> &headers)
{
    // 1. 检查 Cache-Control: max-age
    if (headers.contains("cache-control") || headers.contains("Cache-Control")) {
        QByteArray cacheControl = headers.value("cache-control");
        if (cacheControl.isEmpty()) {
            cacheControl = headers.value("Cache-Control");
        }

        // 解析 max-age=秒数
        QString ccStr          = QString::fromLatin1(cacheControl);
        QStringList directives = ccStr.split(QLatin1Char(','), Qt::SkipEmptyParts);
        for (const QString &directive : directives) {
            QString trimmed = directive.trimmed();
            if (trimmed.startsWith(QStringLiteral("max-age="), Qt::CaseInsensitive)) {
                bool ok    = false;
                int maxAge = trimmed.mid(8).toInt(&ok);
                if (ok && maxAge > 0) {
                    return QDateTime::currentDateTime().addSecs(maxAge);
                }
            }
        }
    }

    // 2. 检查 Expires 头
    if (headers.contains("expires") || headers.contains("Expires")) {
        QByteArray expires = headers.value("expires");
        if (expires.isEmpty()) {
            expires = headers.value("Expires");
        }

        // 解析 HTTP 日期格式（RFC 2616）
        QDateTime expiresDate = QDateTime::fromString(QString::fromLatin1(expires), Qt::RFC2822Date);
        if (expiresDate.isValid()) {
            return expiresDate;
        }
    }

    // 3. 默认：无过期时间（永久缓存）
    return QDateTime();
}

bool QCNetworkCache::isCacheable(const QMap<QByteArray, QByteArray> &headers)
{
    // 检查 Cache-Control: no-store 或 no-cache
    if (headers.contains("cache-control") || headers.contains("Cache-Control")) {
        QByteArray cacheControl = headers.value("cache-control");
        if (cacheControl.isEmpty()) {
            cacheControl = headers.value("Cache-Control");
        }

        QString ccStr = QString::fromLatin1(cacheControl).toLower();
        if (ccStr.contains(QStringLiteral("no-store"))
            || ccStr.contains(QStringLiteral("no-cache"))) {
            return false;
        }
    }

    // 检查 Pragma: no-cache（HTTP/1.0 兼容）
    if (headers.contains("pragma") || headers.contains("Pragma")) {
        QByteArray pragma = headers.value("pragma");
        if (pragma.isEmpty()) {
            pragma = headers.value("Pragma");
        }

        if (QString::fromLatin1(pragma).toLower().contains(QStringLiteral("no-cache"))) {
            return false;
        }
    }

    return true;
}

} // namespace QCurl
