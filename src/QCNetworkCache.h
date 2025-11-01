#ifndef QCNETWORKCACHE_H
#define QCNETWORKCACHE_H

#include <QObject>
#include <QUrl>
#include <QByteArray>
#include <QDateTime>
#include <QMap>

QT_BEGIN_NAMESPACE

namespace QCurl {

/**
 * @brief HTTP 缓存元数据
 *
 * 存储缓存条目的元信息，包括 URL、响应头、过期时间等。
 *
 */
struct QCNetworkCacheMetadata {
    QUrl url;                                    ///< 请求 URL
    QMap<QByteArray, QByteArray> headers;        ///< HTTP 响应头
    QDateTime expirationDate;                    ///< 缓存过期时间
    QDateTime lastModified;                      ///< 最后修改时间
    QDateTime creationDate;                      ///< 缓存创建时间
    qint64 size = 0;                             ///< 数据大小（字节）

    /**
     * @brief 检查缓存是否有效（未过期）
     * @return true 如果缓存仍然有效
     */
    [[nodiscard]] bool isValid() const {
        return expirationDate.isNull() || QDateTime::currentDateTime() < expirationDate;
    }
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
class QCNetworkCache : public QObject
{
    Q_OBJECT

public:
    explicit QCNetworkCache(QObject *parent = nullptr) : QObject(parent) {}
    ~QCNetworkCache() override = default;

    /**
     * @brief 从缓存中读取数据
     * @param url 请求 URL
     * @return 缓存的响应数据，如果不存在返回空 QByteArray
     */
    [[nodiscard]] virtual QByteArray data(const QUrl &url) = 0;

    /**
     * @brief 从缓存中读取元数据
     * @param url 请求 URL
     * @return 缓存的元数据，如果不存在返回默认构造的对象
     */
    [[nodiscard]] virtual QCNetworkCacheMetadata metadata(const QUrl &url) = 0;

    /**
     * @brief 插入数据到缓存
     * @param url 请求 URL
     * @param data 响应数据
     * @param meta 元数据
     */
    virtual void insert(const QUrl &url, const QByteArray &data,
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

protected:
};

} // namespace QCurl

QT_END_NAMESPACE

#endif // QCNETWORKCACHE_H
