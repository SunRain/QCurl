// SPDX-License-Identifier: MIT
// Copyright (c) 2025 QCurl Project

#ifndef QCNETWORKREQUESTSCHEDULERTESTACCESS_P_H
#define QCNETWORKREQUESTSCHEDULERTESTACCESS_P_H

#include "QCNetworkAccessManager_p.h"
#include "QCNetworkRequestScheduler_p.h"

namespace QCurl {

// 仅用于故障注入和 Qt 观测；不提供配置、自动注册或替代产品入口。
class QCNetworkRequestSchedulerTestAccess final
{
public:
    static QTimer *admissionTimer(QCNetworkAccessManager &manager)
    {
        return &manager.d_func()->scheduler->m_throttleTimer;
    }

    static qint64 progressWindow(QCNetworkAccessManager &manager)
    {
        return manager.d_func()->scheduler->m_bytesTransferredInWindow;
    }

    static void rebindProgress(QCNetworkAccessManager &manager, QCNetworkReply *reply)
    {
        auto *scheduler = manager.d_func()->scheduler;
        scheduler->connectProgressTracking(scheduler->requestId(reply));
    }

    static void invalidateStartTicket(QCNetworkAccessManager &manager, QCNetworkReply *reply)
    {
        auto *scheduler = manager.d_func()->scheduler;
        const auto id   = scheduler->requestId(reply);
        ++scheduler->m_bindings[id].startTicket;
    }

private:
    Q_DISABLE_COPY_MOVE(QCNetworkRequestSchedulerTestAccess)
    QCNetworkRequestSchedulerTestAccess() = delete;
};

} // namespace QCurl

#endif // QCNETWORKREQUESTSCHEDULERTESTACCESS_P_H
