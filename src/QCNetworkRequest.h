#ifndef QCNETWORKREQUEST_H
#define QCNETWORKREQUEST_H

#include <QString>
#include <QSharedDataPointer>
#include <QList>
#include <QByteArray>
#include <QUrl>

#include <chrono>
#include <optional>

// 前向声明新配置类
namespace QCurl {
class QCNetworkRequestPrivate;
class QCNetworkSslConfig;
class QCNetworkProxyConfig;
class QCNetworkTimeoutConfig;
class QCNetworkRetryPolicy;
enum class QCNetworkHttpVersion;
enum class QCNetworkRequestPriority;
enum class QCNetworkCachePolicy;

/**
 * @brief HTTP 请求配置类
 *
 * QCNetworkRequest 封装了 HTTP 请求的所有配置选项，包括：
 * - URL 和 Headers
 * - SSL/TLS 配置
 * - 代理设置
 * - 超时配置
 * - HTTP 版本选择
 * - 重定向和 Range 请求
 *
 * 支持流式接口（方法链）：
 * @code
 * QCNetworkRequest request(url);
 * request.setTimeout(std::chrono::seconds(30))
 *        .setSslConfig(QCNetworkSslConfig::defaultConfig())
 *        .setHttpVersion(QCNetworkHttpVersion::Http2);
 * @endcode
 *
 * (基础功能)
 */
class QCNetworkRequest
{
public:
    QCNetworkRequest();
    QCNetworkRequest(const QUrl &url);
    QCNetworkRequest(const QCNetworkRequest &other);
    virtual ~QCNetworkRequest();

    QCNetworkRequest &operator=(const QCNetworkRequest &other);
    bool operator==(const QCNetworkRequest &other) const;
    bool operator!=(const QCNetworkRequest &other) const;

    // ========== 基础配置 ==========

    QUrl url() const;

    /**
     * @brief 设置是否跟随 HTTP 重定向
     * @param followLocation true 为跟随重定向（默认），false 为不跟随
     * @return 返回 *this 以支持方法链
     */
    QCNetworkRequest& setFollowLocation(bool followLocation = true);
    bool followLocation() const;

    /**
     * @brief 设置自定义 HTTP Header
     * @param headerName Header 名称（如 "User-Agent"）
     * @param headerValue Header 值
     * @note 相同 Header 会被覆盖
     * @return 返回 *this 以支持方法链
     */
    QCNetworkRequest& setRawHeader(const QByteArray &headerName, const QByteArray &headerValue);
    QList<QByteArray> rawHeaderList() const;
    QByteArray rawHeader(const QByteArray &headerName) const;

    /**
     * @brief 设置 HTTP Range 请求（部分下载）
     * @param start 起始字节位置
     * @param end 结束字节位置
     * @return 返回 *this 以支持方法链
     */
    QCNetworkRequest& setRange(int start, int end);
    int rangeStart() const;
    int rangeEnd() const;

    // ========== 高级配置 ==========

    /**
     * @brief 设置 SSL/TLS 配置
     * @param config SSL 配置对象
     * @return 返回 *this 以支持方法链
     */
    QCNetworkRequest& setSslConfig(const QCNetworkSslConfig &config);
    [[nodiscard]] QCNetworkSslConfig sslConfig() const;

    /**
     * @brief 设置代理配置
     * @param config 代理配置对象
     * @return 返回 *this 以支持方法链
     */
    QCNetworkRequest& setProxyConfig(const QCNetworkProxyConfig &config);
    [[nodiscard]] std::optional<QCNetworkProxyConfig> proxyConfig() const;

    /**
     * @brief 设置超时配置
     * @param config 超时配置对象
     * @return 返回 *this 以支持方法链
     */
    QCNetworkRequest& setTimeoutConfig(const QCNetworkTimeoutConfig &config);
    [[nodiscard]] QCNetworkTimeoutConfig timeoutConfig() const;

    /**
     * @brief 设置 HTTP 协议版本
     * @param version HTTP 版本枚举（Http1_0/Http1_1/Http2/Http3）
     * @return 返回 *this 以支持方法链
     */
    QCNetworkRequest& setHttpVersion(QCNetworkHttpVersion version);
    [[nodiscard]] QCNetworkHttpVersion httpVersion() const;

    /**
     * @brief 设置请求重试策略
     * @param policy 重试策略对象
     * @return 返回 *this 以支持方法链
     */
    QCNetworkRequest& setRetryPolicy(const QCNetworkRetryPolicy &policy);

    /**
     * @brief 获取当前的重试策略
     * @return 重试策略对象
     */
    [[nodiscard]] QCNetworkRetryPolicy retryPolicy() const;

    // ========== 便捷方法 ==========

    /**
     * @brief 设置总超时时间（便捷方法）
     * @param timeout 超时时长
     * @return 返回 *this 以支持方法链
     */
    QCNetworkRequest& setTimeout(std::chrono::milliseconds timeout);

    /**
     * @brief 设置连接超时时间（便捷方法）
     * @param timeout 连接超时时长
     * @return 返回 *this 以支持方法链
     */
    QCNetworkRequest& setConnectTimeout(std::chrono::milliseconds timeout);

    // ========== 请求优先级 ==========

    /**
     * @brief 设置请求优先级
     * 
     * 用于请求调度器（QCNetworkRequestScheduler）按优先级排序和执行请求。
     * 
     * @param priority 请求优先级（VeryLow/Low/Normal/High/VeryHigh/Critical）
     * @return 返回 *this 以支持方法链
     * 
     * @code
     * QCNetworkRequest request(url);
     * request.setPriority(QCNetworkRequestPriority::High);
     * @endcode
     */
    QCNetworkRequest& setPriority(QCNetworkRequestPriority priority);

    /**
     * @brief 获取请求优先级
     *
     * @return 当前的请求优先级（默认：Normal）
     */
    [[nodiscard]] QCNetworkRequestPriority priority() const;

    // ========== 缓存策略 ==========

    /**
     * @brief 设置缓存策略
     *
     * 控制请求如何使用缓存数据。
     *
     * @param policy 缓存策略（AlwaysCache/PreferCache/PreferNetwork/OnlyNetwork/OnlyCache）
     * @return 返回 *this 以支持方法链
     *
     * @code
     * QCNetworkRequest request(url);
     * request.setCachePolicy(QCNetworkCachePolicy::PreferCache);
     * @endcode
     */
    QCNetworkRequest& setCachePolicy(QCNetworkCachePolicy policy);

    /**
     * @brief 获取缓存策略
     *
     * @return 当前的缓存策略（默认：PreferCache）
     */
    [[nodiscard]] QCNetworkCachePolicy cachePolicy() const;

private:
    QSharedDataPointer<QCurl::QCNetworkRequestPrivate> d;
};

QDebug operator <<(QDebug dbg, const QCNetworkRequest &req);

} //namespace QCurl
#endif // QCNETWORKREQUEST_H
