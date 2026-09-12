// SPDX-License-Identifier: MIT
// Copyright (c) 2025 QCurl Project

#include "QCNetworkReply.h"
#include "QCNetworkRequestScheduler_p.h"
#include "QCThreading_p.h"

#include <QThread>
#include <QUrl>

namespace QCurl {

namespace {

QString originKey(const QUrl &url)
{
    const QString scheme = url.scheme().toLower();
    QString host         = url.host().toLower();
    if (host.contains(QLatin1Char(':')) && !host.startsWith(QLatin1Char('['))) {
        host = QStringLiteral("[%1]").arg(host);
    }
    const int defaultPort = scheme == QStringLiteral("https") ? 443 : 80;
    return QStringLiteral("%1://%2:%3").arg(scheme, host).arg(url.port(defaultPort));
}

} // namespace

SchedulerCommandResult QCNetworkRequestScheduler::scheduleReply(QCNetworkReply *reply,
                                                                const QCNetworkLaneKey &lane,
                                                                QCNetworkRequestPriority priority)
{
    if (QThread::currentThread() != thread()) {
        return SchedulerCommandResult::WrongThread;
    }
    if (!reply) {
        return SchedulerCommandResult::NullReply;
    }
    if (!lane.isValid() || !Internal::isValidPriority(priority)) {
        return SchedulerCommandResult::InvalidArgument;
    }
    if (reply->thread() != thread()) {
        return SchedulerCommandResult::ThreadAffinityMismatch;
    }
    if (!Internal::hasEventDispatcher(thread())) {
        return SchedulerCommandResult::InvalidState;
    }
    if (requestId(reply)) {
        return SchedulerCommandResult::NoChange;
    }
    const RequestId id = m_nextRequestId++;
    const Internal::ReplySnapshot snapshot{lane, originKey(reply->url()), priority};
    const auto result = m_core.enqueue(id, snapshot);
    if (result != SchedulerCommandResult::Applied) {
        return result;
    }
    bindReply(id, reply);
    QPointer<QCNetworkRequestScheduler> safeScheduler(this);
    Q_EMIT requestQueued(reply, snapshot.lane, snapshot.origin, snapshot.priority);
    if (safeScheduler) {
        safeScheduler->processQueue();
    }
    return SchedulerCommandResult::Applied;
}

void QCNetworkRequestScheduler::processQueue()
{
    Q_ASSERT(QThread::currentThread() == thread());
    bool emptied   = false;
    bool throttled = false;
    while (m_core.hasRunnablePending()) {
        if (admissionThrottled()) {
            throttled = true;
            break;
        }
        const auto start = m_core.takeNextPending();
        if (!start) {
            break;
        }
        emptied = emptied || start->pendingQueueEmptied;
        // startRequest 只建立绑定和 queued 工作，不执行 reply 或用户回调。
        startRequest(*start);
    }
    if (throttled) {
        armAdmissionWakeup();
    } else {
        m_throttleTimer.stop();
    }
    // 状态和启动票据全部提交后才通知；此后不再访问成员，允许槽销毁 manager。
    if (emptied) {
        Q_EMIT queueEmpty();
    }
}

void QCNetworkRequestScheduler::queuePump()
{
    if (m_pumpQueued) {
        return;
    }
    m_pumpQueued = true;
    // 析构回调先退出当前 QObject 清理栈，再处理余下的 admission。
    QMetaObject::invokeMethod(
        this,
        [this]() {
            m_pumpQueued = false;
            processQueue();
        },
        Qt::QueuedConnection);
}

bool QCNetworkRequestScheduler::notifyPendingEmpty(bool emptied)
{
    QPointer<QCNetworkRequestScheduler> guard(this);
    if (emptied) {
        Q_EMIT queueEmpty();
    }
    return !guard.isNull();
}

} // namespace QCurl
