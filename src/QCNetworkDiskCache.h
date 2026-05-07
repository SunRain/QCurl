/**
 * @file
 * @brief 声明磁盘缓存实现。
 */

#ifndef QCNETWORKDISKCACHE_H
#define QCNETWORKDISKCACHE_H

#include "QCNetworkCache.h"

#include <QScopedPointer>

namespace QCurl {

class QCNetworkDiskCachePrivate;

/**
 * @brief 磁盘缓存实现
 *
 * 将缓存数据持久化到磁盘，支持进程重启后恢复。
 * 使用 LRU 策略淘汰旧数据，线程安全。
 *
 * @par 缓存目录结构
 * @code
 * ~/.cache/QCurl/
 * ├── <url_hash>.data  // 响应数据
 * └── <url_hash>.meta  // 元数据（JSON）
 * @endcode
 *
 * @par 使用示例
 * @code
 * auto *cache = new QCNetworkDiskCache();
 * cache->setCacheDirectory("/tmp/qcurl_cache");
 * cache->setMaxCacheSize(50 * 1024 * 1024);  // 50MB
 * manager->setCache(cache);
 * @endcode
 *
 */
class QCURL_EXPORT QCNetworkDiskCache : public QCNetworkCache
{
    Q_OBJECT

public:
    /**
     * @brief 构造函数
     * @param parent 父对象
     */
    explicit QCNetworkDiskCache(QObject *parent = nullptr);

    /**
     * @brief 析构函数
     */
    ~QCNetworkDiskCache() override;

    Q_DISABLE_COPY_MOVE(QCNetworkDiskCache)

    /**
     * @brief 设置缓存目录
     * @param path 缓存目录路径
     */
    void setCacheDirectory(const QString &path);

    /**
     * @brief 获取缓存目录
     * @return 缓存目录路径
     */
    [[nodiscard]] QString cacheDirectory() const;

    // QCNetworkCache 接口实现
    [[nodiscard]] QCNetworkCacheLookupResult lookup(const QUrl &url,
                                                    QCNetworkCacheReadMode mode) override;
    void insert(const QUrl &url,
                const QByteArray &data,
                const QCNetworkCacheMetadata &meta) override;
    bool remove(const QUrl &url) override;
    void clear() override;
    [[nodiscard]] qint64 cacheSize() const override;
    [[nodiscard]] qint64 maxCacheSize() const override;
    void setMaxCacheSize(qint64 size) override;

private:
    QScopedPointer<QCNetworkDiskCachePrivate> d_ptr;
};

} // namespace QCurl

#endif // QCNETWORKDISKCACHE_H
