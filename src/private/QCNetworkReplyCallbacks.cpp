/**
 * @file
 * @brief QCNetworkReply curl callbacks 与传输 pause/backpressure 实现。
 */

#include "private/QCNetworkReplyCallbacks_p.h"
#include "private/QCNetworkReplySignal_p.h"
#include "private/QCNetworkReplyTransferState_p.h"

#include <QByteArray>
#include <QDebug>

namespace QCurl::Internal {
namespace {

thread_local int s_replyCurlCallbackDepth = 0;

[[nodiscard]] bool isTerminalState(ReplyState state)
{
    return state == ReplyState::Cancelled || state == ReplyState::Error
           || state == ReplyState::Finished;
}

void appendReplyBody(QCNetworkReplyTransferState *state, char *ptr, size_t totalSize)
{
    const QByteArray chunk(ptr, static_cast<int>(totalSize));
    state->bodyBuffer.append(chunk);
    if (state->cacheBodyLimit >= 0) {
        if (chunk.size() <= state->cacheBodyLimit - state->cacheBodyBuffer.size()) {
            state->cacheBodyBuffer.append(chunk);
        } else {
            state->cacheBodyBuffer = QByteArray();
            state->cacheBodyLimit  = -1;
        }
    }
    state->bytesDownloaded += static_cast<qint64>(totalSize);
}

void updateBackpressurePeak(QCNetworkReplyTransferState *state)
{
    if (state->backpressureLimitBytes <= 0) {
        return;
    }

    state->backpressurePeakBufferedBytes = qMax(state->backpressurePeakBufferedBytes,
                                                state->bodyBuffer.byteAmount());
}

[[nodiscard]] bool applyCallbackPauseMask(QCNetworkReplyTransferState *state,
                                          CURL *handle,
                                          int desiredMask)
{
    if (!handle) {
        return false;
    }

    const int normalized = desiredMask & (CURLPAUSE_RECV | CURLPAUSE_SEND);
    if (normalized == state->appliedPauseMask) {
        return true;
    }

    const CURLcode result = curl_easy_pause(handle, normalized == 0 ? CURLPAUSE_CONT : normalized);
    if (result != CURLE_OK) {
        qWarning() << "QCCurlMultiTransferRecord: curl_easy_pause failed:"
                   << curl_easy_strerror(result) << "desiredMask=" << normalized;
        return false;
    }
    state->appliedPauseMask = normalized;
    return true;
}

SignalEmissionResult activateBackpressureIfNeeded(QCNetworkReplyTransferState *state,
                                                  const QPointer<QCNetworkReply> &observer,
                                                  CURL *handle)
{
    if (state->backpressureLimitBytes <= 0
        || state->bodyBuffer.byteAmount() < state->backpressureLimitBytes
        || (state->userPauseMask & CURLPAUSE_RECV)) {
        return SignalEmissionResult::Alive;
    }

    const bool alreadyPaused = (state->internalPauseMask & CURLPAUSE_RECV) != 0;
    const int desiredBase    = state->userPauseMask | state->internalPauseMask;
    const int desiredMask    = alreadyPaused ? desiredBase : (desiredBase | CURLPAUSE_RECV);
    if (applyCallbackPauseMask(state, handle, desiredMask) && !alreadyPaused) {
        state->internalPauseMask |= CURLPAUSE_RECV;
        state->backpressureActive  = true;
        const qint64 bufferedBytes = state->bodyBuffer.byteAmount();
        const qint64 limitBytes    = state->backpressureLimitBytes;
        return Internal::emitReplySignal(observer,
                                         [bufferedBytes, limitBytes](QCNetworkReply *reply) {
                                             Q_EMIT reply->backpressureStateChanged(true,
                                                                                    bufferedBytes,
                                                                                    limitBytes);
                                         });
    }
    return SignalEmissionResult::Alive;
}

size_t writeAsyncReplyBody(QCNetworkReplyTransferState *state,
                           const QPointer<QCNetworkReply> &observer,
                           CURL *handle,
                           char *ptr,
                           size_t totalSize)
{
    if ((state->userPauseMask & CURLPAUSE_RECV)
        && ((state->appliedPauseMask & CURLPAUSE_RECV) == 0)) {
        state->appliedPauseMask |= CURLPAUSE_RECV;
        return CURL_WRITEFUNC_PAUSE;
    }

    appendReplyBody(state, ptr, totalSize);
    updateBackpressurePeak(state);
    const auto readyReadResult = Internal::emitReplySignal(observer, [](QCNetworkReply *reply) {
        Q_EMIT reply->readyRead();
    });
    if (readyReadResult == SignalEmissionResult::Destroyed) {
        return 0;
    }
    if (activateBackpressureIfNeeded(state, observer, handle) == SignalEmissionResult::Destroyed) {
        return 0;
    }
    return totalSize;
}

SignalEmissionResult emitAsyncProgress(QCNetworkReplyTransferState *state,
                                       const QPointer<QCNetworkReply> &observer)
{
    const qint64 bytesDownloaded = state->bytesDownloaded;
    const qint64 downloadTotal   = state->downloadTotal;
    if (Internal::emitReplySignal(observer,
                                  [bytesDownloaded, downloadTotal](QCNetworkReply *reply) {
                                      Q_EMIT reply->downloadProgress(bytesDownloaded, downloadTotal);
                                  })
        == SignalEmissionResult::Destroyed) {
        return SignalEmissionResult::Destroyed;
    }

    const qint64 bytesUploaded = state->bytesUploaded;
    const qint64 uploadTotal   = state->uploadTotal;
    return Internal::emitReplySignal(observer, [bytesUploaded, uploadTotal](QCNetworkReply *reply) {
        Q_EMIT reply->uploadProgress(bytesUploaded, uploadTotal);
    });
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

size_t writeReplyCurlCallback(char *ptr,
                              size_t size,
                              size_t nmemb,
                              QCNetworkReplyTransferState *state,
                              const QPointer<QCNetworkReply> &observer,
                              CURL *handle)
{
    ReplyCurlCallbackScope callbackScope;

    if (!state) {
        return 0;
    }

    const size_t totalSize = size * nmemb;
    if (isTerminalState(state->state)) {
        return totalSize;
    }
    if (!observer) {
        return 0;
    }

    return writeAsyncReplyBody(state, observer, handle, ptr, totalSize);
}

int progressReplyCurlCallback(QCNetworkReplyTransferState *state,
                              const QPointer<QCNetworkReply> &observer,
                              curl_off_t dltotal,
                              curl_off_t dlnow,
                              curl_off_t ultotal,
                              curl_off_t ulnow)
{
    ReplyCurlCallbackScope callbackScope;

    if (!state) {
        return 1;
    }
    if (isTerminalState(state->state)) {
        return 0;
    }
    if (!observer) {
        return 1;
    }

    state->downloadTotal   = static_cast<qint64>(dltotal);
    state->bytesDownloaded = static_cast<qint64>(dlnow);
    state->uploadTotal     = static_cast<qint64>(ultotal);
    state->bytesUploaded   = static_cast<qint64>(ulnow);

    if (emitAsyncProgress(state, observer) == SignalEmissionResult::Destroyed) {
        return 1;
    }

    return 0;
}

} // namespace QCurl::Internal
