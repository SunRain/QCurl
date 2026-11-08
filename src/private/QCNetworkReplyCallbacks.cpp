/**
 * @file
 * @brief 将原生回调连接到生命周期、响应、请求体与流控模块。
 */

#include "private/QCNetworkReplyCallbacks_p.h"
#include "private/QCNetworkReplySignal_p.h"
#include "private/QCNetworkReplyTransferState_p.h"

#include <QByteArray>
#include <QDebug>
#include <QThread>

#include <limits>

namespace QCurl::Internal {
namespace {

thread_local int s_replyCurlCallbackDepth = 0;

bool callbackOnOwnerThread(const QPointer<QCNetworkReply> &observer)
{
    if (observer && QThread::currentThread() == observer->thread()) {
        return true;
    }
    qWarning() << "QCNetworkReply: callback must run on the reply owner thread";
    return false;
}

SignalEmissionResult activateBackpressureIfNeeded(QCNetworkReplyTransferState *state,
                                                  const QPointer<QCNetworkReply> &observer,
                                                  CURL *handle)
{
    const auto change = state->flow.afterReadyRead(handle,
                                                   state->response.bytesAvailable(),
                                                   state->lifecycle.isTerminal());
    state->receiveIdle.setReceivePaused((state->flow.desiredMask() & CURLPAUSE_RECV) != 0);
    if (change.error != CURLE_OK) {
        qWarning() << "QCCurlMultiTransferRecord: curl_easy_pause failed:"
                   << curl_easy_strerror(change.error) << "desiredMask=" << change.nativeMask;
    }
    if (change.backpressureChanged && !state->lifecycle.isTerminal()) {
        const bool active  = state->flow.backpressureActive();
        const qint64 bytes = state->response.bytesAvailable();
        const qint64 limit = state->flow.limitBytes();
        return emitReplySignal(observer, [active, bytes, limit](QCNetworkReply *reply) {
            Q_EMIT reply->backpressureStateChanged(active, bytes, limit);
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
    if (state->flow.pauseWriteIfRequested()) {
        state->receiveIdle.setReceivePaused(true);
        return CURL_WRITEFUNC_PAUSE;
    }
    if (totalSize > 0) {
        state->receiveIdle.receivedBytes();
    }

    state->response.appendBody(QByteArrayView(ptr, static_cast<qsizetype>(totalSize)));
    state->lifecycle.observeBodyChunk(static_cast<qint64>(totalSize));
    state->flow.observeBufferedBytes(state->response.bytesAvailable());
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
    const qint64 bytesDownloaded = state->lifecycle.bytesDownloaded();
    const qint64 downloadTotal   = state->lifecycle.downloadTotal();
    if (Internal::emitReplySignal(observer,
                                  [bytesDownloaded, downloadTotal](QCNetworkReply *reply) {
                                      Q_EMIT reply->downloadProgress(bytesDownloaded, downloadTotal);
                                  })
        == SignalEmissionResult::Destroyed) {
        return SignalEmissionResult::Destroyed;
    }

    const qint64 bytesUploaded = state->lifecycle.bytesUploaded();
    const qint64 uploadTotal   = state->lifecycle.uploadTotal();
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

size_t headerReplyCurlCallback(char *ptr,
                               size_t size,
                               size_t nmemb,
                               QCNetworkReplyTransferState *state)
{
    if (!state) {
        return 0;
    }
    const size_t totalSize = size * nmemb;
    if (!state->lifecycle.isTerminal()) {
        if (totalSize > 0) {
            state->receiveIdle.receivedBytes();
        }
        state->response.appendHeaders(QByteArrayView(ptr, static_cast<qsizetype>(totalSize)));
    }
    return totalSize;
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
    if (state->lifecycle.isTerminal()) {
        return totalSize;
    }
    if (!callbackOnOwnerThread(observer)) {
        return 0;
    }

    return writeAsyncReplyBody(state, observer, handle, ptr, totalSize);
}

size_t readReplyBodySourceCallback(char *ptr,
                                   size_t size,
                                   size_t nmemb,
                                   QCNetworkReplyTransferState *state,
                                   const QPointer<QCNetworkReply> &observer)
{
    ReplyCurlCallbackScope callbackScope;
    if (!state || !callbackOnOwnerThread(observer) || state->lifecycle.isTerminal()) {
        return CURL_READFUNC_ABORT;
    }
    const size_t capacity = size * nmemb;
    const auto result     = state->bodySource.readInto(
        ptr, static_cast<qint64>(qMin(capacity, size_t(std::numeric_limits<qint64>::max()))));
    switch (result.kind) {
        case ReplyBodyReadKind::Data:
            return static_cast<size_t>(result.bytes);
        case ReplyBodyReadKind::End:
            return 0;
        case ReplyBodyReadKind::Error:
            return CURL_READFUNC_ABORT;
        case ReplyBodyReadKind::Wait:
            if (state->flow.sourceWait() && emitReplySignal(observer, [](QCNetworkReply *reply) {
                                                Q_EMIT reply->uploadSendPausedChanged(true);
                                            }) == SignalEmissionResult::Destroyed) {
                return CURL_READFUNC_ABORT;
            }
            return CURL_READFUNC_PAUSE;
    }
    return CURL_READFUNC_ABORT;
}

int seekReplyBodySourceCallback(QCNetworkReplyTransferState *state, curl_off_t offset, int origin)
{
    return !state || state->lifecycle.isTerminal() ? CURL_SEEKFUNC_FAIL
                                                   : state->bodySource.seek(offset, origin);
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
    if (state->lifecycle.isTerminal()) {
        return 0;
    }
    if (!observer) {
        return 1;
    }

    state->receiveIdle.synchronizeConnection(state->activeEasyHandle);
    if (state->receiveIdle.expired()) {
        return 1;
    }
    state->lifecycle.observeNetworkProgress(static_cast<qint64>(dltotal),
                                            static_cast<qint64>(dlnow),
                                            static_cast<qint64>(ultotal),
                                            static_cast<qint64>(ulnow));

    if (emitAsyncProgress(state, observer) == SignalEmissionResult::Destroyed) {
        return 1;
    }

    return 0;
}

} // namespace QCurl::Internal
