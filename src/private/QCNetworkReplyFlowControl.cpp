/**
 * @file
 * @brief QCNetworkReply 传输 pause/backpressure 状态实现。
 */

#include "QCCurlMultiManager.h"
#include "QCNetworkReply_p.h"
#include "private/QCNetworkReplyCallbacks_p.h"
#include "private/QCNetworkReplyFlowControl_p.h"

#include <QDebug>
#include <QIODevice>
#include <QMetaObject>
#include <QPointer>
#include <QThread>

#include <curl/curl.h>

namespace QCurl::Internal {
namespace {

bool replyIsTerminal(const QCNetworkReplyPrivate *reply)
{
    return !reply || reply->state == ReplyState::Cancelled || reply->state == ReplyState::Error
           || reply->state == ReplyState::Finished;
}

int normalizePauseMask(int desiredMask)
{
    return desiredMask & (CURLPAUSE_RECV | CURLPAUSE_SEND);
}

bool shouldDeferRecvPauseToWriteCallback(int flags)
{
    return (flags == CURLPAUSE_RECV) && isInReplyCurlCallback();
}

int pauseFlagsFromMode(PauseMode mode)
{
    switch (mode) {
        case PauseMode::Recv:
            return CURLPAUSE_RECV;
        case PauseMode::Send:
            return CURLPAUSE_SEND;
        case PauseMode::All:
            return CURLPAUSE_ALL;
    }

    return CURLPAUSE_ALL;
}

/// 仅在 Reply QObject 的 owner thread 同步更新 easy pause 状态；线程违约时 fail-closed。
bool pauseCurlEasy(QCNetworkReplyPrivate *reply, int flags)
{
    QCNetworkReply *const publicReply = reply ? reply->qObject() : nullptr;
    if (!publicReply || QThread::currentThread() != publicReply->thread()) {
        Q_ASSERT_X(publicReply && QThread::currentThread() == publicReply->thread(),
                   "pauseCurlEasy",
                   "reply flow control must run on the reply owner thread");
        return false;
    }

    const CURLcode result = curl_easy_pause(reply->activeCurlHandle(), flags);
    if (result == CURLE_OK) {
        return true;
    }

    qWarning() << "QCNetworkReplyPrivate::applyPauseMask: curl_easy_pause failed:"
               << curl_easy_strerror(result) << "desiredMask=" << normalizePauseMask(flags);
    return false;
}

} // namespace

int desiredReplyPauseMask(const QCNetworkReplyPrivate *reply) noexcept
{
    return reply ? (reply->userPauseMask | reply->internalPauseMask) : 0;
}

bool applyReplyPauseMask(QCNetworkReplyPrivate *reply, int desiredMask)
{
    if (!reply) {
        return false;
    }
    if (!reply->activeCurlHandle()) {
        return false;
    }

    const int normalized = normalizePauseMask(desiredMask);
    if (normalized == reply->appliedPauseMask) {
        return true;
    }

    const int flags = (normalized == 0) ? CURLPAUSE_CONT : normalized;
    if (!pauseCurlEasy(reply, flags)) {
        return false;
    }

    reply->appliedPauseMask = normalized;
    return true;
}

SignalEmissionResult setReplyBackpressureActive(QCNetworkReplyPrivate *reply, bool active)
{
    if (!reply || reply->backpressureLimitBytes <= 0) {
        if (reply) {
            reply->backpressureActive = false;
        }
        return SignalEmissionResult::Alive;
    }
    if (reply->backpressureActive == active) {
        return SignalEmissionResult::Alive;
    }

    reply->backpressureActive = active;
    const QPointer<QCNetworkReply> observer(reply->qObject());
    const qint64 bufferedBytes = reply->bodyBuffer.byteAmount();
    const qint64 limitBytes    = reply->backpressureLimitBytes;
    return emitReplySignal(observer, [active, bufferedBytes, limitBytes](QCNetworkReply *q) {
        Q_EMIT q->backpressureStateChanged(active, bufferedBytes, limitBytes);
    });
}

SignalEmissionResult setReplyUploadSendPaused(QCNetworkReplyPrivate *reply, bool paused)
{
    if (!reply) {
        return SignalEmissionResult::Alive;
    }
    if (reply->uploadSendPaused == paused) {
        return SignalEmissionResult::Alive;
    }

    reply->uploadSendPaused = paused;
    const QPointer<QCNetworkReply> observer(reply->qObject());
    return emitReplySignal(observer, [paused](QCNetworkReply *q) {
        Q_EMIT q->uploadSendPausedChanged(paused);
    });
}

SignalEmissionResult maybeResumeReplyRecvFromBackpressure(QCNetworkReplyPrivate *reply)
{
    if (!reply || reply->backpressureLimitBytes <= 0) {
        return SignalEmissionResult::Alive;
    }
    if ((reply->internalPauseMask & CURLPAUSE_RECV) == 0 || replyIsTerminal(reply)) {
        return SignalEmissionResult::Alive;
    }
    if (reply->bodyBuffer.byteAmount() > reply->backpressureResumeBytes) {
        return SignalEmissionResult::Alive;
    }

    const int oldMask         = reply->appliedPauseMask;
    const int oldInternalMask = reply->internalPauseMask;
    reply->internalPauseMask &= ~CURLPAUSE_RECV;
    if (!applyReplyPauseMask(reply, desiredReplyPauseMask(reply))) {
        reply->internalPauseMask = oldInternalMask;
        return SignalEmissionResult::Alive;
    }

    if (setReplyBackpressureActive(reply, false) == SignalEmissionResult::Destroyed) {
        return SignalEmissionResult::Destroyed;
    }
    if ((oldMask & CURLPAUSE_RECV) && ((reply->appliedPauseMask & CURLPAUSE_RECV) == 0)) {
        QCCurlMultiManager::instance()->wakeup();
    }
    return SignalEmissionResult::Alive;
}

SignalEmissionResult resumeReplySendFromRequestBodySourceIfNeeded(QCNetworkReplyPrivate *reply)
{
    if (!reply) {
        return SignalEmissionResult::Alive;
    }
    if ((reply->internalPauseMask & CURLPAUSE_SEND) == 0 || replyIsTerminal(reply)) {
        return SignalEmissionResult::Alive;
    }

    QIODevice *device = reply->requestBodySource.device.data();
    if (!device || !device->isReadable()) {
        return SignalEmissionResult::Alive;
    }
    if (device->bytesAvailable() <= 0 && !device->atEnd()) {
        return SignalEmissionResult::Alive;
    }

    const int oldMask = reply->appliedPauseMask;
    reply->internalPauseMask &= ~CURLPAUSE_SEND;
    if (!applyReplyPauseMask(reply, desiredReplyPauseMask(reply))) {
        reply->internalPauseMask |= CURLPAUSE_SEND;
        return SignalEmissionResult::Alive;
    }

    if (setReplyUploadSendPaused(reply, false) == SignalEmissionResult::Destroyed) {
        return SignalEmissionResult::Destroyed;
    }
    if ((oldMask & CURLPAUSE_SEND) && ((reply->appliedPauseMask & CURLPAUSE_SEND) == 0)) {
        QCCurlMultiManager::instance()->wakeup();
    }
    return SignalEmissionResult::Alive;
}

void scheduleReplyBackpressureResumeAfterRead(QCNetworkReply *reply,
                                              QCNetworkReplyPrivate *privateReply)
{
    if (!reply || !privateReply) {
        return;
    }
    if (privateReply->backpressureLimitBytes <= 0
        || (privateReply->internalPauseMask & CURLPAUSE_RECV) == 0) {
        return;
    }

    QPointer<QCNetworkReply> safeReply(reply);
    QMetaObject::invokeMethod(
        reply,
        [safeReply, privateReply]() {
            if (safeReply) {
                Q_UNUSED(privateReply->maybeResumeRecvFromBackpressure());
            }
        },
        Qt::QueuedConnection);
}

SignalEmissionResult clearReplyFlowControlOnTerminalState(QCNetworkReplyPrivate *reply)
{
    if (!reply) {
        return SignalEmissionResult::Alive;
    }

    reply->internalPauseMask &= ~CURLPAUSE_RECV;
    reply->internalPauseMask &= ~CURLPAUSE_SEND;
    if (setReplyBackpressureActive(reply, false) == SignalEmissionResult::Destroyed) {
        return SignalEmissionResult::Destroyed;
    }
    return setReplyUploadSendPaused(reply, false);
}

void pauseReplyTransport(QCNetworkReply *reply, QCNetworkReplyPrivate *privateReply, PauseMode mode)
{
    if (!privateReply) {
        return;
    }
    if (privateReply->state != ReplyState::Running) {
        return;
    }

    const int flags             = pauseFlagsFromMode(mode);
    const int oldUserMask       = privateReply->userPauseMask;
    privateReply->userPauseMask = flags;
    if (!shouldDeferRecvPauseToWriteCallback(flags)
        && !applyReplyPauseMask(privateReply, desiredReplyPauseMask(privateReply))) {
        privateReply->userPauseMask = oldUserMask;
        return;
    }

    Q_UNUSED(privateReply->setState(ReplyState::Paused));
    Q_UNUSED(reply);
}

void resumeReplyTransport(QCNetworkReply *reply, QCNetworkReplyPrivate *privateReply)
{
    if (!privateReply) {
        return;
    }
    if (privateReply->state != ReplyState::Paused) {
        return;
    }

    const int oldUserMask       = privateReply->userPauseMask;
    privateReply->userPauseMask = 0;
    if (!applyReplyPauseMask(privateReply, desiredReplyPauseMask(privateReply))) {
        privateReply->userPauseMask = oldUserMask;
        return;
    }

    if (privateReply->setState(ReplyState::Running) == SignalEmissionResult::Destroyed) {
        return;
    }
    QCCurlMultiManager::instance()->wakeup();
    Q_UNUSED(reply);
}

} // namespace QCurl::Internal
