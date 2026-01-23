// SPDX-License-Identifier: MIT
// Copyright (c) 2025 QCurl Project

#ifndef QCNETWORKMIDDLEWARE_H
#define QCNETWORKMIDDLEWARE_H

#include "QCGlobal.h"
#include "QCNetworkRetryPolicy.h"

#include <QString>

#include <memory>

namespace QCurl {

class QCNetworkRequest;
class QCNetworkReply;

/**
 * @brief 网络请求/响应中间件接口
 *
 * 中间件可用于：
 * - 请求前拦截：添加通用Header、签名、日志等
 * - 响应后拦截：统一错误处理、数据转换、日志等
 * - 链式执行：支持多个中间件顺序执行
 *
 *
 * @example
 * @code
 * class AuthMiddleware : public QCNetworkMiddleware {
 *     void onRequestPreSend(QCNetworkRequest &request) override {
 *         request.setRawHeader("Authorization", generateToken());
 *     }
 * };
 *
 * manager->addMiddleware(new AuthMiddleware());
 * @endcode
 */
class QCURL_EXPORT QCNetworkMiddleware
{
public:
    virtual ~QCNetworkMiddleware() = default;

    /**
     * @brief 请求发送前拦截
     * @param request 网络请求对象
     *
     * 在请求发送前调用，可以修改请求参数。
     */
    virtual void onRequestPreSend(QCNetworkRequest &request) { Q_UNUSED(request); }

    /**
     * @brief Reply 创建后拦截
     * @param reply 网络响应对象
     *
     * 在 QCNetworkReply 构造完成后、执行前调用。
     * 适合用于挂接观测/日志（例如订阅 retryAttempt/finished 等信号）。
     */
    virtual void onReplyCreated(QCNetworkReply *reply) { Q_UNUSED(reply); }

    /**
     * @brief 响应接收后拦截
     * @param reply 网络响应对象
     *
     * 在响应接收后调用，可以读取和处理响应数据。
     */
    virtual void onResponseReceived(QCNetworkReply *reply) { Q_UNUSED(reply); }

    /**
     * @brief 中间件名称
     * @return 中间件的标识名称
     */
    virtual QString name() const { return "QCNetworkMiddleware"; }
};

/**
 * @brief 日志记录中间件
 *
 * 自动记录所有请求和响应的信息。
 *
 */
class QCURL_EXPORT QCLoggingMiddleware : public QCNetworkMiddleware
{
public:
    void onRequestPreSend(QCNetworkRequest &request) override;
    void onResponseReceived(QCNetworkReply *reply) override;
    QString name() const override { return "QCLoggingMiddleware"; }
};

/**
 * @brief 错误处理中间件
 *
 * 统一处理网络错误和 HTTP 错误。
 *
 */
class QCURL_EXPORT QCErrorHandlingMiddleware : public QCNetworkMiddleware
{
public:
    /**
     * @brief 设置错误回调函数
     * @param callback 错误回调，接收错误消息
     */
    void setErrorCallback(std::function<void(const QString &)> callback);

    void onResponseReceived(QCNetworkReply *reply) override;
    QString name() const override { return "QCErrorHandlingMiddleware"; }

private:
    std::function<void(const QString &)> m_errorCallback;
};

/**
 * @brief 请求签名中间件（示例）
 *
 * 为请求自动添加签名信息。
 *
 */
class QCURL_EXPORT QCSigningMiddleware : public QCNetworkMiddleware
{
public:
    /**
     * @brief 设置签名密钥
     */
    void setSigningKey(const QString &key);

    void onRequestPreSend(QCNetworkRequest &request) override;
    QString name() const override { return "QCSigningMiddleware"; }

private:
    QString m_signingKey;
};

/**
 * @brief 统一重试策略中间件
 *
 * - 仅在 request 未显式设置 retryPolicy 时注入默认策略（显式优先）。
 * - request 显式 setRetryPolicy(noRetry()) 视为“显式禁用”，不会被覆盖。
 */
class QCURL_EXPORT QCUnifiedRetryPolicyMiddleware : public QCNetworkMiddleware
{
public:
    explicit QCUnifiedRetryPolicyMiddleware(const QCNetworkRetryPolicy &defaultPolicy);

    void setDefaultPolicy(const QCNetworkRetryPolicy &policy);
    [[nodiscard]] QCNetworkRetryPolicy defaultPolicy() const;

    void onRequestPreSend(QCNetworkRequest &request) override;
    QString name() const override { return "QCUnifiedRetryPolicyMiddleware"; }

private:
    QCNetworkRetryPolicy m_defaultPolicy;
};

/**
 * @brief 脱敏日志中间件（默认安全）
 *
 * - 输出 request/response 摘要（不输出 body 明文）。
 * - URL/query/header 做脱敏处理（仅输出 [REDACTED]）。
 */
class QCURL_EXPORT QCRedactingLoggingMiddleware : public QCNetworkMiddleware
{
public:
    void onReplyCreated(QCNetworkReply *reply) override;
    void onResponseReceived(QCNetworkReply *reply) override;
    QString name() const override { return "QCRedactingLoggingMiddleware"; }
};

/**
 * @brief 观测埋点中间件（MVP）
 *
 * 通过 logger 输出可用于排障与估价的关键指标（离线可断言）。
 */
class QCURL_EXPORT QCObservabilityMiddleware : public QCNetworkMiddleware
{
public:
    void onReplyCreated(QCNetworkReply *reply) override;
    void onResponseReceived(QCNetworkReply *reply) override;
    QString name() const override { return "QCObservabilityMiddleware"; }
};

} // namespace QCurl

#endif // QCNETWORKMIDDLEWARE_H
