// SPDX-License-Identifier: MIT
// Copyright (c) 2025 QCurl Project

#include "QCNetworkAccessManager.h"
#include "QCNetworkAccessManager_p.h"

#include <QThread>

namespace QCurl {

SchedulerCommandResult QCNetworkAccessManager::deferScheduledRequest(QCNetworkReply *reply)
{
    if (QThread::currentThread() != thread()) {
        return SchedulerCommandResult::WrongThread;
    }
    Q_D(QCNetworkAccessManager);
    return d->scheduler->deferPendingRequest(reply);
}

SchedulerCommandResult QCNetworkAccessManager::undeferScheduledRequest(QCNetworkReply *reply)
{
    if (QThread::currentThread() != thread()) {
        return SchedulerCommandResult::WrongThread;
    }
    Q_D(QCNetworkAccessManager);
    return d->scheduler->undeferRequest(reply);
}

SchedulerCommandResult QCNetworkAccessManager::cancelScheduledRequest(QCNetworkReply *reply)
{
    if (QThread::currentThread() != thread()) {
        return SchedulerCommandResult::WrongThread;
    }
    Q_D(QCNetworkAccessManager);
    return d->scheduler->cancelRequest(reply);
}

SchedulerCommandResult QCNetworkAccessManager::setScheduledRequestPriority(
    QCNetworkReply *reply, QCNetworkRequestPriority priority)
{
    if (QThread::currentThread() != thread()) {
        return SchedulerCommandResult::WrongThread;
    }
    Q_D(QCNetworkAccessManager);
    return d->scheduler->changePriority(reply, priority);
}

void QCNetworkAccessManagerPrivate::connectSchedulerSignals()
{
    Q_Q(QCNetworkAccessManager);
    // parent ownership 保证相同线程；不引入 queued 转发，保留启动前同步取消窗口。
    const auto forward = [this, q](auto source, auto destination) {
        QObject::connect(scheduler, source, q, destination);
    };
    forward(&QCNetworkRequestScheduler::requestQueued,
            &QCNetworkAccessManager::schedulerRequestQueued);
    forward(&QCNetworkRequestScheduler::requestPriorityChanged,
            &QCNetworkAccessManager::schedulerRequestPriorityChanged);
    forward(&QCNetworkRequestScheduler::requestAboutToStart,
            &QCNetworkAccessManager::schedulerRequestAboutToStart);
    forward(&QCNetworkRequestScheduler::requestStarted,
            &QCNetworkAccessManager::schedulerRequestStarted);
    forward(&QCNetworkRequestScheduler::requestCancelled,
            &QCNetworkAccessManager::schedulerRequestCancelled);
    QObject::connect(scheduler,
                     &QCNetworkRequestScheduler::queueEmpty,
                     q,
                     &QCNetworkAccessManager::schedulerPendingQueueEmpty);
}

} // namespace QCurl
