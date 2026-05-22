/**
 * @file
 * @brief QCNetworkReply curl callbacks 与传输 pause/backpressure 实现。
 */

#include "private/QCNetworkReplyCallbacks_p.h"

#include "QCNetworkReply_p.h"
#include "private/QCNetworkReplyFlowControl_p.h"

#include <QByteArray>

namespace QCurl::Internal {
namespace {

thread_local int s_replyCurlCallbackDepth = 0;

void appendReplyBody(QCNetworkReplyPrivate *reply, char *ptr, size_t totalSize)
{
    reply->bodyBuffer.append(QByteArray(ptr, static_cast<int>(totalSize)));
    reply->bytesDownloaded += static_cast<qint64>(totalSize);
}

void updateBackpressurePeak(QCNetworkReplyPrivate *reply)
{
    if (reply->backpressureLimitBytes <= 0) {
        return;
    }

    reply->backpressurePeakBufferedBytes = qMax(reply->backpressurePeakBufferedBytes,
                                                reply->bodyBuffer.byteAmount());
}

void activateBackpressureIfNeeded(QCNetworkReplyPrivate *reply)
{
    if (reply->backpressureLimitBytes <= 0) {
        return;
    }
    if (reply->bodyBuffer.byteAmount() < reply->backpressureLimitBytes) {
        return;
    }
    if (reply->userPauseMask & CURLPAUSE_RECV) {
        return;
    }

    const bool alreadyPaused = (reply->internalPauseMask & CURLPAUSE_RECV) != 0;
    const int desiredBase = desiredReplyPauseMask(reply);
    const int desiredMask = alreadyPaused ? desiredBase : (desiredBase | CURLPAUSE_RECV);
    if (applyReplyPauseMask(reply, desiredMask) && !alreadyPaused) {
        reply->internalPauseMask |= CURLPAUSE_RECV;
        setReplyBackpressureActive(reply, true);
    }
}

size_t writeAsyncReplyBody(QCNetworkReplyPrivate *reply, char *ptr, size_t totalSize)
{
    if ((reply->userPauseMask & CURLPAUSE_RECV)
        && ((reply->appliedPauseMask & CURLPAUSE_RECV) == 0)) {
        reply->appliedPauseMask |= CURLPAUSE_RECV;
        return CURL_WRITEFUNC_PAUSE;
    }

    appendReplyBody(reply, ptr, totalSize);
    updateBackpressurePeak(reply);
    emit reply->q_ptr->readyRead();
    activateBackpressureIfNeeded(reply);
    return totalSize;
}

void emitAsyncProgress(QCNetworkReplyPrivate *reply)
{
    emit reply->q_ptr->downloadProgress(reply->bytesDownloaded, reply->downloadTotal);
    emit reply->q_ptr->uploadProgress(reply->bytesUploaded, reply->uploadTotal);
}

} // namespace

ReplyCurlCallbackScope::ReplyCurlCallbackScope()
{
    ++s_replyCurlCallbackDepth;
}

ReplyCurlCallbackScope::~ReplyCurlCallbackScope()
{
    --s_replyCurlCallbackDepth;
}

bool isInReplyCurlCallback() noexcept
{
    return s_replyCurlCallbackDepth > 0;
}

size_t writeReplyCurlCallback(char *ptr, size_t size, size_t nmemb, QCNetworkReplyPrivate *reply)
{
    ReplyCurlCallbackScope callbackScope;

    if (!reply || !reply->q_ptr) {
        return 0;
    }

    const size_t totalSize = size * nmemb;
    if (reply->state == ReplyState::Cancelled || reply->state == ReplyState::Error) {
        return totalSize;
    }

    return writeAsyncReplyBody(reply, ptr, totalSize);
}

int progressReplyCurlCallback(QCNetworkReplyPrivate *reply,
                              curl_off_t dltotal,
                              curl_off_t dlnow,
                              curl_off_t ultotal,
                              curl_off_t ulnow)
{
    ReplyCurlCallbackScope callbackScope;

    if (!reply || !reply->q_ptr) {
        return 1;
    }
    if (reply->state == ReplyState::Cancelled || reply->state == ReplyState::Error) {
        return 0;
    }

    reply->downloadTotal = static_cast<qint64>(dltotal);
    reply->bytesDownloaded = static_cast<qint64>(dlnow);
    reply->uploadTotal = static_cast<qint64>(ultotal);
    reply->bytesUploaded = static_cast<qint64>(ulnow);

    emitAsyncProgress(reply);

    return 0;
}


} // namespace QCurl::Internal
