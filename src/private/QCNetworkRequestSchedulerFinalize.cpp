// SPDX-License-Identifier: MIT
// Copyright (c) 2025 QCurl Project

#include "QCNetworkReply.h"
#include "QCNetworkRequestScheduler_p.h"

#include <QThread>

namespace QCurl {

std::optional<Internal::ReplyOutcome> QCNetworkRequestScheduler::captureOutcome(RequestId id) const
{
    Q_ASSERT(QThread::currentThread() == thread());
    const auto binding = m_bindings.value(id);
    if (!binding.reply || binding.reply->thread() != thread()) {
        return std::nullopt;
    }
    Internal::ReplyOutcome outcome;
    outcome.cancelled     = binding.reply->state() == ReplyState::Cancelled
                            || binding.reply->error() == NetworkError::OperationCancelled;
    outcome.bytesReceived = binding.reply->bytesReceived();
    outcome.bytesSent     = binding.lastBytesSent;
    outcome.durationMs    = binding.elapsed.isValid() ? binding.elapsed.elapsed() : 0;
    return outcome;
}

void QCNetworkRequestScheduler::observeFinished(RequestId id)
{
    Q_ASSERT(QThread::currentThread() == thread());
    const auto outcome = captureOutcome(id);
    if (!outcome) {
        return;
    }
    m_bindings[id].outcome = outcome;
    // execute 内同步 finished 不应提前使本次有效启动票据失效。
    QMetaObject::invokeMethod(this, [this, id]() { onRequestFinished(id); }, Qt::QueuedConnection);
}

void QCNetworkRequestScheduler::onRequestFinished(RequestId id)
{
    Q_ASSERT(QThread::currentThread() == thread());
    const auto binding = m_bindings.value(id);
    const auto outcome = binding.outcome ? binding.outcome : captureOutcome(id);
    if (!outcome) {
        return;
    }
    const auto result = m_core.finalize(id, Internal::FinalizeTrigger::FinishedSignal, *outcome);
    unbindReply(id);
    if (!result.wasTracked) {
        return;
    }
    QPointer<QCNetworkRequestScheduler> guard(this);
    if (result.emitCancelled) {
        Q_EMIT requestCancelled(binding.reply.data(),
                                result.snapshot.lane,
                                result.snapshot.origin,
                                result.snapshot.priority);
    }
    if (guard && guard->notifyPendingEmpty(result.pendingQueueEmptied)) {
        guard->processQueue();
    }
}

void QCNetworkRequestScheduler::onReplyDestroyed(RequestId id)
{
    Q_ASSERT(QThread::currentThread() == thread());
    const auto outcome = m_bindings.value(id).outcome;
    const auto result  = outcome ? m_core.finalize(id,
                                                   Internal::FinalizeTrigger::FinishedSignal,
                                                   *outcome)
                                 : m_core.finalize(id, Internal::FinalizeTrigger::Destroyed);
    unbindReply(id);
    if (result.wasTracked && notifyPendingEmpty(result.pendingQueueEmptied)) {
        queuePump();
    }
}

} // namespace QCurl
