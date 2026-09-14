#include "QCNetworkCache.h"

#include "QCNetworkTypes.h"
#include "private/QCHttpDate_p.h"

#include <QHash>
#include <QSharedData>
#include <QStringList>
#include <QTimeZone>

#include <algorithm>
#include <limits>
#include <utility>

namespace QCurl {

namespace {

constexpr QLatin1StringView kMaxAgeDirectivePrefix{"max-age="};

class RawHeaderIndex
{
public:
    explicit RawHeaderIndex(const QList<RawHeaderPair> &headers)
    {
        for (qsizetype i = 0; i < headers.size(); ++i) {
            const QByteArray name = headers.at(i).first.trimmed().toLower();
            if (!name.isEmpty()) {
                m_positions[name].append(i);
            }
        }
    }

    [[nodiscard]] QList<QByteArray> values(const QList<RawHeaderPair> &headers,
                                           QByteArrayView wantedName) const
    {
        QList<QByteArray> result;
        const auto positions = m_positions.value(wantedName.toByteArray().toLower());
        for (const qsizetype position : positions) {
            result.append(headers.at(position).second.trimmed());
        }
        return result;
    }

    [[nodiscard]] bool contains(QByteArrayView wantedName) const
    {
        return m_positions.contains(wantedName.toByteArray().toLower());
    }

private:
    QHash<QByteArray, QList<qsizetype>> m_positions;
};

[[nodiscard]] QByteArray joinedListField(const QList<RawHeaderPair> &headers,
                                         const RawHeaderIndex &index,
                                         QByteArrayView name)
{
    return index.values(headers, name).join(QByteArrayLiteral(","));
}

[[nodiscard]] QStringList cacheControlDirectives(const QList<RawHeaderPair> &headers,
                                                 const RawHeaderIndex &index)
{
    return QString::fromLatin1(joinedListField(headers, index, QByteArrayView("cache-control")))
        .split(QLatin1Char(','), Qt::SkipEmptyParts);
}

[[nodiscard]] bool containsDirective(const QStringList &directives, QLatin1StringView name)
{
    return std::any_of(directives.cbegin(), directives.cend(), [name](const QString &directive) {
        return directive.trimmed().compare(name, Qt::CaseInsensitive) == 0;
    });
}

[[nodiscard]] bool isSensitiveResponseHeader(QByteArrayView name)
{
    return name.compare(QByteArrayView("set-cookie"), Qt::CaseInsensitive) == 0
           || name.compare(QByteArrayView("set-cookie2"), Qt::CaseInsensitive) == 0
           || name.compare(QByteArrayView("authorization"), Qt::CaseInsensitive) == 0
           || name.compare(QByteArrayView("proxy-authorization"), Qt::CaseInsensitive) == 0;
}

[[nodiscard]] bool parseDeltaSeconds(const QByteArray &text, qint64 *result)
{
    const QByteArray value = text.trimmed();
    if (value.isEmpty()) {
        return false;
    }
    for (const char c : value) {
        if (c < '0' || c > '9') {
            return false;
        }
    }
    bool ok                 = false;
    const qulonglong parsed = value.toULongLong(&ok);
    if (!ok || parsed > static_cast<qulonglong>(std::numeric_limits<qint64>::max())) {
        return false;
    }
    *result = static_cast<qint64>(parsed);
    return true;
}

[[nodiscard]] qint64 saturatedAdd(qint64 left, qint64 right)
{
    if (right > 0 && left > std::numeric_limits<qint64>::max() - right) {
        return std::numeric_limits<qint64>::max();
    }
    if (right < 0 && left < std::numeric_limits<qint64>::min() - right) {
        return std::numeric_limits<qint64>::min();
    }
    return left + right;
}

[[nodiscard]] QDateTime saturatedDateAddSecs(const QDateTime &base, qint64 seconds)
{
    if (!base.isValid() || seconds <= 0) {
        return base;
    }
    const QDateTime maxDate(QDate(9999, 12, 31),
                            QTime(23, 59, 59),
                            QTimeZone(QByteArrayLiteral("UTC")));
    const qint64 available = base.secsTo(maxDate);
    if (available <= 0 || seconds >= available) {
        return maxDate;
    }
    return base.addSecs(seconds);
}

[[nodiscard]] bool hasValidSingleValueFields(const QList<RawHeaderPair> &headers,
                                             const RawHeaderIndex &index)
{
    const auto validateDate = [&](QByteArrayView name) {
        const auto values = index.values(headers, name);
        if (values.isEmpty()) {
            return true;
        }
        if (values.size() != 1) {
            return false;
        }
        return Internal::parseHttpDate(values.constFirst()).isValid();
    };
    const auto validateAge = [&]() {
        const auto values = index.values(headers, QByteArrayView("age"));
        if (values.isEmpty()) {
            return true;
        }
        if (values.size() != 1) {
            return false;
        }
        qint64 ignored = 0;
        return parseDeltaSeconds(values.constFirst(), &ignored);
    };
    return validateDate(QByteArrayView("date")) && validateDate(QByteArrayView("expires"))
           && validateAge();
}

[[nodiscard]] bool hasUnsafeResponseHeaders(const QList<RawHeaderPair> &headers)
{
    return std::any_of(headers.cbegin(), headers.cend(), [](const RawHeaderPair &header) {
        return isSensitiveResponseHeader(QByteArrayView(header.first));
    });
}

} // namespace

/// QCNetworkCacheMetadata 的共享存储，避免按值传递时复制 headers。
class QCNetworkCacheMetadataData : public QSharedData
{
public:
    QUrl url;
    QMap<QByteArray, QByteArray> headers;
    QList<RawHeaderPair> rawHeaders;
    QDateTime expirationDate;
    QDateTime lastModified;
    QDateTime creationDate;
    QDateTime requestTime;
    QDateTime responseTime;
    qint64 correctedInitialAgeSeconds = 0;
    qint64 size                       = 0;
    int statusCode                    = 200;
    QList<QByteArray> varyHeaderNames;
    bool rawHeadersCacheSafe = true;
};

QCNetworkCacheMetadata::QCNetworkCacheMetadata()
    : d(new QCNetworkCacheMetadataData)
{}

QCNetworkCacheMetadata::QCNetworkCacheMetadata(const QCNetworkCacheMetadata &other) = default;

QCNetworkCacheMetadata::QCNetworkCacheMetadata(QCNetworkCacheMetadata &&other) noexcept = default;

QCNetworkCacheMetadata::~QCNetworkCacheMetadata() = default;

QCNetworkCacheMetadata &QCNetworkCacheMetadata::operator=(
    const QCNetworkCacheMetadata &other) = default;

QCNetworkCacheMetadata &QCNetworkCacheMetadata::operator=(
    QCNetworkCacheMetadata &&other) noexcept = default;

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
    d->headers.clear();
    d->rawHeaders.clear();
    for (auto it = headers.cbegin(); it != headers.cend(); ++it) {
        setHeader(it.key(), it.value());
    }
    d->rawHeadersCacheSafe = QCNetworkCache::isCacheable(d->rawHeaders);
}

