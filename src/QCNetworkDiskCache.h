#ifndef QCNETWORKDISKCACHE_H
#define QCNETWORKDISKCACHE_H

#include "QCNetworkCache.h"
#include <QMutex>
#include <QDir>

QT_BEGIN_NAMESPACE

namespace QCurl {

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
class QCNetworkDiskCache : public QCNetworkCache
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
    mutable QMutex m_mutex;
    QString m_cacheDir;
    qint64 m_maxSize;
    mutable qint64 m_currentSize;

    QString cacheKey(const QUrl &url) const;
    QString dataFilePath(const QString &key) const;
    QString metaFilePath(const QString &key) const;

    bool writeMetadata(const QString &key, const QCNetworkCacheMetadata &meta);
    QCNetworkCacheMetadata readMetadata(const QString &key) const;

    void ensureCacheDirectory();
    void updateCacheSize() const;
    void evictIfNeeded(qint64 newDataSize);
};

} // namespace QCurl

QT_END_NAMESPACE

#endif // QCNETWORKDISKCACHE_H
