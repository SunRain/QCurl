#include "QCNetworkMemoryCache.h"

#include "private/QCNetworkCacheKey_p.h"

#include <QHash>
#include <QMutex>
#include <QMutexLocker>
#include <QSharedPointer>

namespace QCurl {

struct QCNetworkMemoryCacheEntry
{
    QByteArray primaryDigest;
    QByteArray variantDigest;
    QList<QByteArray> varyHeaderNames;
    QByteArray data;
    QCNetworkCacheMetadata metadata;
    quint64 lastAccess     = 0;
    quint64 storedSequence = 0;

    [[nodiscard]] qint64 size() const noexcept { return data.size(); }
};

/**
 * @brief 保存内存缓存条目、容量统计和淘汰状态的内部 PIMPL 数据。
 */
class QCNetworkMemoryCachePrivate
{
public:
    using EntryPointer = QSharedPointer<QCNetworkMemoryCacheEntry>;

    mutable QMutex mutex;
    QHash<QString, EntryPointer> entries;
    qint64 maxSize         = 10 * 1024 * 1024;
    qint64 currentSize     = 0;
    quint64 accessSequence = 0;
    quint64 storeSequence  = 0;

    [[nodiscard]] EntryPointer matchingEntry(const QCNetworkCacheRequestKey &key)
    {
        const QByteArray primary = Internal::cachePrimaryDigest(key);
        EntryPointer matched;
        qint64 matchedTimestamp = std::numeric_limits<qint64>::min();
        for (const EntryPointer &entry : entries) {
            if (entry->primaryDigest != primary
                || !Internal::cacheVaryDimensionsAvailable(key, entry->varyHeaderNames)
                || entry->variantDigest
                       != Internal::cacheVariantDigest(key, entry->varyHeaderNames)) {
                continue;
            }
            const qint64 timestamp = Internal::cacheResponseSelectionTimestamp(entry->metadata);
            if (!matched || timestamp > matchedTimestamp
                || (timestamp == matchedTimestamp
                    && entry->storedSequence > matched->storedSequence)) {
                matched          = entry;
                matchedTimestamp = timestamp;
            }
        }
        return matched;
    }

    void removeEntry(const QString &entryId)
    {
        const EntryPointer entry = entries.take(entryId);
        if (entry) {
            currentSize -= entry->size();
        }
    }

    void evictIfNeeded()
    {
        while (currentSize > maxSize && !entries.isEmpty()) {
            auto oldest = entries.cbegin();
            for (auto it = entries.cbegin(); it != entries.cend(); ++it) {
                if (it.value()->lastAccess < oldest.value()->lastAccess) {
                    oldest = it;
                }
            }
            removeEntry(oldest.key());
        }
    }
};

QCNetworkMemoryCache::QCNetworkMemoryCache(QObject *parent)
    : QCNetworkCache(parent)
    , d_ptr(new QCNetworkMemoryCachePrivate)
{}

QCNetworkMemoryCache::~QCNetworkMemoryCache()
{
    (void)clear();
}

QCNetworkCacheLookupResult QCNetworkMemoryCache::lookup(const QCNetworkCacheRequestKey &key,
                                                        QCNetworkCacheReadMode mode)
{
    QMutexLocker locker(&d_ptr->mutex);
    if (key.hasAuthenticationContext() && key.cachePartitionKey().isEmpty()) {
        return {};
    }

    const auto entry = d_ptr->matchingEntry(key);
    if (!entry) {
        return {};
    }

    const bool fresh = entry->metadata.isValid();
    if (!fresh && mode == QCNetworkCacheReadMode::FreshOnly) {
        return {};
    }

    entry->lastAccess = ++d_ptr->accessSequence;
    QCNetworkCacheLookupResult result;
    result.setStatus(fresh ? QCNetworkCacheLookupStatus::FreshHit
                           : QCNetworkCacheLookupStatus::StaleHit);
    result.setMetadata(entry->metadata);
    result.setBody(entry->data);
    return result;
}

void QCNetworkMemoryCache::insert(const QCNetworkCacheRequestKey &key,
                                  const QByteArray &data,
                                  const QCNetworkCacheMetadata &meta)
{
    QMutexLocker locker(&d_ptr->mutex);
    if (!Internal::cacheEntryMayBeStored(key, meta) || data.size() > d_ptr->maxSize) {
        return;
    }

    const QList<QByteArray> varyNames = Internal::normalizedVaryHeaderNames(meta.varyHeaderNames());
    const QString entryId             = Internal::cacheEntryId(key, varyNames);
    d_ptr->removeEntry(entryId);

    auto entry             = QSharedPointer<QCNetworkMemoryCacheEntry>::create();
    entry->primaryDigest   = Internal::cachePrimaryDigest(key);
    entry->variantDigest   = Internal::cacheVariantDigest(key, varyNames);
    entry->varyHeaderNames = varyNames;
    entry->data            = data;
    entry->metadata        = meta;
    entry->metadata.setSize(data.size());
    entry->metadata.setCreationDate(QDateTime::currentDateTime());
    entry->metadata.setVaryHeaderNames(varyNames);
    entry->lastAccess     = ++d_ptr->accessSequence;
    entry->storedSequence = ++d_ptr->storeSequence;

    d_ptr->entries.insert(entryId, entry);
    d_ptr->currentSize += entry->size();
    d_ptr->evictIfNeeded();
}

bool QCNetworkMemoryCache::remove(const QCNetworkCacheRequestKey &key)
{
    QMutexLocker locker(&d_ptr->mutex);
    const QByteArray primary = Internal::cachePrimaryDigest(key);
    bool removed             = false;
    for (auto it = d_ptr->entries.begin(); it != d_ptr->entries.end();) {
        const auto &entry = it.value();
        if (entry->primaryDigest == primary
            && entry->variantDigest == Internal::cacheVariantDigest(key, entry->varyHeaderNames)) {
            d_ptr->currentSize -= entry->size();
            it      = d_ptr->entries.erase(it);
            removed = true;
        } else {
            ++it;
        }
    }
    return removed;
}

QCNetworkCacheClearResult QCNetworkMemoryCache::clear()
{
    QMutexLocker locker(&d_ptr->mutex);
    const qint64 removedCount = d_ptr->entries.size();
    d_ptr->entries.clear();
    d_ptr->currentSize = 0;
    return QCNetworkCacheClearResult::success(removedCount, 0);
}

qint64 QCNetworkMemoryCache::cacheSize() const
{
    QMutexLocker locker(&d_ptr->mutex);
    return d_ptr->currentSize;
}

qint64 QCNetworkMemoryCache::maxCacheSize() const
{
    QMutexLocker locker(&d_ptr->mutex);
    return d_ptr->maxSize;
}

void QCNetworkMemoryCache::setMaxCacheSize(qint64 size)
{
    QMutexLocker locker(&d_ptr->mutex);
    d_ptr->maxSize = qMax<qint64>(0, size);
    d_ptr->evictIfNeeded();
}

} // namespace QCurl
