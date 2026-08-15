/**
 * @file
 * @brief 声明缓存抽象接口与元数据结构。
 */

#ifndef QCNETWORKCACHE_H
#define QCNETWORKCACHE_H

#include "QCGlobal.h"
#include "QCNetworkCacheRequestKey.h"

#include <QByteArray>
#include <QDateTime>
#include <QList>
#include <QMap>
#include <QObject>
#include <QPair>
#include <QSharedDataPointer>
#include <QString>
#include <QUrl>

namespace QCurl {

class QCNetworkCacheMetadataData;
class QCNetworkCacheLookupResultData;
class QCNetworkCacheClearResultData;

/// HTTP 缓存条目的元数据，采用隐式共享以保持值传递成本可控。
class QCURL_EXPORT QCNetworkCacheMetadata
{
public:
    using RawHeaderPair = QPair<QByteArray, QByteArray>;

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

    /// 返回用于重建缓存响应的有序原始响应头，保留重复项与字段名大小写。
    [[nodiscard]] QList<RawHeaderPair> rawHeaders() const;
    void setRawHeaders(const QList<RawHeaderPair> &headers);

    [[nodiscard]] QDateTime expirationDate() const;
    void setExpirationDate(const QDateTime &expirationDate);

    [[nodiscard]] QDateTime lastModified() const;
    void setLastModified(const QDateTime &lastModified);

    [[nodiscard]] QDateTime creationDate() const;
    void setCreationDate(const QDateTime &creationDate);

    [[nodiscard]] QDateTime requestTime() const;
    void setRequestTime(const QDateTime &requestTime);

    [[nodiscard]] QDateTime responseTime() const;
    void setResponseTime(const QDateTime &responseTime);

    [[nodiscard]] qint64 correctedInitialAgeSeconds() const noexcept;
    void setCorrectedInitialAgeSeconds(qint64 ageSeconds) noexcept;
    [[nodiscard]] qint64 currentAgeSeconds() const noexcept;

    [[nodiscard]] qint64 size() const;
    void setSize(qint64 size);

    [[nodiscard]] int statusCode() const noexcept;
    void setStatusCode(int statusCode);

    [[nodiscard]] QList<QByteArray> varyHeaderNames() const;
    void setVaryHeaderNames(const QList<QByteArray> &headerNames);

    /**
     * @brief 检查缓存是否仍在有效期内。
     * @return 未设置过期时间或当前时间早于过期时间时返回 true。
     */
    [[nodiscard]] bool isValid() const;

private:
    QSharedDataPointer<QCNetworkCacheMetadataData> d;
};

/// 控制缓存读取是否允许返回已过期条目。
enum class QCNetworkCacheReadMode {
    /// 只接受未过期条目，过期条目按未命中处理。
    FreshOnly,
    /// 允许返回已存在的过期条目，由调用方决定后续处理。
    AllowStale,
};

/// 描述标准缓存查询结果。
enum class QCNetworkCacheLookupStatus {
    /// 未找到可返回的缓存条目。
    Miss,
    /// 找到未过期条目。
    FreshHit,
    /// 找到已过期条目，仅在 AllowStale 模式下返回。
    StaleHit,
};

/**
 * @brief `QCNetworkCache::lookup()` 的值结果。
 *
 * status 用于区分未命中和空响应命中。
 *
 * @note 错误生命周期：每次 lookup/clear 结果只描述本次操作，后续缓存操作不会改写它。
 * 成功、部分成功与失败状态决定错误码和诊断文本是否有效。
 */
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

/// QCNetworkCache::clear() 的结构化结果，精确报告已删除和残留状态。
class QCURL_EXPORT QCNetworkCacheClearResult
{
public:
    enum class Status {
        Success,
        PartialFailure,
        Failure,
    };

    enum class ErrorCode {
        None,
        EntryRemovalFailed,
        CacheDirectoryUnavailable,
    };

