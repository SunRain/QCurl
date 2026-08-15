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
 * @note QObject 借用合同：构造参数 `parent` 继承 `QCNetworkCache` 的 owning 与
 * affinity 合同；parent 销毁后所有外部 cache 裸指针立即失效。
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

    /// 通过标准 lookup API 同时读取元数据和响应体。
    [[nodiscard]] QCNetworkCacheLookupResult lookup(const QCNetworkCacheRequestKey &key,
                                                    QCNetworkCacheReadMode mode) override;

    /// 插入或覆盖指定 URL 的缓存条目。
    void insert(const QCNetworkCacheRequestKey &key,
                const QByteArray &data,
                const QCNetworkCacheMetadata &meta) override;

    /// 移除指定 URL 的缓存条目。
    [[nodiscard]] bool remove(const QCNetworkCacheRequestKey &key) override;

    /// 清空当前内存缓存中的全部条目并返回精确删除计数。
    [[nodiscard]] QCNetworkCacheClearResult clear() override;

    /// 返回当前缓存占用的字节数。
    [[nodiscard]] qint64 cacheSize() const override;

    /// 返回缓存允许使用的最大字节数。
    [[nodiscard]] qint64 maxCacheSize() const override;

    /// 设置缓存允许使用的最大字节数。
    void setMaxCacheSize(qint64 size) override;

private:
    Q_DISABLE_COPY_MOVE(QCNetworkMemoryCache)

    QScopedPointer<QCNetworkMemoryCachePrivate> d_ptr;
};

} // namespace QCurl

#endif // QCNETWORKMEMORYCACHE_H
