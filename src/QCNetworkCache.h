/**
 * @file
 * @brief 声明缓存抽象接口与元数据结构。
 */

#ifndef QCNETWORKCACHE_H
#define QCNETWORKCACHE_H

#include "QCGlobal.h"

#include <QByteArray>
#include <QDateTime>
#include <QMap>
#include <QObject>
#include <QSharedDataPointer>
#include <QUrl>

namespace QCurl {

class QCNetworkCacheMetadataData;
class QCNetworkCacheLookupResultData;

/// HTTP 缓存条目的元数据，采用隐式共享以保持值传递成本可控。
class QCURL_EXPORT QCNetworkCacheMetadata
{
public:
    QCNetworkCacheMetadata();
    QCNetworkCacheMetadata(const QCNetworkCacheMetadata &other);
    QCNetworkCacheMetadata(QCNetworkCacheMetadata &&other) noexcept;
    ~QCNetworkCacheMetadata();

    QCNetworkCacheMetadata &operator=(const QCNetworkCacheMetadata &other);
    QCNetworkCacheMetadata &operator=(QCNetworkCacheMetadata &&other) noexcept;

    [[nodiscard]] QUrl url() const;
    void setUrl(const QUrl &url);

    [[nodiscard]] QMap<QByteArray, QByteArray> headers() const;
    void setHeaders(const QMap<QByteArray, QByteArray> &headers);
    void setHeader(const QByteArray &name, const QByteArray &value);

    [[nodiscard]] QDateTime expirationDate() const;
    void setExpirationDate(const QDateTime &expirationDate);

    [[nodiscard]] QDateTime lastModified() const;
    void setLastModified(const QDateTime &lastModified);

    [[nodiscard]] QDateTime creationDate() const;
    void setCreationDate(const QDateTime &creationDate);

    [[nodiscard]] qint64 size() const;
    void setSize(qint64 size);

    /**
     * @brief 检查缓存是否仍在有效期内。
     * @return 未设置过期时间或当前时间早于过期时间时返回 true。
     */
    [[nodiscard]] bool isValid() const;

private:
    QSharedDataPointer<QCNetworkCacheMetadataData> d;
};

/// 控制缓存读取是否允许返回已过期条目。
enum class QCNetworkCacheReadMode
{
    /// 只接受未过期条目，过期条目按未命中处理。
    FreshOnly,
    /// 允许返回已存在的过期条目，由调用方决定后续处理。
    AllowStale,
};

/// 描述标准缓存查询结果。
enum class QCNetworkCacheLookupStatus
{
    /// 未找到可返回的缓存条目。
    Miss,
    /// 找到未过期条目。
    FreshHit,
    /// 找到已过期条目，仅在 AllowStale 模式下返回。
    StaleHit,
};

/// QCNetworkCache::lookup() 的返回值，status 用于区分未命中和空响应命中。
class QCURL_EXPORT QCNetworkCacheLookupResult
{
public:
    QCNetworkCacheLookupResult();
    QCNetworkCacheLookupResult(const QCNetworkCacheLookupResult &other);
    QCNetworkCacheLookupResult(QCNetworkCacheLookupResult &&other) noexcept;
    ~QCNetworkCacheLookupResult();

    QCNetworkCacheLookupResult &operator=(const QCNetworkCacheLookupResult &other);
    QCNetworkCacheLookupResult &operator=(QCNetworkCacheLookupResult &&other) noexcept;

    [[nodiscard]] QCNetworkCacheLookupStatus status() const;
    void setStatus(QCNetworkCacheLookupStatus status);

    [[nodiscard]] QCNetworkCacheMetadata metadata() const;
    void setMetadata(const QCNetworkCacheMetadata &metadata);

    [[nodiscard]] QByteArray body() const;
    void setBody(const QByteArray &body);

    /// 查询命中缓存时返回 true；空响应体仍可视为命中。
    [[nodiscard]] bool hit() const;

private:
    QSharedDataPointer<QCNetworkCacheLookupResultData> d;
};

/**
 * @brief HTTP 缓存抽象基类
 *
 * 定义缓存接口，支持内存缓存和磁盘缓存两种实现。
 *
 * @par 使用示例
 * @code
 * auto *cache = new QCNetworkMemoryCache();
 * cache->setMaxCacheSize(20 * 1024 * 1024);  // 20MB
 * manager->setCache(cache);
 * @endcode
 *
 */
class QCURL_EXPORT QCNetworkCache : public QObject
{
    Q_OBJECT

public:
    /// 构造缓存抽象基类。
    explicit QCNetworkCache(QObject *parent = nullptr)
        : QObject(parent)
    {}
    /// 通过多态接口释放缓存实现。
    ~QCNetworkCache() override = default;

    /**
     * @brief 标准缓存读取接口。
     * @param url 请求 URL。
     * @param mode 是否允许读取已过期条目。
     * @return 同时携带命中状态、元数据和响应体。
     */
    [[nodiscard]] virtual QCNetworkCacheLookupResult lookup(const QUrl &url,
                                                            QCNetworkCacheReadMode mode) = 0;

    /**
     * @brief 插入数据到缓存
     * @param url 请求 URL
     * @param data 响应数据
     * @param meta 元数据
     */
    virtual void insert(const QUrl &url,
                        const QByteArray &data,
                        const QCNetworkCacheMetadata &meta) = 0;

    /**
     * @brief 从缓存中移除指定 URL 的数据
     * @param url 请求 URL
     * @return true 如果成功移除
     */
    virtual bool remove(const QUrl &url) = 0;

    /**
     * @brief 清空所有缓存
     */
    virtual void clear() = 0;

    /**
     * @brief 获取当前缓存大小
     * @return 缓存大小（字节）
     */
    [[nodiscard]] virtual qint64 cacheSize() const = 0;

    /**
     * @brief 获取最大缓存大小
     * @return 最大缓存大小（字节）
     */
    [[nodiscard]] virtual qint64 maxCacheSize() const = 0;

    /**
     * @brief 设置最大缓存大小
     * @param size 最大缓存大小（字节）
     */
    virtual void setMaxCacheSize(qint64 size) = 0;

    /**
     * @brief 从 HTTP 响应头解析过期时间
     * @param headers HTTP 响应头
     * @return 过期时间，如果无法解析返回 null QDateTime
     */
    [[nodiscard]] static QDateTime parseExpirationDate(const QMap<QByteArray, QByteArray> &headers);

    /**
     * @brief 检查响应是否可缓存
     * @param headers HTTP 响应头
     * @return true 如果可以缓存
     */
    [[nodiscard]] static bool isCacheable(const QMap<QByteArray, QByteArray> &headers);

};

} // namespace QCurl

#endif // QCNETWORKCACHE_H
