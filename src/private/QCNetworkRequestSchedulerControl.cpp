// SPDX-License-Identifier: MIT
// Copyright (c) 2025 QCurl Project

#include "QCNetworkReply.h"
#include "QCNetworkRequestScheduler_p.h"

#include <QThread>

namespace QCurl {

SchedulerCommandResult QCNetworkRequestScheduler::validateCommand(QCNetworkReply *reply,
                                                                  RequestId *id) const
{
    if (QThread::currentThread() != thread()) {
        return SchedulerCommandResult::WrongThread;
    }
    if (!reply) {
        return SchedulerCommandResult::NullReply;
    }
    *id = requestId(reply);
    if (!*id) {
        return SchedulerCommandResult::NotTracked;
    }
    if (reply->thread() != thread()) {
        return SchedulerCommandResult::ThreadAffinityMismatch;
    }
    return SchedulerCommandResult::Applied;
}

SchedulerCommandResult QCNetworkRequestScheduler::deferPendingRequest(QCNetworkReply *reply)
{
    RequestId id     = 0;
    const auto valid = validateCommand(reply, &id);
    if (valid != SchedulerCommandResult::Applied) {
        return valid;
    }
    const auto change = m_core.defer(id);
    if (change.result == SchedulerCommandResult::Applied
        && notifyPendingEmpty(change.pendingQueueEmptied)) {
        processQueue();
    }
    return change.result;
}

SchedulerCommandResult QCNetworkRequestScheduler::undeferRequest(QCNetworkReply *reply)
{
    RequestId id     = 0;
    const auto valid = validateCommand(reply, &id);
    if (valid != SchedulerCommandResult::Applied) {
        return valid;
    }
    const auto change = m_core.undefer(id);
    if (change.result != SchedulerCommandResult::Applied) {
        return change.result;
    }
    QPointer<QCNetworkRequestScheduler> guard(this);
    Q_EMIT requestQueued(reply,
                         change.snapshot.lane,
                         change.snapshot.origin,
                         change.snapshot.priority);
    if (guard) {
        guard->processQueue();
    }
    return change.result;
}

SchedulerCommandResult QCNetworkRequestScheduler::changePriority(QCNetworkReply *reply,
                                                                 QCNetworkRequestPriority priority)
{
    if (QThread::currentThread() != thread()) {
        return SchedulerCommandResult::WrongThread;
    }
    if (!reply) {
        return SchedulerCommandResult::NullReply;
    }
    if (!Internal::isValidPriority(priority)) {
        return SchedulerCommandResult::InvalidArgument;
    }
    RequestId id     = 0;
    const auto valid = validateCommand(reply, &id);
    if (valid != SchedulerCommandResult::Applied) {
        return valid;
    }
    const auto change = m_core.changePriority(id, priority);
    if (change.result != SchedulerCommandResult::Applied) {
        return change.result;
    }
    QPointer<QCNetworkRequestScheduler> guard(this);
    Q_EMIT requestPriorityChanged(reply,
                                  change.snapshot.lane,
                                  change.snapshot.origin,
                                  change.snapshot.priority);
    if (guard) {
        guard->processQueue();
    }
    return change.result;
}

int QCNetworkRequestScheduler::cancelRequests(const QList<RequestId> &ids)
{
    /// 整批取消提交后的通知快照，避免用户回调打断尚未投递的传输取消。
    struct CancelledReply
    {
        QPointer<QCNetworkReply> reply;
        Internal::ReplySnapshot snapshot;
    };
    QList<CancelledReply> cancelled;
    bool emptied = false;
    for (RequestId id : ids) {
        const auto result  = m_core.finalize(id, Internal::FinalizeTrigger::ExplicitCancel);
        const auto binding = unbindReply(id);
        if (result.wasTracked) {
            cancelled.append({binding.reply, result.snapshot});
            emptied = emptied || result.pendingQueueEmptied;
        }
    }
    const int count = cancelled.size();
    // 整批状态已提交。后续通知允许重入，但不能重复取消这批标识或改写本次返回数量。
    for (const auto &item : std::as_const(cancelled)) {
        if (!item.reply) {
            continue;
        }
        QMetaObject::invokeMethod(
            item.reply.data(),
            [reply = item.reply]() {
                if (reply) {
                    reply->cancel();
                }
            },
            Qt::QueuedConnection);
    }
    // 通知可能销毁 manager；所有已提交取消必须先送往各 reply，不能漏掉重新设 parent 的对象。
    QPointer<QCNetworkRequestScheduler> guard(this);
    for (const auto &item : std::as_const(cancelled)) {
        if (!item.reply) {
            continue;
        }
        Q_EMIT requestCancelled(item.reply.data(),
                                item.snapshot.lane,
                                item.snapshot.origin,
                                item.snapshot.priority);
        if (!guard) {
            return count;
        }
    }
    if (notifyPendingEmpty(emptied)) {
        processQueue();
    }
    return count;
}

SchedulerCommandResult QCNetworkRequestScheduler::cancelRequest(QCNetworkReply *reply)
{
    RequestId id     = 0;
    const auto valid = validateCommand(reply, &id);
    if (valid != SchedulerCommandResult::Applied) {
        return valid;
    }
    cancelRequests({id});
    return SchedulerCommandResult::Applied;
}

int QCNetworkRequestScheduler::cancelLaneRequests(const QCNetworkLaneKey &lane, bool includeRunning)
{
    Q_ASSERT(QThread::currentThread() == thread());
    return cancelRequests(m_core.requestIds(lane, includeRunning));
}

} // namespace QCurl
