#include "QCNetworkMemoryCache.h"
#include <QMutexLocker>
#include <QCryptographicHash>

namespace QCurl {

QCNetworkMemoryCache::QCNetworkMemoryCache(QObject *parent)
    : QCNetworkCache(parent)
    , m_maxSize(10 * 1024 * 1024)  // 默认 10MB
    , m_currentSize(0)
{
    m_cache.setMaxCost(m_maxSize);
}

QCNetworkMemoryCache::~QCNetworkMemoryCache()
{
    clear();
}

QString QCNetworkMemoryCache::cacheKey(const QUrl &url) const
{
    return url.toString();
}

QByteArray QCNetworkMemoryCache::data(const QUrl &url)
{
    QMutexLocker locker(&m_mutex);

    CacheEntry *entry = m_cache.object(cacheKey(url));
    if (!entry) {
        return QByteArray();
    }

    // 检查是否过期
    if (!entry->metadata.isValid()) {
        m_cache.remove(cacheKey(url));
        return QByteArray();
    }

    return entry->data;
}

QCNetworkCacheMetadata QCNetworkMemoryCache::metadata(const QUrl &url)
{
    QMutexLocker locker(&m_mutex);

    CacheEntry *entry = m_cache.object(cacheKey(url));
    if (!entry) {
        return QCNetworkCacheMetadata();
    }

    return entry->metadata;
}

void QCNetworkMemoryCache::insert(const QUrl &url, const QByteArray &data,
                                   const QCNetworkCacheMetadata &meta)
{
    QMutexLocker locker(&m_mutex);

    // 检查是否可缓存
    if (!isCacheable(meta.headers)) {
        return;
    }

    QString key = cacheKey(url);
    qint64 dataSize = data.size();

    // 如果数据太大，不缓存
    if (dataSize > m_maxSize) {
        return;
    }

    // 移除旧条目（如果存在）
    if (m_cache.contains(key)) {
        CacheEntry *oldEntry = m_cache.object(key);
        if (oldEntry) {
            m_currentSize -= oldEntry->size();
        }
        m_cache.remove(key);
    }

    // 创建新条目
    auto *entry = new CacheEntry();
    entry->data = data;
    entry->metadata = meta;
    entry->metadata.size = dataSize;
    entry->metadata.creationDate = QDateTime::currentDateTime();

    // 插入缓存（QCache 会自动处理 LRU 淘汰）
    bool inserted = m_cache.insert(key, entry, dataSize);
    if (inserted) {
        m_currentSize += dataSize;
    } else {
        delete entry;  // 如果插入失败，需要手动删除
    }
}

bool QCNetworkMemoryCache::remove(const QUrl &url)
{
    QMutexLocker locker(&m_mutex);

    QString key = cacheKey(url);
    CacheEntry *entry = m_cache.object(key);
    if (entry) {
        m_currentSize -= entry->size();
        return m_cache.remove(key);
    }

    return false;
}

void QCNetworkMemoryCache::clear()
{
    QMutexLocker locker(&m_mutex);
    m_cache.clear();
    m_currentSize = 0;
}

qint64 QCNetworkMemoryCache::cacheSize() const
{
    QMutexLocker locker(&m_mutex);
    return m_currentSize;
}

qint64 QCNetworkMemoryCache::maxCacheSize() const
{
    QMutexLocker locker(&m_mutex);
    return m_maxSize;
}

void QCNetworkMemoryCache::setMaxCacheSize(qint64 size)
{
    QMutexLocker locker(&m_mutex);
    m_maxSize = size;
    m_cache.setMaxCost(size);
}

} // namespace QCurl
