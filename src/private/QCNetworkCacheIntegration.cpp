#include "QCNetworkSslConfig.h"
#include "private/QCHttpDate_p.h"
#include "private/QCNetworkCacheIntegration_p.h"

#include <QHash>
#include <QSet>
#include <QTimeZone>

#include <limits>
#include <optional>

namespace QCurl::Internal {
namespace {

[[nodiscard]] bool requestCacheControlContains(const QCNetworkRequest &request,
                                               QByteArrayView wantedDirective)
{
    const QByteArray cacheControl = request.rawHeader(QByteArrayLiteral("Cache-Control"));
    for (const QByteArray &part : cacheControl.split(',')) {
        if (QByteArrayView(part.trimmed()).compare(wantedDirective, Qt::CaseInsensitive) == 0) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] bool requestHasHeader(const QCNetworkRequest &request, QByteArrayView wantedName)
{
    for (const QByteArray &name : request.rawHeaderList()) {
        if (QByteArrayView(name).compare(wantedName, Qt::CaseInsensitive) == 0
            && !request.rawHeader(name).isEmpty()) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] QByteArray metadataHeader(const QCNetworkCacheMetadata &metadata,
                                        QByteArrayView wantedName)
{
    QByteArray value;
    bool found = false;
    for (const auto &[name, candidate] : metadata.rawHeaders()) {
        if (QByteArrayView(name).compare(wantedName, Qt::CaseInsensitive) != 0) {
            continue;
        }
        if (found) {
            return {};
        }
        value = candidate.trimmed();
        found = true;
    }
    return value;
}

using RawHeaderPair = QCNetworkCacheMetadata::RawHeaderPair;

[[nodiscard]] bool isCacheSensitiveHeader(QByteArrayView name)
{
    return name.compare(QByteArrayView("set-cookie"), Qt::CaseInsensitive) == 0
           || name.compare(QByteArrayView("set-cookie2"), Qt::CaseInsensitive) == 0
           || name.compare(QByteArrayView("authorization"), Qt::CaseInsensitive) == 0
           || name.compare(QByteArrayView("proxy-authorization"), Qt::CaseInsensitive) == 0;
}

class ResponseHeaderIndex
{
public:
    explicit ResponseHeaderIndex(const QList<RawHeaderPair> &headers)
        : m_headers(headers)
    {
        for (qsizetype i = 0; i < headers.size(); ++i) {
            const QByteArray name = headers.at(i).first.trimmed().toLower();
            if (!name.isEmpty()) {
                m_positions[name].append(i);
            }
        }
    }

    [[nodiscard]] QList<QByteArray> values(QByteArrayView wantedName) const
    {
        QList<QByteArray> result;
        const auto positions = m_positions.value(wantedName.toByteArray().toLower());
        for (const qsizetype position : positions) {
            result.append(m_headers.at(position).second.trimmed());
        }
        return result;
    }

    [[nodiscard]] QByteArray joined(QByteArrayView wantedName) const
    {
        return values(wantedName).join(QByteArrayLiteral(","));
    }

private:
    const QList<RawHeaderPair> &m_headers;
    QHash<QByteArray, QList<qsizetype>> m_positions;
};

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
    return left + right;
}

[[nodiscard]] QDateTime saturatedDateAddSecs(const QDateTime &base, qint64 seconds)
{
    if (!base.isValid() || seconds <= 0) {
        return base;
    }
    const QDateTime maxDate(QDate(9999, 12, 31), QTime(23, 59, 59), QTimeZone::UTC);
    const qint64 available = base.secsTo(maxDate);
    return available <= 0 || seconds >= available ? maxDate : base.addSecs(seconds);
}

[[nodiscard]] QStringList responseCacheControlDirectives(const ResponseHeaderIndex &headers)
{
    return QString::fromLatin1(headers.joined(QByteArrayView("cache-control")))
        .split(QLatin1Char(','), Qt::SkipEmptyParts);
}

[[nodiscard]] std::optional<qint64> responseMaxAge(const ResponseHeaderIndex &headers)
{
    constexpr QLatin1StringView prefix{"max-age="};
    for (const QString &directive : responseCacheControlDirectives(headers)) {
        const QString trimmed = directive.trimmed();
        if (!trimmed.startsWith(prefix, Qt::CaseInsensitive)) {
            continue;
        }
        qint64 value = 0;
        return parseDeltaSeconds(trimmed.mid(prefix.size()).toLatin1(), &value)
                   ? std::optional<qint64>(value)
                   : std::nullopt;
    }
    return std::nullopt;
}

[[nodiscard]] bool responseRequiresRevalidation(const ResponseHeaderIndex &headers)
{
    for (const QString &directive : responseCacheControlDirectives(headers)) {
        if (directive.trimmed().compare(QLatin1StringView("no-cache"), Qt::CaseInsensitive) == 0) {
            return true;
        }
    }
    return headers.joined(QByteArrayView("pragma")).toLower().contains(QByteArrayLiteral("no-cache"));
}

[[nodiscard]] qint64 correctedInitialAgeSeconds(const ResponseHeaderIndex &headers,
                                                const QDateTime &requestTime,
                                                const QDateTime &responseTime)
{
    const auto dateValues = headers.values(QByteArrayView("date"));
    const auto ageValues  = headers.values(QByteArrayView("age"));
    if (dateValues.size() > 1 || ageValues.size() > 1) {
        return std::numeric_limits<qint64>::max();
    }
    const QDateTime date = dateValues.isEmpty() ? QDateTime()
                                                : parseHttpDate(dateValues.constFirst());
    if (!dateValues.isEmpty() && !date.isValid()) {
        return std::numeric_limits<qint64>::max();
    }
    const qint64 apparentAge = date.isValid() ? qMax<qint64>(0, date.secsTo(responseTime)) : 0;

    qint64 ageValue = 0;
    if (!ageValues.isEmpty() && !parseDeltaSeconds(ageValues.constFirst(), &ageValue)) {
        return std::numeric_limits<qint64>::max();
    }
    const qint64 responseDelay     = requestTime.isValid()
                                         ? qMax<qint64>(0, requestTime.secsTo(responseTime))
                                         : 0;
    const qint64 correctedAgeValue = saturatedAdd(ageValue, responseDelay);
    return qMax(apparentAge, correctedAgeValue);
}

[[nodiscard]] QDateTime cacheExpirationDate(const ResponseHeaderIndex &headers,
                                            const QDateTime &responseTime,
                                            qint64 correctedInitialAge)
{
    if (responseRequiresRevalidation(headers)) {
        return responseTime;
    }
    if (const auto maxAge = responseMaxAge(headers); maxAge.has_value()) {
        return correctedInitialAge >= maxAge.value()
                   ? responseTime
                   : saturatedDateAddSecs(responseTime, maxAge.value() - correctedInitialAge);
    }

    const auto expiresValues = headers.values(QByteArrayView("expires"));
    const auto dateValues    = headers.values(QByteArrayView("date"));
    if (expiresValues.size() != 1 || dateValues.size() > 1) {
        return responseTime;
    }
    const QDateTime expires = parseHttpDate(expiresValues.constFirst());
    if (!expires.isValid()) {
        return responseTime;
    }
    const QDateTime date  = dateValues.isEmpty() ? QDateTime()
                                                 : parseHttpDate(dateValues.constFirst());
    const qint64 lifetime = date.isValid() ? qMax<qint64>(0, date.secsTo(expires))
                                           : qMax<qint64>(0, responseTime.secsTo(expires));
    return correctedInitialAge >= lifetime
               ? responseTime
               : saturatedDateAddSecs(responseTime, lifetime - correctedInitialAge);
}

[[nodiscard]] QByteArray configuredAcceptEncoding(const QCNetworkRequest &request)
{
    if (!request.autoDecompressionEnabled()) {
        return {};
    }

    QStringList encodings;
    for (const QString &encoding : request.acceptedEncodings()) {
        const QString trimmed = encoding.trimmed();
        if (!trimmed.isEmpty()) {
            encodings.append(trimmed);
        }
    }
    return encodings.join(QLatin1Char(',')).toUtf8();
}

} // namespace

bool requestRequiresCacheRevalidation(const QCNetworkRequest &request)
{
    if (requestCacheControlContains(request, QByteArrayView("no-cache"))
        || requestCacheControlContains(request, QByteArrayView("no-store"))
        || requestCacheControlContains(request, QByteArrayView("max-age=0"))) {
        return true;
    }
    return request.rawHeader(QByteArrayLiteral("Pragma"))
        .toLower()
        .contains(QByteArrayLiteral("no-cache"));
}

bool requestForbidsCacheStorage(const QCNetworkRequest &request)
{
    return requestCacheControlContains(request, QByteArrayView("no-store"));
}

bool requestHasAuthenticationContext(const QCNetworkRequest &request, bool managerUsesCookies)
{
    const QCNetworkSslConfig ssl = request.sslConfig();
    return managerUsesCookies || request.httpAuth().has_value()
           || !request.url().userInfo().isEmpty()
           || requestHasHeader(request, QByteArrayView("authorization"))
           || requestHasHeader(request, QByteArrayView("cookie")) || !ssl.clientCertPath().isEmpty()
           || !ssl.clientKeyPath().isEmpty();
}

QCNetworkCacheRequestKey buildCacheRequestKey(const QCNetworkRequest &request,
                                              HttpMethod method,
                                              bool managerUsesCookies)
{
    const HttpMethod storageMethod = method == HttpMethod::Head ? HttpMethod::Get : method;
    QCNetworkCacheRequestKey key(storageMethod, request.url());
    for (const QByteArray &name : request.rawHeaderList()) {
        key.setRequestHeader(name, request.rawHeader(name));
    }
    if (!requestHasHeader(request, QByteArrayView("referer")) && !request.referer().isEmpty()) {
        key.setRequestHeader(QByteArrayLiteral("Referer"), request.referer().toUtf8());
    } else if (!requestHasHeader(request, QByteArrayView("referer")) && request.followLocation()
               && request.autoRefererEnabled()) {
        key.markRequestHeaderUnavailable(QByteArrayLiteral("Referer"));
    }
    if (!requestHasHeader(request, QByteArrayView("accept-encoding"))) {
        const QByteArray acceptEncoding = configuredAcceptEncoding(request);
        if (!acceptEncoding.isEmpty()) {
            key.setRequestHeader(QByteArrayLiteral("Accept-Encoding"), acceptEncoding);
        } else if (request.autoDecompressionEnabled()) {
            key.markRequestHeaderUnavailable(QByteArrayLiteral("Accept-Encoding"));
        }
    }
    if (managerUsesCookies && !requestHasHeader(request, QByteArrayView("cookie"))) {
        key.markRequestHeaderUnavailable(QByteArrayLiteral("Cookie"));
    }
    key.setCachePartitionKey(request.cachePartitionKey());
    key.setAuthenticationContext(requestHasAuthenticationContext(request, managerUsesCookies));
    return key;
}

QCNetworkCacheMetadata buildCacheMetadata(
    const QCNetworkCacheRequestKey &key,
    int statusCode,
    const QList<QCNetworkCacheMetadata::RawHeaderPair> &rawResponseHeaders,
    qint64 responseDelayMs)
{
    const QDateTime responseTime = QDateTime::currentDateTimeUtc();
    const qint64 boundedDelay    = qMax<qint64>(0, responseDelayMs);
    const QDateTime requestTime  = boundedDelay > responseTime.toMSecsSinceEpoch()
                                       ? QDateTime::fromMSecsSinceEpoch(0, QTimeZone::UTC)
                                       : responseTime.addMSecs(-boundedDelay);
    const ResponseHeaderIndex responseHeaders(rawResponseHeaders);
    const qint64 initialAge = correctedInitialAgeSeconds(responseHeaders, requestTime, responseTime);
    QCNetworkCacheMetadata metadata;
    metadata.setUrl(key.normalizedUrl());
    metadata.setRawHeaders(rawResponseHeaders);
    metadata.setStatusCode(statusCode);
    metadata.setVaryHeaderNames(QCNetworkCache::varyHeaderNames(rawResponseHeaders));
    metadata.setRequestTime(requestTime);
    metadata.setResponseTime(responseTime);
    metadata.setCorrectedInitialAgeSeconds(initialAge);
    metadata.setExpirationDate(cacheExpirationDate(responseHeaders, responseTime, initialAge));
    return metadata;
}

QCNetworkCacheMetadata buildCacheMetadata(
    const QCNetworkCacheRequestKey &key,
    int statusCode,
    const QMap<QByteArray, QByteArray> &responseHeaders,
    const QList<QCNetworkCacheMetadata::RawHeaderPair> &rawResponseHeaders,
    qint64 responseDelayMs)
{
    if (!rawResponseHeaders.isEmpty()) {
        return buildCacheMetadata(key, statusCode, rawResponseHeaders, responseDelayMs);
    }

    QCNetworkCacheMetadata metadata;
    metadata.setUrl(key.normalizedUrl());
    metadata.setHeaders(responseHeaders);
    metadata.setStatusCode(statusCode);
    const QDateTime responseTime = QDateTime::currentDateTimeUtc();
    metadata.setRequestTime(responseTime);
    metadata.setResponseTime(responseTime);
    metadata.setCorrectedInitialAgeSeconds(std::numeric_limits<qint64>::max());
    metadata.setExpirationDate(responseTime);
    return metadata;
}

bool responseHeadersAreCacheable(const QList<RawHeaderPair> &rawResponseHeaders)
{
    return QCNetworkCache::isCacheable(rawResponseHeaders);
}

bool cacheMetadataHasValidator(const QCNetworkCacheMetadata &metadata)
{
    return !metadataHeader(metadata, QByteArrayView("etag")).isEmpty()
           || !metadataHeader(metadata, QByteArrayView("last-modified")).isEmpty();
}

QCNetworkRequest requestWithCacheValidators(const QCNetworkRequest &request,
                                            const QCNetworkCacheMetadata &metadata)
{
    QCNetworkRequest conditioned = request;
    const QByteArray etag        = metadataHeader(metadata, QByteArrayView("etag"));
    if (!etag.isEmpty()) {
        conditioned.setRawHeader(QByteArrayLiteral("If-None-Match"), etag);
    }
    const QByteArray lastModified = metadataHeader(metadata, QByteArrayView("last-modified"));
    if (!lastModified.isEmpty()) {
        conditioned.setRawHeader(QByteArrayLiteral("If-Modified-Since"), lastModified);
    }
    return conditioned;
}

QList<QCNetworkCacheMetadata::RawHeaderPair> mergeRevalidatedRawHeaders(
    const QList<QCNetworkCacheMetadata::RawHeaderPair> &cachedHeaders,
    const QList<QCNetworkCacheMetadata::RawHeaderPair> &validationHeaders)
{
    QSet<QByteArray> replacementNames;
    for (const auto &[name, value] : validationHeaders) {
        Q_UNUSED(value);
        if (!isCacheSensitiveHeader(QByteArrayView(name))) {
            replacementNames.insert(name.toLower());
        }
    }

    QList<QCNetworkCacheMetadata::RawHeaderPair> merged;
    QSet<QByteArray> emittedNames;
    const auto appendValidationGroup = [&](const QByteArray &normalizedName) {
        if (emittedNames.contains(normalizedName)) {
            return;
        }
        for (const auto &header : validationHeaders) {
            if (!isCacheSensitiveHeader(QByteArrayView(header.first))
                && header.first.toLower() == normalizedName) {
                merged.append(header);
            }
        }
        emittedNames.insert(normalizedName);
    };

    for (const auto &header : cachedHeaders) {
        const QByteArray normalizedName = header.first.toLower();
        if (isCacheSensitiveHeader(QByteArrayView(header.first))) {
            continue;
        }
        if (replacementNames.contains(normalizedName)) {
            appendValidationGroup(normalizedName);
        } else {
            merged.append(header);
        }
    }
    for (const auto &[name, value] : validationHeaders) {
        Q_UNUSED(value);
        if (!isCacheSensitiveHeader(QByteArrayView(name))) {
            appendValidationGroup(name.toLower());
        }
    }
    return merged;
}

} // namespace QCurl::Internal
