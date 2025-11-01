// SPDX-License-Identifier: MIT
// Copyright (c) 2025 QCurl Project

#ifndef QCNETWORKCANCELTOKEN_H
#define QCNETWORKCANCELTOKEN_H

#include <QObject>
#include <QList>
#include <QTimer>
#include <memory>
#include "QCGlobal.h"

namespace QCurl {

class QCNetworkReply;

/**
 * @brief 网络请求取消令牌
 * 
 * 用于管理和取消多个网络请求，支持以下功能：
 * - 一键取消多个请求
 * - 自动超时取消
 * - 批量请求管理
 * 
 * 
 * @example
 * @code
 * auto token = new QCNetworkCancelToken();
 * 
 * // 附加请求到令牌
 * auto *reply1 = manager->sendGet(request1);
 * auto *reply2 = manager->sendGet(request2);
 * token->attach(reply1);
 * token->attach(reply2);
 * 
 * // 一键取消所有请求
 * token->cancel();
 * 
 * // 或设置自动超时取消（30秒后自动取消）
 * token->setAutoTimeout(30000);
 * @endcode
 */
class QCURL_EXPORT QCNetworkCancelToken : public QObject
{
    Q_OBJECT

public:
    /**
     * @brief 构造函数
     * @param parent 父对象
     */
    explicit QCNetworkCancelToken(QObject *parent = nullptr);
    
    /**
     * @brief 析构函数
     * 
     * 析构时自动取消所有附加的请求
     */
    ~QCNetworkCancelToken() override;
    
    /**
     * @brief 将请求附加到此令牌
     * @param reply 网络响应对象
     */
    void attach(QCNetworkReply *reply);
    
    /**
     * @brief 将多个请求附加到此令牌
     * @param replies 网络响应对象列表
     */
    void attachMultiple(const QList<QCNetworkReply *> &replies);
    
    /**
     * @brief 从令牌中移除请求
     * @param reply 网络响应对象
     */
    void detach(QCNetworkReply *reply);
    
    /**
     * @brief 取消令牌中的所有请求
     */
    void cancel();
    
    /**
     * @brief 设置自动超时取消
     * @param msecs 超时时间（毫秒），0 表示禁用自动超时
     */
    void setAutoTimeout(int msecs);
    
    /**
     * @brief 获取附加的请求数量
     */
    int attachedCount() const;
    
    /**
     * @brief 检查令牌是否已被取消
     */
    bool isCancelled() const;
    
    /**
     * @brief 清空所有附加的请求
     */
    void clear();

signals:
    /**
     * @brief 当令牌被取消时发射此信号
     */
    void cancelled();
    
    /**
     * @brief 当某个请求完成时发射此信号
     * @param reply 已完成的响应对象
     */
    void requestCompleted(QCNetworkReply *reply);

private slots:
    void onReplyFinished();
    void onAutoTimeoutTriggered();

private:
    class Private;
    std::unique_ptr<Private> d_ptr;
};

} // namespace QCurl

#endif // QCNETWORKCANCELTOKEN_H
