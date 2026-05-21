// SPDX-License-Identifier: MIT
// Copyright (c) 2025 QCurl Project

/**
 * @file
 * @brief 声明可选安装的通用中间件 Extras。
 */

#ifndef QCNETWORKMIDDLEWAREEXTRAS_H
#define QCNETWORKMIDDLEWAREEXTRAS_H

#include "QCNetworkMiddleware.h"

namespace QCurl {

/**
 * @brief 脱敏日志中间件（默认安全）
 *
 * 输出 request/response 摘要，不输出 body 明文；URL/query/header 做脱敏处理。
 */
class QCURL_EXPORT QCRedactingLoggingMiddleware : public QCNetworkMiddleware
{
public:
    /// 在 reply 创建后挂接脱敏日志观察点。
    void onReplyCreated(QCNetworkReply *reply) override;
    /// 在响应完成后输出脱敏摘要。
    void onResponseReceived(QCNetworkReply *reply) override;
    /// 返回中间件标识名。
    QString name() const override { return QStringLiteral("QCRedactingLoggingMiddleware"); }
};

/**
 * @brief 观测埋点中间件。
 *
 * 通过 logger 输出可用于排障与估价的关键指标。
 */
class QCURL_EXPORT QCObservabilityMiddleware : public QCNetworkMiddleware
{
public:
    /// 在 reply 创建后绑定观测埋点。
    void onReplyCreated(QCNetworkReply *reply) override;
    /// 在响应完成后输出关键观测指标。
    void onResponseReceived(QCNetworkReply *reply) override;
    /// 返回中间件标识名。
    QString name() const override { return QStringLiteral("QCObservabilityMiddleware"); }
};

} // namespace QCurl

#endif // QCNETWORKMIDDLEWAREEXTRAS_H
