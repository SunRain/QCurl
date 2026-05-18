/**
 * @file
 * @brief QCNetworkReply 传输 pause/backpressure 状态实现。
 */

#include "private/QCNetworkReplyFlowControl_p.h"

#include "QCCurlMultiManager.h"
#include "QCNetworkReply_p.h"
#include "private/QCNetworkReplyCallbacks_p.h"

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

bool pauseCurlEasy(QCNetworkReplyPrivate *reply, int flags)
{
    CURL *handle = reply->curlManager.handle();
    auto *multiManager = QCCurlMultiManager::instance();
    CURLcode result = CURLE_OK;

    if (QThread::currentThread() == multiManager->thread()) {
        result = curl_easy_pause(handle, flags);
    } else {
        QMetaObject::invokeMethod(
            multiManager,
            [handle, flags, &result]() { result = curl_easy_pause(handle, flags); },
            Qt::BlockingQueuedConnection);
    }

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
    if (!reply || reply->executionMode == ExecutionMode::Sync) {
        return false;
    }
    if (!reply->curlManager.handle()) {
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

void setReplyBackpressureActive(QCNetworkReplyPrivate *reply, bool active)
{
    if (!reply || reply->backpressureLimitBytes <= 0) {
        if (reply) {
            reply->backpressureActive = false;
        }
        return;
    }
    if (reply->backpressureActive == active) {
        return;
    }

    reply->backpressureActive = active;
    if (reply->q_ptr) {
        emit reply->q_ptr->backpressureStateChanged(active,
                                                    reply->bodyBuffer.byteAmount(),
                                                    reply->backpressureLimitBytes);
    }
}

void setReplyUploadSendPaused(QCNetworkReplyPrivate *reply, bool paused)
{
    if (!reply || reply->executionMode != ExecutionMode::Async) {
        if (reply) {
            reply->uploadSendPaused = false;
        }
        return;
    }
    if (reply->uploadSendPaused == paused) {
        return;
    }

    reply->uploadSendPaused = paused;
    if (reply->q_ptr) {
        emit reply->q_ptr->uploadSendPausedChanged(paused);
    }
}

void maybeResumeReplyRecvFromBackpressure(QCNetworkReplyPrivate *reply)
{
    if (!reply || reply->executionMode != ExecutionMode::Async || reply->backpressureLimitBytes <= 0) {
        return;
    }
    if ((reply->internalPauseMask & CURLPAUSE_RECV) == 0 || replyIsTerminal(reply)) {
        return;
    }
    if (reply->bodyBuffer.byteAmount() > reply->backpressureResumeBytes) {
        return;
    }

    const int oldMask = reply->appliedPauseMask;
    const int oldInternalMask = reply->internalPauseMask;
    reply->internalPauseMask &= ~CURLPAUSE_RECV;
    if (!applyReplyPauseMask(reply, desiredReplyPauseMask(reply))) {
        reply->internalPauseMask = oldInternalMask;
        return;
    }

    setReplyBackpressureActive(reply, false);
    if ((oldMask & CURLPAUSE_RECV) && ((reply->appliedPauseMask & CURLPAUSE_RECV) == 0)) {
        QCCurlMultiManager::instance()->wakeup();
    }
}

void resumeReplySendFromRequestBodySourceIfNeeded(QCNetworkReplyPrivate *reply)
{
    if (!reply || reply->executionMode != ExecutionMode::Async) {
        return;
    }
    if ((reply->internalPauseMask & CURLPAUSE_SEND) == 0 || replyIsTerminal(reply)) {
        return;
    }

    QIODevice *device = reply->requestBodySource.device.data();
    if (!device || !device->isReadable()) {
        return;
    }
    if (device->bytesAvailable() <= 0 && !device->atEnd()) {
        return;
    }

    const int oldMask = reply->appliedPauseMask;
    reply->internalPauseMask &= ~CURLPAUSE_SEND;
    if (!applyReplyPauseMask(reply, desiredReplyPauseMask(reply))) {
        reply->internalPauseMask |= CURLPAUSE_SEND;
        return;
    }

    setReplyUploadSendPaused(reply, false);
    if ((oldMask & CURLPAUSE_SEND) && ((reply->appliedPauseMask & CURLPAUSE_SEND) == 0)) {
        QCCurlMultiManager::instance()->wakeup();
    }
}

void scheduleReplyBackpressureResumeAfterRead(QCNetworkReply *reply,
                                              QCNetworkReplyPrivate *privateReply)
{
    if (!reply || !privateReply || privateReply->executionMode != ExecutionMode::Async) {
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
                privateReply->maybeResumeRecvFromBackpressure();
            }
        },
        Qt::QueuedConnection);
}

void clearReplyFlowControlOnTerminalState(QCNetworkReplyPrivate *reply)
{
    if (!reply) {
        return;
    }

    reply->internalPauseMask &= ~CURLPAUSE_RECV;
    reply->internalPauseMask &= ~CURLPAUSE_SEND;
    setReplyBackpressureActive(reply, false);
    setReplyUploadSendPaused(reply, false);
}

void pauseReplyTransport(QCNetworkReply *reply, QCNetworkReplyPrivate *privateReply, PauseMode mode)
{
    if (!privateReply || privateReply->executionMode == ExecutionMode::Sync) {
        qWarning() << "QCNetworkReply::pauseTransport: Sync mode does not support transfer pause/resume";
        return;
    }
    if (privateReply->state != ReplyState::Running) {
        return;
    }

    const int flags = pauseFlagsFromMode(mode);
    const int oldUserMask = privateReply->userPauseMask;
    privateReply->userPauseMask = flags;
    if (!shouldDeferRecvPauseToWriteCallback(flags)
        && !applyReplyPauseMask(privateReply, desiredReplyPauseMask(privateReply))) {
        privateReply->userPauseMask = oldUserMask;
        return;
    }

    privateReply->setState(ReplyState::Paused);
    Q_UNUSED(reply);
}

void resumeReplyTransport(QCNetworkReply *reply, QCNetworkReplyPrivate *privateReply)
{
    if (!privateReply || privateReply->executionMode == ExecutionMode::Sync) {
        qWarning() << "QCNetworkReply::resumeTransport: Sync mode does not support transfer pause/resume";
        return;
    }
    if (privateReply->state != ReplyState::Paused) {
        return;
    }

    const int oldUserMask = privateReply->userPauseMask;
    privateReply->userPauseMask = 0;
    if (!applyReplyPauseMask(privateReply, desiredReplyPauseMask(privateReply))) {
        privateReply->userPauseMask = oldUserMask;
        return;
    }

    privateReply->setState(ReplyState::Running);
    QCCurlMultiManager::instance()->wakeup();
    Q_UNUSED(reply);
}

} // namespace QCurl::Internal
