#ifndef QCREQUEST_H
#define QCREQUEST_H

#include "QCNetworkRequest.h"
#include "QCNetworkAccessManager.h"
#include "QCNetworkReply.h"
#include <QString>
#include <QUrl>
#include <QJsonObject>
#include <chrono>

namespace QCurl {

/**
 * @brief 流式链式 API 风格的 HTTP 请求构建器
 *
 * QCRequest 提供简洁的流式接口,用于快速构建和发送 HTTP 请求。
 * 它内部封装了 QCNetworkRequest,支持方法链式调用。
 *
 * @par 核心特性
 * - 静态工厂方法创建不同 HTTP 方法的请求
 * - 流式接口配置请求参数
 * - 自动管理默认 manager 或使用自定义 manager
 * - 支持回调函数处理响应
 *
 * @par 使用示例 - 基本 GET 请求
 * @code
 * auto *reply = QCRequest::get("https://api.example.com/data")
 *     .withHeader("Authorization", "Bearer token")
 *     .withTimeout(std::chrono::seconds(30))
 *     .send();
 *
 * connect(reply, &QCNetworkReply::finished, [reply]() {
 *     qDebug() << "Response:" << reply->readAll().value();
 *     reply->deleteLater();
 * });
 * @endcode
 *
 * @par 使用示例 - POST JSON 数据
 * @code
 * QJsonObject json;
 * json["name"] = "Alice";
 * json["age"] = 30;
 *
 * QCRequest::post("https://api.example.com/users")
 *     .withJson(json)
 *     .withHeader("Content-Type", "application/json")
 *     .send([](QCNetworkReply *reply) {
 *         qDebug() << "Created user, status:" << reply->httpStatusCode();
 *     });
 * @endcode
 *
 */
class QCRequest
{
public:
    /**
     * @brief 创建 GET 请求
     * @param url 请求 URL(字符串或 QUrl)
     * @return QCRequest 对象,支持方法链
     */
    static QCRequest get(const QString &url);
    static QCRequest get(const QUrl &url);

    /**
     * @brief 创建 POST 请求
     * @param url 请求 URL(字符串或 QUrl)
     * @return QCRequest 对象,支持方法链
     */
    static QCRequest post(const QString &url);
    static QCRequest post(const QUrl &url);

    /**
     * @brief 创建 PUT 请求
     * @param url 请求 URL(字符串或 QUrl)
     * @return QCRequest 对象,支持方法链
     */
    static QCRequest put(const QString &url);
    static QCRequest put(const QUrl &url);

    /**
     * @brief 创建 DELETE 请求
     * @param url 请求 URL(字符串或 QUrl)
     * @return QCRequest 对象,支持方法链
     */
    static QCRequest del(const QString &url);
    static QCRequest del(const QUrl &url);

    /**
     * @brief 创建 PATCH 请求
     * @param url 请求 URL(字符串或 QUrl)
     * @return QCRequest 对象,支持方法链
     */
    static QCRequest patch(const QString &url);
    static QCRequest patch(const QUrl &url);

    /**
     * @brief 创建 HEAD 请求
     * @param url 请求 URL(字符串或 QUrl)
     * @return QCRequest 对象,支持方法链
     */
    static QCRequest head(const QString &url);
    static QCRequest head(const QUrl &url);

    // ========== 流式配置方法 ==========

    /**
     * @brief 添加 HTTP Header
     * @param key Header 名称(如 "Authorization")
     * @param value Header 值
     * @return 返回 *this 以支持方法链
     */
    QCRequest& withHeader(const QString &key, const QString &value);

    /**
     * @brief 添加 URL 查询参数
     * @param key 参数名
     * @param value 参数值
     * @return 返回 *this 以支持方法链
     */
    QCRequest& withQueryParam(const QString &key, const QString &value);

    /**
     * @brief 设置超时时间
     * @param timeout 超时时长(使用 std::chrono)
     * @return 返回 *this 以支持方法链
     *
     * @code
     * request.withTimeout(std::chrono::seconds(30));
     * request.withTimeout(std::chrono::milliseconds(5000));
     * @endcode
     */
    QCRequest& withTimeout(std::chrono::seconds timeout);
    QCRequest& withTimeout(std::chrono::milliseconds timeout);

