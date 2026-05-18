// SPDX-License-Identifier: MIT
// Copyright (c) 2025 QCurl Project

#include "QCNetworkRequestScheduler.h"

#include "QCNetworkReply.h"
#include "private/QCNetworkRequestSchedulerPrivate_p.h"

#include <QMetaObject>
#include <QMutexLocker>
#include <QPointer>
#include <QThread>

namespace QCurl {

void QCNetworkRequestScheduler::onRequestFinished(QCNetworkReply *reply)
{
    if (!reply) {
        return;
    }

    if (QThread::currentThread() != thread()) {
        QPointer<QCNetworkReply> safeReply(reply);
        Internal::invokeOnSchedulerOwnerThread(
            this,
            [this, safeReply]() {
                if (safeReply) {
                    onRequestFinished(safeReply.data());
                }
            },
            "QCNetworkRequestScheduler::onRequestFinished");
        return;
    }
    Internal::assertSchedulerOwnerThread(this, "QCNetworkRequestScheduler::onRequestFinished");
    Q_ASSERT_X(reply->thread() == thread(),
               "QCNetworkRequestScheduler::onRequestFinished",
               "reply must live on the scheduler owner thread");

    const Internal::ReplyKey key = Internal::replyKey(reply);
    const Internal::ReplyOutcome outcome = Internal::captureReplyOutcome(reply);
    Internal::FinalizeResult result;
    {
        QMutexLocker locker(&m_impl->mutex);
        result = m_impl->finalizeReplyLocked(key, Internal::FinalizeTrigger::FinishedSignal, outcome);
    }

    if (!result.wasTracked) {
        return;
    }

    if (result.emitCancelled) {
        emit requestCancelled(reply, result.snapshot.lane, result.snapshot.hostKey);
    }

    if (result.emitFinished) {
        emit requestFinished(reply, result.snapshot.lane, result.snapshot.hostKey);
    }

    if (result.shouldKickQueue) {
        processQueue();
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
    if (!obj) {
        return;
    }

    if (QThread::currentThread() != thread()) {
        QPointer<QObject> safeObject(obj);
        Internal::invokeOnSchedulerOwnerThread(
            this,
            [this, safeObject]() {
                if (safeObject) {
                    onReplyDestroyed(safeObject.data());
                }
            },
            "QCNetworkRequestScheduler::onReplyDestroyed");
        return;
    }
    Internal::assertSchedulerOwnerThread(this, "QCNetworkRequestScheduler::onReplyDestroyed");

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
