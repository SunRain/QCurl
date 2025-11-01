#ifndef QCREQUESTBUILDER_H
#define QCREQUESTBUILDER_H

#include "QCNetworkRequest.h"
#include "QCNetworkSslConfig.h"
#include "QCNetworkProxyConfig.h"
#include "QCNetworkHttpVersion.h"
#include "QCNetworkRequestPriority.h"
#include "QCNetworkRetryPolicy.h"
#include <QString>
#include <QUrl>
#include <QMap>
#include <QJsonObject>

namespace QCurl {

/**
 * @brief 传统构建器模式的 HTTP 请求构建器
 *
 * QCRequestBuilder 提供传统的构建器模式 API.
 * 通过一系列配置方法,最后调用 build() 生成 QCNetworkRequest.
 *
 * 核心特性:
 * - 传统构建器模式,每个配置方法返回引用
 * - 支持所有 HTTP 方法(GET/POST/PUT/DELETE/PATCH/HEAD)
 * - 独立的查询参数和请求头管理
 * - 最后通过 build() 生成最终的 QCNetworkRequest
 *
 * 使用示例 - 基本用法:
 * QCRequestBuilder builder;
 * auto request = builder
 *     .setUrl("https://api.example.com/users")
 *     .setMethod(QCRequestBuilder::GET)
 *     .addHeader("Authorization", "Bearer token")
 *     .addQueryParam("page", "1")
 *     .setTimeout(30)
 *     .build();
 *
 * auto *reply = manager->sendGet(request);
 *
 * 使用示例 - POST JSON:
 * QJsonObject json;
 * json["name"] = "Alice";
 * json["email"] = "alice@example.com";
 *
 * QCRequestBuilder builder;
 * auto request = builder
 *     .setUrl("https://api.example.com/users")
 *     .setMethod(QCRequestBuilder::POST)
 *     .setJsonBody(json)
 *     .setTimeout(30)
 *     .build();
 *
 */
class QCRequestBuilder
{
public:
    /**
     * @brief HTTP 方法枚举
     */
    enum Method {
        GET,     ///< HTTP GET 方法
        POST,    ///< HTTP POST 方法
        PUT,     ///< HTTP PUT 方法
        DELETE,  ///< HTTP DELETE 方法
        HEAD,    ///< HTTP HEAD 方法
        PATCH    ///< HTTP PATCH 方法
    };

    /**
     * @brief 默认构造函数
     *
     * 创建一个空的构建器,需要手动设置 URL 和方法
     */
    QCRequestBuilder();

    // ========== 基本配置方法 ==========

    /**
     * @brief 设置请求 URL
     * @param url URL 字符串
     * @return 返回 *this 以支持方法链
     */
    QCRequestBuilder& setUrl(const QString &url);

    /**
     * @brief 设置请求 URL
     * @param url QUrl 对象
     * @return 返回 *this 以支持方法链
     */
    QCRequestBuilder& setUrl(const QUrl &url);

    /**
     * @brief 设置 HTTP 方法
     * @param method HTTP 方法枚举(GET/POST/PUT/DELETE/HEAD/PATCH)
     * @return 返回 *this 以支持方法链
     */
    QCRequestBuilder& setMethod(Method method);

    // ========== Header 和查询参数 ==========

    /**
     * @brief 添加 HTTP Header
     * @param key Header 名称
     * @param value Header 值
     * @return 返回 *this 以支持方法链
     *
     * @note 如果已存在同名 Header,会被覆盖
     */
    QCRequestBuilder& addHeader(const QString &key, const QString &value);

    /**
     * @brief 添加 URL 查询参数
     * @param key 参数名
     * @param value 参数值
     * @return 返回 *this 以支持方法链
     *
     * @note 支持添加多个同名参数
     */
    QCRequestBuilder& addQueryParam(const QString &key, const QString &value);

    // ========== 超时配置 ==========

    /**
     * @brief 设置超时时间(秒)
     * @param seconds 超时秒数
     * @return 返回 *this 以支持方法链
     */
    QCRequestBuilder& setTimeout(int seconds);

    /**
     * @brief 设置超时时间(毫秒)
     * @param milliseconds 超时毫秒数
     * @return 返回 *this 以支持方法链
     */
    QCRequestBuilder& setTimeoutMs(int milliseconds);

    // ========== 请求体配置 ==========

