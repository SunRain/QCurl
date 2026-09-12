// SPDX-License-Identifier: MIT
// Copyright (c) 2025 QCurl Project

#include "QCNetworkReply.h"
#include "QCNetworkRequestScheduler_p.h"

#include <QThread>

namespace QCurl {

void QCNetworkRequestScheduler::startRequest(const Internal::AdmissionStart &start)
{
    Q_ASSERT(QThread::currentThread() == thread());
    const RequestId id = start.request.id;
    auto it            = m_bindings.find(id);
    Q_ASSERT(it != m_bindings.end());
    it->startTicket = m_nextStartTicket++;
    it->elapsed.start();
    const quint64 ticket = it->startTicket;
    connectProgressTracking(id);
    // 即使同线程也延后 execute，工厂返回后调用方仍能接线或取消。
    QMetaObject::invokeMethod(
        this,
        [this, id, ticket, snapshot = start.request.snapshot]() {
            dispatchReplyExecution(id, ticket, snapshot);
        },
        Qt::QueuedConnection);
}

bool QCNetworkRequestScheduler::isStartTicketValid(RequestId id, quint64 ticket) const
{
    const auto it = m_bindings.constFind(id);
    if (it == m_bindings.cend() || it->startTicket != ticket) {
        return false;
    }
    const auto request = m_core.request(id);
    return request && request->state == Internal::ScheduledState::Running;
}

void QCNetworkRequestScheduler::dispatchReplyExecution(RequestId id,
                                                       quint64 ticket,
                                                       const Internal::ReplySnapshot &snapshot)
{
    Q_ASSERT(QThread::currentThread() == thread());
    if (!isStartTicketValid(id, ticket)) {
        return;
    }
    QPointer<QCNetworkRequestScheduler> safeScheduler(this);
    const QPointer<QCNetworkReply> safeReply = m_bindings.value(id).reply;
    if (!safeReply || safeReply->thread() != thread()) {
        return;
    }
    if (safeReply->state() == ReplyState::Cancelled || safeReply->isFinished()) {
        onRequestFinished(id);
        return;
    }
    Q_EMIT requestAboutToStart(safeReply.data(), snapshot.lane, snapshot.origin, snapshot.priority);
    if (!safeScheduler || !safeReply || !safeScheduler->isStartTicketValid(id, ticket)) {
        return;
    }
    if (safeReply->thread() != safeScheduler->thread()) {
        return;
    }
    if (safeReply->state() == ReplyState::Cancelled || safeReply->isFinished()) {
        safeScheduler->onRequestFinished(id);
        return;
    }
    safeReply->execute();
    if (!safeScheduler || !safeReply || !safeScheduler->isStartTicketValid(id, ticket)) {
        return;
    }
    if (safeReply->state() != ReplyState::Cancelled) {
        Q_EMIT safeScheduler->requestStarted(safeReply.data(),
                                             snapshot.lane,
                                             snapshot.origin,
                                             snapshot.priority);
    }
}

} // namespace QCurl
