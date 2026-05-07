/**
 * @file
 * @brief 声明内存缓存实现。
 */

#ifndef QCNETWORKMEMORYCACHE_H
#define QCNETWORKMEMORYCACHE_H

#include "QCNetworkCache.h"

#include <QScopedPointer>

namespace QCurl {

class QCNetworkMemoryCachePrivate;

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

    Q_DISABLE_COPY_MOVE(QCNetworkMemoryCache)

    /// 通过标准 lookup API 同时读取元数据和响应体。
    [[nodiscard]] QCNetworkCacheLookupResult lookup(const QUrl &url,
                                                    QCNetworkCacheReadMode mode) override;

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
    QScopedPointer<QCNetworkMemoryCachePrivate> d_ptr;
};

} // namespace QCurl

#endif // QCNETWORKMEMORYCACHE_H
