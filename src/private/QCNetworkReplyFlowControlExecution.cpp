/**
 * @file
 * @brief 在 owner thread 编排流控发布、延后恢复和 multi 唤醒。
 */

#include "QCCurlMultiManager.h"
#include "QCNetworkReply_p.h"
#include "private/QCNetworkReplyCallbacks_p.h"
#include "private/QCNetworkReplyExecution_p.h"
#include "private/QCNetworkReplyFlowControlExecution_p.h"
#include "private/QCNetworkReplyFlowControl_p.h"

#include <QDebug>
#include <QMetaObject>
#include <QThread>

namespace QCurl::Internal {
namespace {

bool onOwnerThread(const QCNetworkReplyPrivate *reply)
{
    const auto *observer = reply ? reply->qObject() : nullptr;
    if (observer && QThread::currentThread() == observer->thread()) {
        return true;
    }
    qWarning() << "QCNetworkReply: flow control must run on the reply owner thread";
    return false;
}

void reportPauseFailure(const ReplyFlowChange &change)
{
    if (change.error != CURLE_OK) {
        qWarning() << "QCNetworkReplyPrivate::applyPauseMask: curl_easy_pause failed:"
                   << curl_easy_strerror(change.error) << "desiredMask=" << change.nativeMask;
    }
}

SignalEmissionResult publishFlowChange(const QPointer<QCNetworkReply> &observer,
                                       const QSharedPointer<QCNetworkReplyTransferState> &holder,
                                       const ReplyFlowChange &change)
{
    reportPauseFailure(change);
    if (!observer) {
        return SignalEmissionResult::Destroyed;
    }
    if (holder->lifecycle.isTerminal()) {
        return SignalEmissionResult::Alive;
    }
    if (change.backpressureChanged) {
        const bool active  = holder->flow.backpressureActive();
        const qint64 bytes = holder->response.bytesAvailable();
        const qint64 limit = holder->flow.limitBytes();
        if (emitReplySignal(observer,
                            [active, bytes, limit](QCNetworkReply *reply) {
                                Q_EMIT reply->backpressureStateChanged(active, bytes, limit);
                            })
            == SignalEmissionResult::Destroyed) {
            return SignalEmissionResult::Destroyed;
        }
    }
    if (!holder->lifecycle.isTerminal() && change.uploadPauseChanged) {
        const bool paused = holder->flow.uploadSendPaused();
        if (emitReplySignal(observer,
                            [paused](QCNetworkReply *reply) {
                                Q_EMIT reply->uploadSendPausedChanged(paused);
                            })
            == SignalEmissionResult::Destroyed) {
            return SignalEmissionResult::Destroyed;
        }
    }
    if (change.wake && !holder->lifecycle.isTerminal()) {
        QCCurlMultiManager::instance()->wakeup();
    }
    return SignalEmissionResult::Alive;
}

} // namespace

SignalEmissionResult maybeResumeReplyRecvFromBackpressure(QCNetworkReplyPrivate *reply)
{
    if (!onOwnerThread(reply)) {
        return SignalEmissionResult::Alive;
    }
    const QPointer<QCNetworkReply> observer(reply->qObject());
    const auto holder = reply->transferState;
    const auto change = holder->flow.resumeAfterRead(reply->activeCurlHandle(),
                                                     holder->response.bytesAvailable(),
                                                     holder->lifecycle.isTerminal());
    return publishFlowChange(observer, holder, change);
}

SignalEmissionResult resumeReplySendFromRequestBodySourceIfNeeded(QCNetworkReplyPrivate *reply)
{
    if (!onOwnerThread(reply)) {
        return SignalEmissionResult::Alive;
    }
    const QPointer<QCNetworkReply> observer(reply->qObject());
    const auto holder = reply->transferState;
    const auto change = holder->flow.resumeReadySource(reply->activeCurlHandle(),
                                                       holder->bodySource.readiness(),
                                                       holder->lifecycle.isTerminal());
    return publishFlowChange(observer, holder, change);
}

void scheduleReplyBackpressureResumeAfterRead(QCNetworkReply *reply,
                                              QCNetworkReplyPrivate *privateReply)
{
    if (!reply || !privateReply || !privateReply->transferState->flow.recvResumePending()) {
        return;
    }
    const QPointer<QCNetworkReply> observer(reply);
    // readAll 可发生在 write callback 中；下轮再按实际水位解除接收原因。
    QMetaObject::invokeMethod(
        reply,
        [observer, privateReply]() {
            if (observer) {
                Q_UNUSED(maybeResumeReplyRecvFromBackpressure(privateReply));
            }
        },
        Qt::QueuedConnection);
}

SignalEmissionResult clearReplyFlowControlOnTerminalState(QCNetworkReplyPrivate *reply)
{
    const QPointer<QCNetworkReply> observer(reply->qObject());
    const auto holder = reply->transferState;
    if (holder->flow.leaveBackpressure()) {
        const qint64 bytes = holder->response.bytesAvailable();
        const qint64 limit = holder->flow.limitBytes();
        if (emitReplySignal(observer,
                            [bytes, limit](QCNetworkReply *q) {
                                Q_EMIT q->backpressureStateChanged(false, bytes, limit);
                            })
            == SignalEmissionResult::Destroyed) {
            return SignalEmissionResult::Destroyed;
        }
    }
    if (holder->flow.leaveUploadPause()) {
        return emitReplySignal(observer,
                               [](QCNetworkReply *q) { Q_EMIT q->uploadSendPausedChanged(false); });
    }
    return SignalEmissionResult::Alive;
}

void pauseReplyTransport(QCNetworkReply *reply, QCNetworkReplyPrivate *privateReply, PauseMode mode)
{
    if (!onOwnerThread(privateReply)) {
        return;
    }
    const auto holder = privateReply->transferState;
    if (holder->lifecycle.state() != ReplyState::Running) {
        return;
    }
    const QPointer<QCNetworkReply> observer(reply);
    const auto change = holder->flow.pauseUser(privateReply->activeCurlHandle(),
                                               mode,
                                               isInReplyCurlCallback());
    reportPauseFailure(change);
    if (observer && change.accepted && !holder->lifecycle.isTerminal()) {
        Q_UNUSED(privateReply->setState(ReplyState::Paused));
    }
}

void resumeReplyTransport(QCNetworkReply *reply, QCNetworkReplyPrivate *privateReply)
{
    if (!onOwnerThread(privateReply)) {
        return;
    }
    const auto holder = privateReply->transferState;
    if (holder->lifecycle.state() != ReplyState::Paused) {
        return;
    }
    const QPointer<QCNetworkReply> observer(reply);
    const auto change = holder->flow.resumeUser(privateReply->activeCurlHandle());
    reportPauseFailure(change);
    if (!observer || !change.accepted || holder->lifecycle.isTerminal()) {
        return;
    }
    if (privateReply->setState(ReplyState::Running) == SignalEmissionResult::Destroyed) {
        return;
    }
    if (!holder->lifecycle.isTerminal()) {
        QCCurlMultiManager::instance()->wakeup();
    }
}

#ifdef QCURL_ENABLE_TEST_HOOKS
void QCNetworkReplyExecution::pauseOnCurrentThreadForTest(QCNetworkReply *reply, PauseMode mode)
{
    pauseReplyTransport(reply, reply->d_func(), mode);
}
#endif

} // namespace QCurl::Internal
