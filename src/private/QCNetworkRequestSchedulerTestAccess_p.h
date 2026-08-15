// SPDX-License-Identifier: MIT
// Copyright (c) 2025 QCurl Project

#ifndef QCNETWORKREQUESTSCHEDULERTESTACCESS_P_H
#define QCNETWORKREQUESTSCHEDULERTESTACCESS_P_H

#include "private/QCNetworkRequestSchedulerPrivate_p.h"

#include <QMutexLocker>

namespace QCurl {

/**
 * @brief 为仓内测试暴露 Scheduler 的 owner-thread-only 内部入口。
 *
 * 该类型不属于安装 API，只用于验证错误线程调用在接触 QObject 参数前 fail-closed。
 */
class QCNetworkRequestSchedulerTestAccess final
{
public:
    /// 调用请求启动入口。
    static void startRequest(QCNetworkRequestScheduler *scheduler, QCNetworkReply *reply)
    {
        scheduler->startRequest(reply);
    }

    /// 调用请求完成入口。
    static void finishRequest(QCNetworkRequestScheduler *scheduler, QCNetworkReply *reply)
    {
        scheduler->onRequestFinished(reply);
    }

    /// 调用 reply 销毁入口。
    static void destroyReply(QCNetworkRequestScheduler *scheduler, QObject *object)
    {
        scheduler->onReplyDestroyed(object);
    }

    /// 为指定 reply 建立 progress 跟踪；重复调用用于验证 rebind 语义。
    static void connectProgress(QCNetworkRequestScheduler *scheduler, QCNetworkReply *reply)
    {
        scheduler->m_impl->connectProgressTracking(scheduler, reply, Internal::replyKey(reply));
    }

    /// 断开指定 reply 的 progress 跟踪。
    static void disconnectProgress(QCNetworkRequestScheduler *scheduler, QCNetworkReply *reply)
    {
        scheduler->m_impl->disconnectProgressTracking(Internal::replyKey(reply));
    }

    /// 清空 progress delta 累计窗口。
    static void resetProgressWindow(QCNetworkRequestScheduler *scheduler)
    {
        QMutexLocker locker(&scheduler->m_impl->mutex);
        scheduler->m_impl->bytesTransferredInWindow = 0;
    }

    /// 返回当前 progress delta 累计值。
    [[nodiscard]] static qint64 progressWindow(QCNetworkRequestScheduler *scheduler)
    {
        QMutexLocker locker(&scheduler->m_impl->mutex);
        return scheduler->m_impl->bytesTransferredInWindow;
    }

private:
    Q_DISABLE_COPY_MOVE(QCNetworkRequestSchedulerTestAccess)
    QCNetworkRequestSchedulerTestAccess() = delete;
};

} // namespace QCurl

#endif // QCNETWORKREQUESTSCHEDULERTESTACCESS_P_H
