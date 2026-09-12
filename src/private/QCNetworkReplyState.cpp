/**
 * @file
 * @brief QCNetworkReply private state transitions and curl callbacks.
 */

#include "QCCurlMultiManager.h"
#include "QCNetworkAccessManager.h"
#include "QCNetworkCache.h"
#include "QCNetworkCachePolicy.h"
#include "QCNetworkConnectionPoolManager_p.h"
#include "QCNetworkLogger.h"
#include "QCNetworkReply_p.h"
#include "private/QCNetworkCacheIntegration_p.h"
#include "private/QCNetworkReplyBodySource_p.h"
#include "private/QCNetworkReplyCache_p.h"
#include "private/QCNetworkReplyCallbacks_p.h"
#include "private/QCNetworkReplyExecution_p.h"
#include "private/QCNetworkReplyFlowControl_p.h"
#include "private/QCNetworkReplyResponse_p.h"
#include "private/QCNetworkReplyRuntime_p.h"

#include <QPointer>

namespace QCurl {

namespace {

constexpr int kCookieWriteMode = static_cast<int>(QCNetworkAccessManager::WriteOnly);

[[nodiscard]] bool isTerminalState(ReplyState state)
{
    return state == ReplyState::Finished || state == ReplyState::Error
           || state == ReplyState::Cancelled;
}

[[nodiscard]] QPointer<QCNetworkReply> callbackObserver(QCNetworkReplyPrivate *reply)
{
    return reply ? QPointer<QCNetworkReply>(reply->qObject()) : QPointer<QCNetworkReply>();
}

[[nodiscard]] ReplyState resolveFinishTransition(QCNetworkReplyPrivate *reply,
                                                 ReplyState requestedState)
{
    if (requestedState != ReplyState::Finished || !reply->beforeFinishTransition) {
        return requestedState;
    }
    if (const auto error = reply->beforeFinishTransition(); error.has_value()) {
        reply->setError(NetworkError::InvalidRequest, error.value());
        requestedState = ReplyState::Error;
    }
    reply->beforeFinishTransition = nullptr;
    return requestedState;
}

void updateElapsedTime(QCNetworkReplyPrivate *reply, ReplyState newState)
{
    if (newState == ReplyState::Running && !reply->elapsedTimerStarted) {
        reply->elapsedTimer.start();
        reply->elapsedTimerStarted = true;
        reply->durationMs          = -1;
        return;
    }
    if (isTerminalState(newState) && reply->elapsedTimerStarted && reply->durationMs < 0) {
        reply->durationMs = reply->elapsedTimer.elapsed();
    }
}

void flushCookieJar(QCNetworkReplyPrivate *reply)
{
    CURL *handle = reply->curlManager.handle();
    if (!handle || !(reply->cookieMode & kCookieWriteMode) || reply->cookieFilePath.isEmpty()) {
        return;
    }

    const CURLcode result = curl_easy_setopt(handle, CURLOPT_COOKIELIST, "FLUSH");
    if (result == CURLE_OK) {
        return;
    }
    if (Internal::isReplyCapabilityRelatedCurlError(result)) {
        Internal::appendReplyCapabilityWarning(
            reply,
            QStringLiteral("libcurl 不支持 Cookie flush（CURLOPT_COOKIELIST，%1），CookieJAR "
                           "可能不会立即落盘")
                .arg(QString::fromUtf8(curl_easy_strerror(result))));
        return;
    }
    Internal::appendReplyCapabilityWarning(reply,
                                           QStringLiteral("Cookie flush 失败（%1）")
                                               .arg(QString::fromUtf8(curl_easy_strerror(result))));
}

Internal::SignalEmissionResult completeFinishedReply(QCNetworkReplyPrivate *reply,
                                                     const QPointer<QCNetworkReply> &observer)
{
    flushCookieJar(reply);
    Internal::storeReplyInCache(reply);
    reply->cacheBodyBuffer = QByteArray();
    return Internal::emitReplySignal(observer, [](QCNetworkReply *q) { Q_EMIT q->finished(); });
}

Internal::SignalEmissionResult emitTerminalSignals(QCNetworkReplyPrivate *reply,
                                                   const QPointer<QCNetworkReply> &observer,
                                                   ReplyState newState)
{
    if (newState == ReplyState::Finished) {
        return completeFinishedReply(reply, observer);
    } else if (newState == ReplyState::Error) {
        const NetworkError errorCode = reply->errorCode;
        if (Internal::emitReplySignal(observer,
                                      [errorCode](QCNetworkReply *q) { Q_EMIT q->error(errorCode); })
            == Internal::SignalEmissionResult::Destroyed) {
            return Internal::SignalEmissionResult::Destroyed;
        }
        return Internal::emitReplySignal(observer, [](QCNetworkReply *q) { Q_EMIT q->finished(); });
    } else if (newState == ReplyState::Cancelled) {
        if (Internal::emitReplySignal(observer, [](QCNetworkReply *q) { Q_EMIT q->cancelled(); })
            == Internal::SignalEmissionResult::Destroyed) {
            return Internal::SignalEmissionResult::Destroyed;
        }
        return Internal::emitReplySignal(observer, [](QCNetworkReply *q) { Q_EMIT q->finished(); });
    }
    return Internal::SignalEmissionResult::Alive;
}

} // namespace

Internal::SignalEmissionResult QCNetworkReplyPrivate::setState(ReplyState newState)
{
    Q_Q(QCNetworkReply);
    const QPointer<QCNetworkReply> observer(q);

    if (state == newState || isTerminalState(state)) {
        return Internal::SignalEmissionResult::Alive;
    }

    newState = resolveFinishTransition(this, newState);
    if (newState == ReplyState::Finished) {
        if (Internal::restoreRevalidatedCacheResponse(this)
            == Internal::SignalEmissionResult::Destroyed) {
            return Internal::SignalEmissionResult::Destroyed;
        }
    }
    state = newState;
    updateElapsedTime(this, newState);

    if (isTerminalState(newState)) {
        finishPoolRequest();
        if (newState != ReplyState::Finished) {
            cacheBodyBuffer = QByteArray();
        }
        if (Internal::clearReplyFlowControlOnTerminalState(this)
            == Internal::SignalEmissionResult::Destroyed) {
            return Internal::SignalEmissionResult::Destroyed;
        }
    }
    if (newState == ReplyState::Finished || newState == ReplyState::Error) {
        parseHeaders();
    }
    if (Internal::emitReplySignal(observer,
                                  [newState](QCNetworkReply *reply) {
                                      Q_EMIT reply->stateChanged(newState);
                                  })
        == Internal::SignalEmissionResult::Destroyed) {
        return Internal::SignalEmissionResult::Destroyed;
    }

    return emitTerminalSignals(this, observer, newState);
}

void QCNetworkReplyPrivate::setError(NetworkError error, const QString &message)
{
    errorCode    = error;
    errorMessage = message;
}

void QCNetworkReplyPrivate::parseHeaders()
{
    Internal::parseReplyHeaders(this);
}

bool QCNetworkReplyPrivate::applyPauseMask(int desiredMask)
{
    return Internal::applyReplyPauseMask(this, desiredMask);
}

int QCNetworkReplyPrivate::desiredPauseMask() const
{
    return Internal::desiredReplyPauseMask(this);
}

Internal::SignalEmissionResult QCNetworkReplyPrivate::setBackpressureActive(bool active)
{
    return Internal::setReplyBackpressureActive(this, active);
}

Internal::SignalEmissionResult QCNetworkReplyPrivate::setUploadSendPaused(bool paused)
{
    return Internal::setReplyUploadSendPaused(this, paused);
}

Internal::SignalEmissionResult QCNetworkReplyPrivate::maybeResumeRecvFromBackpressure()
{
    return Internal::maybeResumeReplyRecvFromBackpressure(this);
}

Internal::SignalEmissionResult QCNetworkReplyPrivate::resumeSendFromRequestBodySourceIfNeeded()
{
    return Internal::resumeReplySendFromRequestBodySourceIfNeeded(this);
}

// ==================
// Curl 静态回调函数实现
// ==================

size_t QCNetworkReplyPrivate::curlWriteCallback(char *ptr, size_t size, size_t nmemb, void *userdata)
{
    auto *reply = static_cast<QCNetworkReplyPrivate *>(userdata);
    return Internal::writeReplyCurlCallback(ptr,
                                            size,
                                            nmemb,
                                            reply ? reply->transferState.data() : nullptr,
                                            callbackObserver(reply),
                                            reply ? reply->activeCurlHandle() : nullptr);
}

size_t QCNetworkReplyPrivate::curlHeaderCallback(char *ptr,
                                                 size_t size,
                                                 size_t nmemb,
                                                 void *userdata)
{
    auto *reply = static_cast<QCNetworkReplyPrivate *>(userdata);
    return Internal::headerReplyCurlCallback(ptr,
                                             size,
                                             nmemb,
                                             reply ? reply->transferState.data() : nullptr);
}

size_t QCNetworkReplyPrivate::curlReadCallback(char *ptr, size_t size, size_t nmemb, void *userdata)
{
    Internal::ReplyCurlCallbackScope callbackScope;
    auto *reply = static_cast<QCNetworkReplyPrivate *>(userdata);

    return Internal::readReplyBodySourceCallback(ptr,
                                                 size,
                                                 nmemb,
                                                 reply ? reply->transferState.data() : nullptr,
                                                 callbackObserver(reply));
}

int QCNetworkReplyPrivate::curlSeekCallback(void *userdata, curl_off_t offset, int origin)
{
    auto *reply = static_cast<QCNetworkReplyPrivate *>(userdata);
    return Internal::seekReplyBodySourceCallback(reply ? reply->transferState.data() : nullptr,
                                                 offset,
                                                 origin);
}

int QCNetworkReplyPrivate::curlProgressCallback(
    void *userdata, curl_off_t dltotal, curl_off_t dlnow, curl_off_t ultotal, curl_off_t ulnow)
{
    auto *reply = static_cast<QCNetworkReplyPrivate *>(userdata);
    return Internal::progressReplyCurlCallback(reply ? reply->transferState.data() : nullptr,
                                               callbackObserver(reply),
                                               dltotal,
                                               dlnow,
                                               ultotal,
                                               ulnow);
}

int QCNetworkReplyPrivate::curlDebugCallback(
    CURL *handle, curl_infotype type, char *data, size_t size, void *userptr)
{
    Q_UNUSED(handle);

    auto *d = static_cast<QCNetworkReplyPrivate *>(userptr);
    if (!d || !d->q_ptr) {
        return 0;
    }

    if (d->state == ReplyState::Cancelled || d->state == ReplyState::Error) {
        return 0;
    }

    if (!d->debugTraceEnabled) {
        return 0;
    }

    const auto logger = d->logger;
    if (!logger) {
        return 0;
    }

    const QByteArray raw  = QByteArray(data, static_cast<int>(size));
    const QString message = Internal::formatReplyDebugTraceMessage(type, raw);
    if (message.isEmpty()) {
        return 0;
    }

    static_cast<void>(logger->log(NetworkLogLevel::Debug, QStringLiteral("Trace"), message));
    return 0;
}

void QCNetworkReplyPrivate::onCurlMultiFinished(CURLcode curlCode, long httpStatusCode)
{
    Q_Q(QCNetworkReply);

    // 已取消/已错误：保持既有可观测语义，不允许完成回调覆盖状态
    if (transferRemovalRequested || state == ReplyState::Cancelled || state == ReplyState::Error
        || state == ReplyState::Finished) {
        return;
    }

    const auto info = Internal::attemptErrorFromCurlAndHttp(this, curlCode, httpStatusCode);

    if (info.error == NetworkError::NoError) {
        Q_UNUSED(setState(ReplyState::Finished));
        return;
    }

    const auto retry = Internal::advanceReplyRetryIfNeeded(this, info.error);
    if (retry.emissionResult == Internal::SignalEmissionResult::Destroyed) {
        return;
    }
    if (retry.delay.has_value()) {
        Internal::scheduleAsyncReplyRetry(QPointer<QCNetworkReply>(q), this, retry.delay.value());
        return;
    }
    if (Internal::QCNetworkReplyExecution::tryPreferNetworkCacheFallback(q)) {
        return;
    }

    setError(info.error, info.message);
    Q_UNUSED(setState(ReplyState::Error));
}

} // namespace QCurl
