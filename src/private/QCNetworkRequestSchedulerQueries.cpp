// SPDX-License-Identifier: MIT
// Copyright (c) 2025 QCurl Project

#include "QCNetworkRequestScheduler.h"

#include "QCNetworkReply.h"
#include "private/QCNetworkRequestSchedulerPrivate_p.h"

#include <QMutexLocker>
#include <QThread>

namespace QCurl {

QCNetworkRequestScheduler::Statistics QCNetworkRequestScheduler::statistics() const
{
    if (QThread::currentThread() != thread()) {
        return Internal::rejectOffOwnerThreadValue(this, Statistics{}, "QCNetworkRequestScheduler::statistics");
    }
    Internal::assertSchedulerOwnerThread(this, "QCNetworkRequestScheduler::statistics");

    QMutexLocker locker(&m_impl->mutex);
    return m_impl->stats;
}

QList<QCNetworkReply *> QCNetworkRequestScheduler::pendingRequests() const
{
    if (QThread::currentThread() != thread()) {
        return Internal::rejectOffOwnerThreadValue(this,
                                                  QList<QCNetworkReply *>{},
                                                  "QCNetworkRequestScheduler::pendingRequests");
    }
    Internal::assertSchedulerOwnerThread(this, "QCNetworkRequestScheduler::pendingRequests");

    QMutexLocker locker(&m_impl->mutex);

    QList<QCNetworkReply *> result;
    result.reserve(m_impl->queues.pendingRequests.size());
    for (const auto &request : std::as_const(m_impl->queues.pendingRequests)) {
        if (request.reply) {
            result.append(request.reply);
        }
    }
    return result;
}

QList<QCNetworkReply *> QCNetworkRequestScheduler::runningRequests() const
{
    if (QThread::currentThread() != thread()) {
        return Internal::rejectOffOwnerThreadValue(this,
                                                  QList<QCNetworkReply *>{},
                                                  "QCNetworkRequestScheduler::runningRequests");
    }
    Internal::assertSchedulerOwnerThread(this, "QCNetworkRequestScheduler::runningRequests");

    QMutexLocker locker(&m_impl->mutex);
    return m_impl->queues.runningRequests;
}

} // namespace QCurl
