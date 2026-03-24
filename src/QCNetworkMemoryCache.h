/**
 * @file
 * @brief 声明内存缓存实现。
 */

#ifndef QCNETWORKMEMORYCACHE_H
#define QCNETWORKMEMORYCACHE_H

#include "QCNetworkCache.h"

#include <QCache>
#include <QMutex>

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
class QCURL_EXPORT QCNetworkMemoryCache : public QCNetworkCache
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

    /**
     * @brief 读取指定 URL 的缓存 body
     *
     * 未命中时返回空 QByteArray。
     */
    [[nodiscard]] QByteArray data(const QUrl &url) override;

    /// 读取指定 URL 的缓存元数据。
    [[nodiscard]] QCNetworkCacheMetadata metadata(const QUrl &url) override;

    /// 插入或覆盖指定 URL 的缓存条目。
    void insert(const QUrl &url,
                const QByteArray &data,
                const QCNetworkCacheMetadata &meta) override;

    /// 移除指定 URL 的缓存条目。
    bool remove(const QUrl &url) override;

    /// 清空当前内存缓存中的全部条目。
    void clear() override;

    /// 返回当前缓存占用的字节数。
    [[nodiscard]] qint64 cacheSize() const override;

    /// 返回缓存允许使用的最大字节数。
    [[nodiscard]] qint64 maxCacheSize() const override;

    /// 设置缓存允许使用的最大字节数。
    void setMaxCacheSize(qint64 size) override;

private:
    struct CacheEntry
    {
        QByteArray data;
        QCNetworkCacheMetadata metadata;
        /// 返回当前条目占用的数据字节数。
        qint64 size() const { return data.size(); }
    };

    mutable QMutex m_mutex;
    QCache<QString, CacheEntry> m_cache;
    qint64 m_maxSize;
    qint64 m_currentSize;

    QString cacheKey(const QUrl &url) const;
};

} // namespace QCurl

#endif // QCNETWORKMEMORYCACHE_H