void QCNetworkCacheMetadata::setHeader(const QByteArray &name, const QByteArray &value)
{
    const QByteArray normalizedName = name.trimmed().toLower();
    if (normalizedName.isEmpty()) {
        return;
    }
    const QByteArray normalizedValue = value.trimmed();
    d->headers.insert(normalizedName, normalizedValue);
    d->rawHeaders.append(qMakePair(name.trimmed(), normalizedValue));
    d->rawHeadersCacheSafe = QCNetworkCache::isCacheable(d->rawHeaders);
}

QList<RawHeaderPair> QCNetworkCacheMetadata::rawHeaders() const
{
    return d->rawHeaders;
}

void QCNetworkCacheMetadata::setRawHeaders(const QList<RawHeaderPair> &headers)
{
    d->headers.clear();
    d->rawHeaders.clear();
    for (const auto &[name, value] : headers) {
        const QByteArray trimmedName = name.trimmed();
        if (trimmedName.isEmpty()) {
            continue;
        }
        const QByteArray trimmedValue = value.trimmed();
        d->rawHeaders.append(qMakePair(trimmedName, trimmedValue));
        d->headers.insert(trimmedName.toLower(), trimmedValue);
    }
    d->rawHeadersCacheSafe = QCNetworkCache::isCacheable(d->rawHeaders);
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

QDateTime QCNetworkCacheMetadata::requestTime() const
{
    return d->requestTime;
}

void QCNetworkCacheMetadata::setRequestTime(const QDateTime &requestTime)
{
    d->requestTime = requestTime;
}

QDateTime QCNetworkCacheMetadata::responseTime() const
{
    return d->responseTime;
}

void QCNetworkCacheMetadata::setResponseTime(const QDateTime &responseTime)
{
    d->responseTime = responseTime;
}

qint64 QCNetworkCacheMetadata::correctedInitialAgeSeconds() const noexcept
{
    return d->correctedInitialAgeSeconds;
}

void QCNetworkCacheMetadata::setCorrectedInitialAgeSeconds(qint64 ageSeconds) noexcept
{
    d->correctedInitialAgeSeconds = qMax<qint64>(0, ageSeconds);
}

qint64 QCNetworkCacheMetadata::currentAgeSeconds() const noexcept
{
    const qint64 residentTime = d->responseTime.isValid()
                                    ? qMax<qint64>(0,
                                                   d->responseTime.secsTo(
                                                       QDateTime::currentDateTimeUtc()))
                                    : 0;
    return saturatedAdd(d->correctedInitialAgeSeconds, residentTime);
}

qint64 QCNetworkCacheMetadata::size() const
{
    return d->size;
}

void QCNetworkCacheMetadata::setSize(qint64 size)
{
    d->size = size;
}

int QCNetworkCacheMetadata::statusCode() const noexcept
{
    return d->statusCode;
}

void QCNetworkCacheMetadata::setStatusCode(int statusCode)
{
    d->statusCode = statusCode;
}

QList<QByteArray> QCNetworkCacheMetadata::varyHeaderNames() const
{
    return d->varyHeaderNames;
}

void QCNetworkCacheMetadata::setVaryHeaderNames(const QList<QByteArray> &headerNames)
{
    d->varyHeaderNames.clear();
    for (const QByteArray &name : headerNames) {
        const QByteArray normalized = name.trimmed().toLower();
        if (!normalized.isEmpty() && !d->varyHeaderNames.contains(normalized)) {
            d->varyHeaderNames.append(normalized);
        }
    }
    std::sort(d->varyHeaderNames.begin(), d->varyHeaderNames.end());
}

bool QCNetworkCacheMetadata::isValid() const
{
    return d->rawHeadersCacheSafe
           && (d->expirationDate.isNull()
               || QDateTime::currentDateTimeUtc() < d->expirationDate.toUTC());
}

QDateTime QCNetworkCache::parseExpirationDate(const QList<RawHeaderPair> &headers)
{
    const QDateTime now = QDateTime::currentDateTimeUtc();
    const RawHeaderIndex index(headers);
    const QStringList directives = cacheControlDirectives(headers, index);
    if (containsDirective(directives, QLatin1StringView("no-cache"))) {
        return now;
    }
    bool maxAgeSeen = false;
    qint64 maxAge   = 0;
    for (const QString &directive : directives) {
        const QString trimmed = directive.trimmed();
        if (trimmed.startsWith(kMaxAgeDirectivePrefix, Qt::CaseInsensitive)) {
            qint64 value = 0;
            if (!parseDeltaSeconds(trimmed.mid(kMaxAgeDirectivePrefix.size()).toLatin1(), &value)
                || (maxAgeSeen && value != maxAge)) {
                return now;
            }
            maxAgeSeen = true;
            maxAge     = value;
        }
    }
    if (maxAgeSeen) {
        return saturatedDateAddSecs(now, maxAge);
    }

    const auto expiresValues = index.values(headers, QByteArrayView("expires"));
    if (expiresValues.size() == 1) {
        const QDateTime expiresDate = Internal::parseHttpDate(expiresValues.constFirst());
        if (expiresDate.isValid()) {
            return expiresDate.toUTC();
        }
    } else if (!expiresValues.isEmpty()) {
        return now;
    }

    if (joinedListField(headers, index, QByteArrayView("pragma")).toLower().contains("no-cache")) {
        return now;
    }

    // 未提供显式新鲜度时保守地立即进入 stale 状态，等待 validator 重验证。
    return now;
}

bool QCNetworkCache::isCacheable(const QList<RawHeaderPair> &headers)
{
    const RawHeaderIndex index(headers);
    if (hasUnsafeResponseHeaders(headers) || !hasValidSingleValueFields(headers, index)) {
        return false;
    }

    bool maxAgeSeen              = false;
    qint64 maxAge                = 0;
    const QStringList directives = cacheControlDirectives(headers, index);
    for (const QString &directive : directives) {
        const QString trimmed = directive.trimmed();
        if (trimmed.compare(QLatin1StringView("no-store"), Qt::CaseInsensitive) == 0) {
            return false;
        }
        if (!trimmed.startsWith(kMaxAgeDirectivePrefix, Qt::CaseInsensitive)) {
            continue;
        }
        qint64 value = 0;
        if (!parseDeltaSeconds(trimmed.mid(kMaxAgeDirectivePrefix.size()).toLatin1(), &value)
            || (maxAgeSeen && value != maxAge)) {
            return false;
        }
        maxAgeSeen = true;
        maxAge     = value;
    }

    const QList<QByteArray> varyNames = varyHeaderNames(headers);
    return !varyNames.contains(QByteArrayLiteral("*"));
}

QList<QByteArray> QCNetworkCache::varyHeaderNames(const QList<RawHeaderPair> &headers)
{
    QList<QByteArray> names;
    const RawHeaderIndex index(headers);
    const QByteArray vary = joinedListField(headers, index, QByteArrayView("vary"));
    for (const QByteArray &part : vary.split(',')) {
        const QByteArray normalized = part.trimmed().toLower();
        if (!normalized.isEmpty() && !names.contains(normalized)) {
            names.append(normalized);
        }
    }
    std::sort(names.begin(), names.end());
    return names;
}

} // namespace QCurl
