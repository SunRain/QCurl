// SPDX-License-Identifier: MIT
// Copyright (c) 2025 QCurl Project

#include "QCNetworkReply.h"
#include "QCNetworkRequestScheduler.h"
#include "private/QCNetworkRequestSchedulerPrivate_p.h"

#include <QMetaObject>
#include <QMutexLocker>
#include <QPointer>
#include <QThread>

namespace QCurl {

void QCNetworkRequestScheduler::onRequestFinished(QCNetworkReply *reply)
{
    if (QThread::currentThread() != thread()) {
        qWarning() << "QCNetworkRequestScheduler::onRequestFinished: owner thread required";
        return;
    }
    Internal::assertSchedulerOwnerThread(this, "QCNetworkRequestScheduler::onRequestFinished");
    if (!reply) {
        return;
    }
    Q_ASSERT_X(reply->thread() == thread(),
               "QCNetworkRequestScheduler::onRequestFinished",
               "reply must live on the scheduler owner thread");

    const Internal::ReplyKey key         = Internal::replyKey(reply);
    const Internal::ReplyOutcome outcome = Internal::captureReplyOutcome(reply);
    Internal::FinalizeResult result;
    {
        QMutexLocker locker(&m_impl->mutex);
        result = m_impl->finalizeReplyLocked(key,
                                             Internal::FinalizeTrigger::FinishedSignal,
                                             outcome);
    }

    if (!result.wasTracked) {
        return;
    }

    QPointer<QCNetworkRequestScheduler> safeScheduler(this);
    QPointer<QCNetworkReply> safeReply(reply);
    if (result.emitCancelled) {
        Q_EMIT safeScheduler->requestCancelled(safeReply.data(),
                                               result.snapshot.lane,
                                               result.snapshot.hostKey);
        if (!safeScheduler || !safeReply) {
            return;
        }
    }

    if (result.emitFinished) {
        Q_EMIT safeScheduler->requestFinished(safeReply.data(),
                                              result.snapshot.lane,
                                              result.snapshot.hostKey);
        if (!safeScheduler || !safeReply) {
            return;
        }
    }

    if (result.shouldKickQueue) {
        safeScheduler->processQueue();
    }
}

void QCNetworkRequestScheduler::updateBandwidthStats()
{
    if (QThread::currentThread() != thread()) {
        Internal::invokeOnSchedulerOwnerThread(
            this,
            [this]() { updateBandwidthStats(); },
            "QCNetworkRequestScheduler::updateBandwidthStats");
        return;
    }
    Internal::assertSchedulerOwnerThread(this, "QCNetworkRequestScheduler::updateBandwidthStats");

    const bool shouldProcess = m_impl->resetBandwidthWindow();

    if (shouldProcess) {
        processQueue();
    }
}

void QCNetworkRequestScheduler::onReplyDestroyed(QObject *obj)
{
    if (QThread::currentThread() != thread()) {
        qWarning() << "QCNetworkRequestScheduler::onReplyDestroyed: owner thread required";
        return;
    }
    Internal::assertSchedulerOwnerThread(this, "QCNetworkRequestScheduler::onReplyDestroyed");
    if (!obj) {
        return;
    }

    const Internal::ReplyKey key = obj;
    Internal::FinalizeResult result;
    {
        QMutexLocker locker(&m_impl->mutex);
        result = m_impl->finalizeReplyLocked(key, Internal::FinalizeTrigger::Destroyed);
    }

    if (result.shouldKickQueue) {
        QMetaObject::invokeMethod(this, [this]() { processQueue(); }, Qt::QueuedConnection);
    }
}

} // namespace QCurl
