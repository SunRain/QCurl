#ifndef QCNETWORKMEMORYCACHE_H
#define QCNETWORKMEMORYCACHE_H

#include "QCNetworkCache.h"
#include <QCache>
#include <QMutex>

QT_BEGIN_NAMESPACE

namespace QCurl {

/**
 * @brief 内存缓存实现
 *
 * 使用 QCache 实现 LRU（最近最少使用）淘汰策略。
 * 线程安全，支持多线程并发访问。
 *
 * @par 使用示例
 * @code
 * auto *cache = new QCNetworkMemoryCache();
 * cache->setMaxCacheSize(10 * 1024 * 1024);  // 10MB
 * manager->setCache(cache);
 * @endcode
 *
 */
class QCNetworkMemoryCache : public QCNetworkCache
{
    Q_OBJECT

public:
    /**
     * @brief 构造函数
     * @param parent 父对象
     */
    explicit QCNetworkMemoryCache(QObject *parent = nullptr);

    /**
     * @brief 析构函数
     */
    ~QCNetworkMemoryCache() override;

    // QCNetworkCache 接口实现
    [[nodiscard]] QByteArray data(const QUrl &url) override;
    [[nodiscard]] QCNetworkCacheMetadata metadata(const QUrl &url) override;
    void insert(const QUrl &url, const QByteArray &data,
                const QCNetworkCacheMetadata &meta) override;
    bool remove(const QUrl &url) override;
    void clear() override;
    [[nodiscard]] qint64 cacheSize() const override;
    [[nodiscard]] qint64 maxCacheSize() const override;
    void setMaxCacheSize(qint64 size) override;

private:
    struct CacheEntry {
        QByteArray data;
        QCNetworkCacheMetadata metadata;
        qint64 size() const { return data.size(); }
    };

    mutable QMutex m_mutex;
    QCache<QString, CacheEntry> m_cache;
    qint64 m_maxSize;
    qint64 m_currentSize;

    QString cacheKey(const QUrl &url) const;
};

} // namespace QCurl

QT_END_NAMESPACE

#endif // QCNETWORKMEMORYCACHE_H
