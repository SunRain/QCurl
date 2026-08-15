/**
 * @file
 * @brief Internal structured-cache identity helpers.
 */

#ifndef QCNETWORKCACHEKEY_P_H
#define QCNETWORKCACHEKEY_P_H

#include "QCNetworkCache.h"
#include "QCNetworkCacheRequestKey.h"
#include "private/QCHttpDate_p.h"

#include <QCryptographicHash>
#include <QDataStream>
#include <QIODevice>
#include <QSet>

#include <algorithm>
#include <limits>

namespace QCurl::Internal {

[[nodiscard]] inline QList<QByteArray> normalizedVaryHeaderNames(const QList<QByteArray> &headerNames)
{
    QSet<QByteArray> uniqueNames;
    for (const QByteArray &name : headerNames) {
        const QByteArray normalized = name.trimmed().toLower();
        if (!normalized.isEmpty()) {
            uniqueNames.insert(normalized);
        }
    }

    QList<QByteArray> names(uniqueNames.cbegin(), uniqueNames.cend());
    std::sort(names.begin(), names.end());
    return names;
}

[[nodiscard]] inline QByteArray digestCacheIdentity(const QByteArray &payload)
{
    return QCryptographicHash::hash(payload, QCryptographicHash::Sha256);
}

[[nodiscard]] inline qint64 cacheResponseSelectionTimestamp(const QCNetworkCacheMetadata &metadata)
{
    QList<QByteArray> dateValues;
    for (const auto &[name, value] : metadata.rawHeaders()) {
        if (QByteArrayView(name).compare(QByteArrayView("date"), Qt::CaseInsensitive) == 0) {
            dateValues.append(value.trimmed());
        }
    }
    if (dateValues.size() == 1) {
        const QDateTime responseDate = parseHttpDate(dateValues.constFirst());
        if (responseDate.isValid()) {
            return responseDate.toMSecsSinceEpoch();
        }
    }

    const QDateTime creationDate = metadata.creationDate();
    return creationDate.isValid() ? creationDate.toMSecsSinceEpoch()
                                  : std::numeric_limits<qint64>::min();
}

[[nodiscard]] inline QByteArray cachePrimaryDigest(const QCNetworkCacheRequestKey &key)
{
    QByteArray payload;
    QDataStream stream(&payload, QIODevice::WriteOnly);
    stream.setVersion(QDataStream::Qt_6_0);
    stream << static_cast<qint32>(key.method())
           << key.normalizedUrl().toString(QUrl::FullyEncoded).toUtf8()
           << digestCacheIdentity(key.cachePartitionKey());
    return digestCacheIdentity(payload);
}

[[nodiscard]] inline QByteArray cacheVariantDigest(const QCNetworkCacheRequestKey &key,
                                                   const QList<QByteArray> &varyHeaderNames)
{
    QByteArray payload;
    QDataStream stream(&payload, QIODevice::WriteOnly);
    stream.setVersion(QDataStream::Qt_6_0);
    for (const QByteArray &name : normalizedVaryHeaderNames(varyHeaderNames)) {
        stream << name << key.requestHeader(name);
    }
    return digestCacheIdentity(payload);
}

[[nodiscard]] inline bool cacheVaryDimensionsAvailable(const QCNetworkCacheRequestKey &key,
                                                       const QList<QByteArray> &varyHeaderNames)
{
    return std::all_of(varyHeaderNames.cbegin(),
                       varyHeaderNames.cend(),
                       [&key](const QByteArray &name) {
                           return key.isRequestHeaderAvailable(name);
                       });
}

[[nodiscard]] inline QString cacheEntryId(const QCNetworkCacheRequestKey &key,
                                          const QList<QByteArray> &varyHeaderNames)
{
    return QString::fromLatin1(cachePrimaryDigest(key).toHex()) + QLatin1Char('-')
           + QString::fromLatin1(cacheVariantDigest(key, varyHeaderNames).toHex());
}

[[nodiscard]] inline bool cacheEntryMayBeStored(const QCNetworkCacheRequestKey &key,
                                                const QCNetworkCacheMetadata &metadata)
{
    if (key.method() != HttpMethod::Get || metadata.statusCode() != 200) {
        return false;
    }
    if (key.hasAuthenticationContext() && key.cachePartitionKey().isEmpty()) {
        return false;
    }
    if (!cacheVaryDimensionsAvailable(key, metadata.varyHeaderNames())) {
        return false;
    }
    return QCNetworkCache::isCacheable(metadata.rawHeaders());
}

} // namespace QCurl::Internal

#endif // QCNETWORKCACHEKEY_P_H