    QCNetworkCacheClearResult();
    QCNetworkCacheClearResult(const QCNetworkCacheClearResult &other);
    QCNetworkCacheClearResult(QCNetworkCacheClearResult &&other) noexcept;
    ~QCNetworkCacheClearResult();

    QCNetworkCacheClearResult &operator=(const QCNetworkCacheClearResult &other);
    QCNetworkCacheClearResult &operator=(QCNetworkCacheClearResult &&other) noexcept;

    /// 构造完整成功结果；remainingBytes 通常为零。
    [[nodiscard]] static QCNetworkCacheClearResult success(qint64 removedCount,
                                                           qint64 remainingBytes);
    /// 构造部分删除失败结果，消息不得包含缓存键或文件路径。
    [[nodiscard]] static QCNetworkCacheClearResult partialFailure(qint64 removedCount,
                                                                  qint64 failedCount,
                                                                  qint64 remainingBytes,
                                                                  ErrorCode errorCode,
                                                                  const QString &errorMessage);
    /// 构造完全失败结果，消息不得包含缓存键或文件路径。
    [[nodiscard]] static QCNetworkCacheClearResult failure(qint64 failedCount,
                                                           qint64 remainingBytes,
                                                           ErrorCode errorCode,
                                                           const QString &errorMessage);

    [[nodiscard]] Status status() const noexcept;
    [[nodiscard]] bool isSuccess() const noexcept;
    [[nodiscard]] qint64 removedCount() const noexcept;
    [[nodiscard]] qint64 failedCount() const noexcept;
    [[nodiscard]] qint64 remainingBytes() const noexcept;
    [[nodiscard]] ErrorCode errorCode() const noexcept;
    [[nodiscard]] QString errorMessage() const;

private:
    QSharedDataPointer<QCNetworkCacheClearResultData> d;
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
 * @note QObject 借用合同：`parent` 可为空；非空 parent 取得 cache 所有权并在销毁时使
 * 外部 cache 裸指针失效。构造时 parent 与 cache 必须位于同一 affinity thread。
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
     * @param key 包含 method、规范化 URL、请求头与认证分区的结构化请求键。
     * @param mode 是否允许读取已过期条目。
     * @return 同时携带命中状态、元数据和响应体。
     */
    [[nodiscard]] virtual QCNetworkCacheLookupResult lookup(const QCNetworkCacheRequestKey &key,
                                                            QCNetworkCacheReadMode mode) = 0;

    /**
     * @brief 插入数据到缓存
     * @param key 结构化请求键
     * @param data 响应数据
     * @param meta 元数据
     */
    virtual void insert(const QCNetworkCacheRequestKey &key,
                        const QByteArray &data,
                        const QCNetworkCacheMetadata &meta) = 0;

    /**
     * @brief 从缓存中移除指定请求变体的数据
     * @param key 结构化请求键
     * @return true 如果成功移除
     */
    [[nodiscard]] virtual bool remove(const QCNetworkCacheRequestKey &key) = 0;

    /**
     * @brief 尝试清空所有缓存。
     * @return 删除计数、失败计数、残留字节和稳定错误信息。
     */
    [[nodiscard]] virtual QCNetworkCacheClearResult clear() = 0;

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

    /// 从有序原始响应头解析过期时间；该重载是缓存策略的权威入口。
    [[nodiscard]] static QDateTime parseExpirationDate(
        const QList<QCNetworkCacheMetadata::RawHeaderPair> &headers);

    /// 检查有序原始响应头是否满足缓存安全策略。
    [[nodiscard]] static bool isCacheable(
        const QList<QCNetworkCacheMetadata::RawHeaderPair> &headers);

    /// 从有序原始响应头解析 Vary；重复行按列表字段合并。
    [[nodiscard]] static QList<QByteArray> varyHeaderNames(
        const QList<QCNetworkCacheMetadata::RawHeaderPair> &headers);

private:
    Q_DISABLE_COPY_MOVE(QCNetworkCache)
};

} // namespace QCurl

#endif // QCNETWORKCACHE_H
