// SPDX-License-Identifier: MIT
// Copyright (c) 2025 QCurl Project

#ifndef QCNETWORKREQUESTBUILDER_H
#define QCNETWORKREQUESTBUILDER_H

#include <QUrl>
#include <QString>
#include <QPair>
#include <functional>
#include <memory>
#include <optional>
#include "QCGlobal.h"

class QIODevice;

namespace QCurl {

class QCNetworkAccessManager;
class QCNetworkRequest;
class QCNetworkReply;
struct QCNetworkHttpAuthConfig;

/**
 * @brief 流式请求构建器
 * 
 * 提供链式 API 来简化网络请求的构建和发送。
 * 支持 GET、POST、HEAD、DELETE 等 HTTP 方法。
 * 
 * 
 * @example
 * @code
 * auto *reply = manager->newRequest(url)
 *     .withFollowLocation(true)
 *     .withHeader("Authorization", "Bearer token")
 *     .withTimeout(30000)
 *     .sendGet();
 * @endcode
 */
class QCURL_EXPORT QCNetworkRequestBuilder
{
public:
    /**
     * @brief 构造函数
     * @param manager 网络访问管理器
     * @param url 请求 URL
     */
    QCNetworkRequestBuilder(QCNetworkAccessManager *manager, const QUrl &url);

    QCNetworkRequestBuilder(const QCNetworkRequestBuilder &) = delete;
    QCNetworkRequestBuilder &operator=(const QCNetworkRequestBuilder &) = delete;
    QCNetworkRequestBuilder(QCNetworkRequestBuilder &&) noexcept;
    QCNetworkRequestBuilder &operator=(QCNetworkRequestBuilder &&) noexcept;
    
    /**
     * @brief 析构函数
     */
    ~QCNetworkRequestBuilder();
    
    /**
     * @brief 添加自定义 Header
     * @param name Header 名称
     * @param value Header 值
     */
    QCNetworkRequestBuilder &withHeader(const QString &name, const QString &value);

    /**
     * @brief 设置 HTTP Basic 认证（请求级，libcurl 生成 Authorization 头）
     * @note Basic 仅建议在 HTTPS 下使用
     */
    QCNetworkRequestBuilder &withBasicAuth(const QString &userName, const QString &password);

    /**
     * @brief 设置请求级 HTTP 认证（用户名/密码 + 认证策略）
     * @note 若同时显式设置了 `Authorization` header，则以 header 为准
     */
    QCNetworkRequestBuilder &withHttpAuth(const QCNetworkHttpAuthConfig &config);
    
    /**
     * @brief 设置 Content-Type
     * @param contentType Content-Type 值
     */
    QCNetworkRequestBuilder &withContentType(const QString &contentType);
    
    /**
     * @brief 设置 User-Agent
     * @param userAgent User-Agent 字符串
     */
    QCNetworkRequestBuilder &withUserAgent(const QString &userAgent);
    
    /**
     * @brief 设置请求超时时间
     * @param msecs 超时时间（毫秒）
     */
    QCNetworkRequestBuilder &withTimeout(int msecs);
    
    /**
     * @brief 设置是否跟随重定向
     * @param follow 是否跟随
     */
    QCNetworkRequestBuilder &withFollowLocation(bool follow);
    
    /**
     * @brief 设置代理服务器
     * @param proxyUrl 代理 URL
     */
    QCNetworkRequestBuilder &withProxy(const QUrl &proxyUrl);
    
    /**
     * @brief 设置 SSL 证书验证
     * @param verify 是否验证
     */
    QCNetworkRequestBuilder &withSSLVerify(bool verify);
    
    /**
     * @brief 设置自定义 CA 证书路径
     * @param certPath 证书文件路径
     */
    QCNetworkRequestBuilder &withCACert(const QString &certPath);
    
    /**
     * @brief 添加 URL 查询参数
     * @param name 参数名
     * @param value 参数值
     */
    QCNetworkRequestBuilder &withQueryParam(const QString &name, const QString &value);
    
    /**
     * @brief 设置请求体
     * @param body 请求体数据
     */
    QCNetworkRequestBuilder &withBody(const QByteArray &body);

    /**
     * @brief 设置流式上传的请求体来源（QIODevice，仅 PUT/POST）
     *
     * 说明：
     * - 所有权：device 由调用方管理，QCurl 不会 close/delete
     * - 生命周期：device 必须在请求完成前保持有效且可读
     */
    QCNetworkRequestBuilder &withUploadDevice(QIODevice *device, std::optional<qint64> sizeBytes = std::nullopt);

    /**
     * @brief 设置流式上传的请求体来源（本地文件路径，仅 PUT/POST）
     */
    QCNetworkRequestBuilder &withUploadFile(const QString &filePath, std::optional<qint64> sizeBytes = std::nullopt);
    
    /**
     * @brief 发送 GET 请求
     * @return 网络响应对象
     */
    QCNetworkReply *sendGet();
    
    /**
     * @brief 发送 POST 请求
     * @param body 请求体数据
     * @return 网络响应对象
     */
    QCNetworkReply *sendPost(const QByteArray &body = QByteArray());
    
    /**
     * @brief 发送 HEAD 请求
     * @return 网络响应对象
     */
    QCNetworkReply *sendHead();
    
    /**
     * @brief 发送 DELETE 请求
     * @param body 请求体数据（可选）。若为空且已通过 withBody 设置过 body，则使用 withBody 中的值。
     * @return 网络响应对象
     */
    QCNetworkReply *sendDelete(const QByteArray &body = QByteArray());
    
    /**
     * @brief 发送 PUT 请求
     * @param body 请求体数据
     * @return 网络响应对象
     */
    QCNetworkReply *sendPut(const QByteArray &body = QByteArray());

private:
    class Private;
    std::unique_ptr<Private> d_ptr;
};

} // namespace QCurl

#endif // QCNETWORKREQUESTBUILDER_H