    /**
     * @brief 设置原始请求体数据
     * @param data 请求体数据
     * @return 返回 *this 以支持方法链
     *
     * @note 适用于 POST/PUT/PATCH 方法
     */
    QCRequestBuilder& setBody(const QByteArray &data);

    /**
     * @brief 设置 JSON 请求体
     * @param json QJsonObject 对象
     * @return 返回 *this 以支持方法链
     *
     * @note 自动设置 Content-Type: application/json
     * @note 自动序列化 JSON 对象
     */
    QCRequestBuilder& setJsonBody(const QJsonObject &json);

    /**
     * @brief 设置 Content-Type header
     * @param contentType MIME 类型(如 "application/json", "text/xml")
     * @return 返回 *this 以支持方法链
     */
    QCRequestBuilder& setContentType(const QString &contentType);

    // ========== 高级配置 ==========

    /**
     * @brief 设置 SSL/TLS 配置
     * @param config SSL 配置对象
     * @return 返回 *this 以支持方法链
     */
    QCRequestBuilder& setSslConfig(const QCNetworkSslConfig &config);

    /**
     * @brief 设置代理配置
     * @param config 代理配置对象
     * @return 返回 *this 以支持方法链
     */
    QCRequestBuilder& setProxyConfig(const QCNetworkProxyConfig &config);

    /**
     * @brief 设置 HTTP 版本
     * @param version HTTP 版本枚举(Http1_1/Http2/Http3)
     * @return 返回 *this 以支持方法链
     */
    QCRequestBuilder& setHttpVersion(QCNetworkHttpVersion version);

    /**
     * @brief 设置请求优先级
     * @param priority 优先级枚举(VeryLow/Low/Normal/High/VeryHigh/Critical)
     * @return 返回 *this 以支持方法链
     */
    QCRequestBuilder& setPriority(QCNetworkRequestPriority priority);

    /**
     * @brief 设置重试策略
     * @param policy 重试策略对象
     * @return 返回 *this 以支持方法链
     */
    QCRequestBuilder& setRetryPolicy(const QCNetworkRetryPolicy &policy);

    /**
     * @brief 设置是否跟随 HTTP 重定向
     * @param follow true 为跟随(默认),false 为不跟随
     * @return 返回 *this 以支持方法链
     */
    QCRequestBuilder& setFollowRedirects(bool follow);

    // ========== 构建方法 ==========

    /**
     * @brief 构建最终的 QCNetworkRequest 对象
     * @return QCNetworkRequest 对象,可直接传给 QCNetworkAccessManager
     *
     * @note 此方法会验证必要的配置(URL 和 Method)
     * @note 可以多次调用 build() 生成不同的请求对象
     *
     * @code
     * QCRequestBuilder builder;
     * builder.setUrl("https://example.com").setMethod(QCRequestBuilder::GET);
     * auto request1 = builder.build();
     * auto request2 = builder.addQueryParam("page", "2").build();  // 复用构建器
     * @endcode
     */
    QCNetworkRequest build() const;

    /**
     * @brief 重置构建器到初始状态
     *
     * 清除所有配置,可重新使用构建器
     */
    void reset();

private:
    QUrl m_url;                                  ///< 请求 URL
    Method m_method;                             ///< HTTP 方法
    QMap<QString, QString> m_headers;            ///< HTTP Headers(键值对)
    QMap<QString, QString> m_queryParams;        ///< URL 查询参数(键值对)
    int m_timeoutMs;                             ///< 超时时间(毫秒)
    QByteArray m_body;                           ///< 请求体数据
    bool m_followRedirects;                      ///< 是否跟随重定向

    // 高级配置(可选,使用值类型 + 标志位模式)
    QCNetworkSslConfig m_sslConfig;              ///< SSL 配置
    bool m_hasSslConfig;                         ///< 是否设置了 SSL 配置

    QCNetworkProxyConfig m_proxyConfig;          ///< 代理配置
    bool m_hasProxyConfig;                       ///< 是否设置了代理配置

    QCNetworkHttpVersion m_httpVersion;          ///< HTTP 版本
    bool m_hasHttpVersion;                       ///< 是否设置了 HTTP 版本

    QCNetworkRequestPriority m_priority;         ///< 请求优先级
    bool m_hasPriority;                          ///< 是否设置了优先级

    QCNetworkRetryPolicy m_retryPolicy;          ///< 重试策略
    bool m_hasRetryPolicy;                       ///< 是否设置了重试策略
};

} // namespace QCurl

#endif // QCREQUESTBUILDER_H
