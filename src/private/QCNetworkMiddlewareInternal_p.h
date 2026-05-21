// SPDX-License-Identifier: MIT
// Copyright (c) 2025 QCurl Project

/**
 * @file
 * @brief 声明项目内部使用的策略型中间件。
 */

#ifndef QCNETWORKMIDDLEWAREINTERNAL_P_H
#define QCNETWORKMIDDLEWAREINTERNAL_P_H

#include "QCNetworkMiddleware.h"
#include "QCNetworkRetryPolicy.h"

#include <functional>

namespace QCurl {

/**
 * @brief 内部日志中间件。
 */
class QCLoggingMiddleware : public QCNetworkMiddleware
{
public:
    /// 在发送前记录请求摘要。
    void onRequestPreSend(QCNetworkRequest &request) override;
    /// 在接收完成后记录响应摘要。
    void onResponseReceived(QCNetworkReply *reply) override;
    /// 返回中间件标识名。
    QString name() const override { return QStringLiteral("QCLoggingMiddleware"); }
};

/**
 * @brief 内部错误处理中间件。
 */
class QCErrorHandlingMiddleware : public QCNetworkMiddleware
{
public:
    /// 设置错误回调函数。
    void setErrorCallback(std::function<void(const QString &)> callback);

    /// 在响应后统一处理错误和 HTTP 失败。
    void onResponseReceived(QCNetworkReply *reply) override;
    /// 返回中间件标识名。
    QString name() const override { return QStringLiteral("QCErrorHandlingMiddleware"); }

private:
    std::function<void(const QString &)> m_errorCallback;
};

/**
 * @brief 内部请求签名中间件示例。
 */
class QCSigningMiddleware : public QCNetworkMiddleware
{
public:
    /// 设置签名密钥。
    void setSigningKey(const QString &key);

    /// 在发送前把签名信息写入请求。
    void onRequestPreSend(QCNetworkRequest &request) override;
    /// 返回中间件标识名。
    QString name() const override { return QStringLiteral("QCSigningMiddleware"); }

private:
    QString m_signingKey;
};

/**
 * @brief 内部统一重试策略中间件。
 *
 * 仅在 request 未显式设置 retryPolicy 时注入默认策略。
 */
class QCUnifiedRetryPolicyMiddleware : public QCNetworkMiddleware
{
public:
    /// 构造默认重试策略中间件。
    explicit QCUnifiedRetryPolicyMiddleware(const QCNetworkRetryPolicy &defaultPolicy);

    /// 更新默认重试策略。
    void setDefaultPolicy(const QCNetworkRetryPolicy &policy);
    /// 返回当前默认重试策略。
    [[nodiscard]] QCNetworkRetryPolicy defaultPolicy() const;

    /// 在请求未显式配置时注入默认重试策略。
    void onRequestPreSend(QCNetworkRequest &request) override;
    /// 返回中间件标识名。
    QString name() const override { return QStringLiteral("QCUnifiedRetryPolicyMiddleware"); }

private:
    QCNetworkRetryPolicy m_defaultPolicy;
};

} // namespace QCurl

#endif // QCNETWORKMIDDLEWAREINTERNAL_P_H
