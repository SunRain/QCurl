// SPDX-License-Identifier: MIT
// Copyright (c) 2025 QCurl Project

/**
 * @file
 * @brief 声明请求/响应中间件接口。
 */

#ifndef QCNETWORKMIDDLEWARE_H
#define QCNETWORKMIDDLEWARE_H

#include "QCGlobal.h"

#include <QScopedPointer>
#include <QString>

namespace QCurl {

class QCNetworkAccessManager;
class QCNetworkMiddlewarePrivate;
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
 * AuthMiddleware auth;
 * manager->addMiddleware(&auth); // manager 不持有 middleware 所有权
 * @endcode
 */
class QCURL_EXPORT QCNetworkMiddleware
{
public:
    /// 构造中间件基类。
    QCNetworkMiddleware();
    /// 通过多态接口释放中间件对象。
    virtual ~QCNetworkMiddleware();

    Q_DISABLE_COPY_MOVE(QCNetworkMiddleware)

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
    virtual QString name() const { return QStringLiteral("QCNetworkMiddleware"); }

private:
    void registerManager(QCNetworkAccessManager *manager);
    void unregisterManager(QCNetworkAccessManager *manager);

    QScopedPointer<QCNetworkMiddlewarePrivate> d_ptr;

    friend class QCNetworkAccessManager;
};

} // namespace QCurl

#endif // QCNETWORKMIDDLEWARE_H
