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
 * 成功命中会刷新条目的访问时间；达到容量上限时按 LRU 策略淘汰旧数据。实现线程安全。
 *
 * @par 缓存目录结构
 * @code
 * ~/.cache/QCurl/
 * └── <primary_hash>-<variant_hash>.qce
 * @endcode
 * 每个 `.qce` 文件使用有界 v4 二进制 envelope 同时保存元数据与响应体。固定头携带
 * payload 长度和 SHA-256；字段在分配前执行长度与数量配额检查，并通过 QSaveFile 原子替换。
 * 单条响应体上限为 64 MiB，元数据上限为 1 MiB；超过配额的条目不会写入或读取。
 *
 * @par 使用示例
 * @code
 * auto *cache = new QCNetworkDiskCache();
 * cache->setCacheDirectory("/tmp/qcurl_cache");
 * cache->setMaxCacheSize(50 * 1024 * 1024);  // 50MB
 * manager->setCache(cache);
 * @endcode
 *
 * @note QObject 借用合同：构造参数 `parent` 继承 `QCNetworkCache` 的 owning 与
 * affinity 合同；parent 销毁后所有外部 cache 裸指针立即失效。
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
    [[nodiscard]] QCNetworkCacheLookupResult lookup(const QCNetworkCacheRequestKey &key,
                                                    QCNetworkCacheReadMode mode) override;
    void insert(const QCNetworkCacheRequestKey &key,
                const QByteArray &data,
                const QCNetworkCacheMetadata &meta) override;
    [[nodiscard]] bool remove(const QCNetworkCacheRequestKey &key) override;
    [[nodiscard]] QCNetworkCacheClearResult clear() override;
    [[nodiscard]] qint64 cacheSize() const override;
    [[nodiscard]] qint64 maxCacheSize() const override;
    void setMaxCacheSize(qint64 size) override;

private:
    Q_DISABLE_COPY_MOVE(QCNetworkDiskCache)

    QScopedPointer<QCNetworkDiskCachePrivate> d_ptr;
};

} // namespace QCurl

#endif // QCNETWORKDISKCACHE_H
