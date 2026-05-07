#include "QCNetworkMemoryCache.h"

#include <QCache>
#include <QMutex>
#include <QMutexLocker>

#include <memory>

namespace QCurl {

/// 内存缓存条目，保存响应体及用于新鲜度判断的元数据。
struct QCNetworkMemoryCacheEntry
{
    QByteArray data;
    QCNetworkCacheMetadata metadata;

    qint64 size() const { return data.size(); }
};

/// 内存缓存内部状态；QCache 和 currentSize 由 mutex 统一保护。
class QCNetworkMemoryCachePrivate
{
public:
    mutable QMutex mutex;
    QCache<QString, QCNetworkMemoryCacheEntry> cache;
    qint64 maxSize     = 10 * 1024 * 1024;
    qint64 currentSize = 0;

    QString cacheKey(const QUrl &url) const
    {
        return url.toString();
    }
};

QCNetworkMemoryCache::QCNetworkMemoryCache(QObject *parent)
    : QCNetworkCache(parent)
    , d_ptr(new QCNetworkMemoryCachePrivate)
{
    d_ptr->cache.setMaxCost(d_ptr->maxSize);
}

QCNetworkMemoryCache::~QCNetworkMemoryCache()
{
    clear();
}

QCNetworkCacheLookupResult QCNetworkMemoryCache::lookup(const QUrl &url,
                                                        QCNetworkCacheReadMode mode)
{
    QMutexLocker locker(&d_ptr->mutex);

    const QString key = d_ptr->cacheKey(url);
    QCNetworkMemoryCacheEntry *entry = d_ptr->cache.object(key);
    if (!entry) {
        return {};
    }

    const bool fresh = entry->metadata.isValid();
    if (!fresh && mode == QCNetworkCacheReadMode::FreshOnly) {
        // FreshOnly 不返回过期条目，同时清理它避免后续重复命中。
        d_ptr->currentSize -= entry->size();
        d_ptr->cache.remove(key);
        return {};
    }

    QCNetworkCacheLookupResult result;
    result.setStatus(fresh ? QCNetworkCacheLookupStatus::FreshHit
                           : QCNetworkCacheLookupStatus::StaleHit);
    result.setMetadata(entry->metadata);
    result.setBody(entry->data);
    return result;
}

void QCNetworkMemoryCache::insert(const QUrl &url,
                                  const QByteArray &data,
                                  const QCNetworkCacheMetadata &meta)
{
    QMutexLocker locker(&d_ptr->mutex);

    // 检查是否可缓存
    if (!isCacheable(meta.headers())) {
        return;
    }

    QString key     = d_ptr->cacheKey(url);
    qint64 dataSize = data.size();

    // 如果数据太大，不缓存
    if (dataSize > d_ptr->maxSize) {
        return;
    }

    // 移除旧条目（如果存在）
    if (d_ptr->cache.contains(key)) {
        QCNetworkMemoryCacheEntry *oldEntry = d_ptr->cache.object(key);
        if (oldEntry) {
            d_ptr->currentSize -= oldEntry->size();
        }
        d_ptr->cache.remove(key);
    }

    auto entry      = std::make_unique<QCNetworkMemoryCacheEntry>();
    entry->data     = data;
    entry->metadata = meta;
    entry->metadata.setSize(dataSize);
    entry->metadata.setCreationDate(QDateTime::currentDateTime());

    // 插入缓存（QCache 会自动处理 LRU 淘汰）
    const int cost = dataSize > 0 ? static_cast<int>(dataSize) : 1;
    if (d_ptr->cache.insert(key, entry.get(), cost)) {
        d_ptr->currentSize += dataSize;
        static_cast<void>(entry.release());
    }
}

bool QCNetworkMemoryCache::remove(const QUrl &url)
{
    QMutexLocker locker(&d_ptr->mutex);

    QString key = d_ptr->cacheKey(url);
    QCNetworkMemoryCacheEntry *entry = d_ptr->cache.object(key);
    if (entry) {
        d_ptr->currentSize -= entry->size();
        return d_ptr->cache.remove(key);
    }

    return false;
}

void QCNetworkMemoryCache::clear()
{
    QMutexLocker locker(&d_ptr->mutex);
    d_ptr->cache.clear();
    d_ptr->currentSize = 0;
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
    d_ptr->maxSize = size;
    d_ptr->cache.setMaxCost(size);
}

} // namespace QCurl