    /**
     * @brief 设置请求体为 JSON 对象
     * @param json QJsonObject 对象
     * @return 返回 *this 以支持方法链
     * @note 自动设置 Content-Type: application/json
     */
    QCRequest& withJson(const QJsonObject &json);

    /**
     * @brief 设置请求体为原始二进制数据
     * @param data 请求体数据
     * @param contentType 可选的 Content-Type(默认 application/octet-stream)
     * @return 返回 *this 以支持方法链
     */
    QCRequest& withBody(const QByteArray &data, const QString &contentType = QString());

    /**
     * @brief 设置 SSL/TLS 配置
     * @param config SSL 配置对象
     * @return 返回 *this 以支持方法链
     */
    QCRequest& withSslConfig(const QCNetworkSslConfig &config);

    /**
     * @brief 设置代理配置
     * @param config 代理配置对象
     * @return 返回 *this 以支持方法链
     */
    QCRequest& withProxyConfig(const QCNetworkProxyConfig &config);

    /**
     * @brief 设置 HTTP 版本
     * @param version HTTP 版本枚举(Http1_1/Http2/Http3)
     * @return 返回 *this 以支持方法链
     */
    QCRequest& withHttpVersion(QCNetworkHttpVersion version);

    /**
     * @brief 设置请求优先级
     * @param priority 优先级枚举(VeryLow/Low/Normal/High/VeryHigh/Critical)
     * @return 返回 *this 以支持方法链
     */
    QCRequest& withPriority(QCNetworkRequestPriority priority);

    /**
     * @brief 设置重试策略
     * @param policy 重试策略对象
     * @return 返回 *this 以支持方法链
     */
    QCRequest& withRetryPolicy(const QCNetworkRetryPolicy &policy);

    /**
     * @brief 设置是否跟随 HTTP 重定向
     * @param follow true 为跟随(默认),false 为不跟随
     * @return 返回 *this 以支持方法链
     */
    QCRequest& withFollowRedirects(bool follow = true);

    // ========== 发送请求方法 ==========

    /**
     * @brief 发送请求(使用默认全局 manager)
     * @param manager 可选的自定义 QCNetworkAccessManager(默认使用全局实例)
     * @return QCNetworkReply 指针,调用者负责 deleteLater()
     *
     * @code
     * auto *reply = QCRequest::get(url).send();
     * connect(reply, &QCNetworkReply::finished, ...);
     * @endcode
     */
    QCNetworkReply* send(QCNetworkAccessManager *manager = nullptr);

    /**
     * @brief 发送请求并在完成时调用回调函数
     * @param callback 完成时的回调函数,参数为 QCNetworkReply*
     * @return QCNetworkReply 指针,调用者负责 deleteLater()
     *
     * @code
     * QCRequest::get(url).send([](QCNetworkReply *reply) {
     *     qDebug() << reply->readAll().value();
     *     reply->deleteLater();
     * });
     * @endcode
     */
    QCNetworkReply* send(std::function<void(QCNetworkReply*)> callback);

private:
    /**
     * @brief 私有构造函数,通过静态工厂方法创建
     * @param url 请求 URL
     * @param method HTTP 方法字符串("GET"/"POST"/...)
     */
    explicit QCRequest(const QUrl &url, const QString &method);

    /**
     * @brief 获取或创建全局默认的 QCNetworkAccessManager
     * @return 全局 manager 指针
     */
    static QCNetworkAccessManager* defaultManager();

    /**
     * @brief 内部发送请求的实现
     * @param manager 使用的 manager
     * @return QCNetworkReply 指针
     */
    QCNetworkReply* sendInternal(QCNetworkAccessManager *manager);

private:
    QCNetworkRequest m_request;   ///< 内部的 QCNetworkRequest 对象
    QString m_method;             ///< HTTP 方法("GET", "POST", ...)
    QByteArray m_postData;        ///< POST/PUT/PATCH/DELETE 的请求体数据
};

} // namespace QCurl

#endif // QCREQUEST_H
