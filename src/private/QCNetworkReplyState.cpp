/**
 * @file
 * @brief 编排 QCNetworkReply 终态发布与原生回调适配。
 */

#include "QCCurlMultiManager.h"
#include "QCNetworkLogger.h"
#include "QCNetworkReply_p.h"
#include "private/QCNetworkReplyBodySource_p.h"
#include "private/QCNetworkReplyCallbacks_p.h"
#include "private/QCNetworkReplyConfigurationExecution_p.h"
#include "private/QCNetworkReplyExecution_p.h"
#include "private/QCNetworkReplyFlowControlExecution_p.h"
#include "private/QCNetworkReplyProperties_p.h"
#include "private/QCNetworkReplyResponse_p.h"
#include "private/QCNetworkReplyRuntime_p.h"

#include <QPointer>

namespace QCurl {

namespace {

[[nodiscard]] QPointer<QCNetworkReply> callbackObserver(QCNetworkReplyPrivate *reply)
{
    return reply ? QPointer<QCNetworkReply>(reply->qObject()) : QPointer<QCNetworkReply>();
}

Internal::SignalEmissionResult completeFinishedReply(QCNetworkReplyPrivate *reply,
                                                     const QPointer<QCNetworkReply> &observer)
{
    Q_UNUSED(Internal::publishReplyCurlConfigurationResult(
        reply, reply->transferState->configuration.flushCookieJar(reply->curlManager.handle())));
    Internal::QCNetworkReplyExecution::storeCacheResponse(reply->qObject());
    reply->transferState->response.releaseCacheCandidate();
    return Internal::emitReplySignal(observer, [](QCNetworkReply *q) { Q_EMIT q->finished(); });
}

Internal::SignalEmissionResult emitTerminalSignals(QCNetworkReplyPrivate *reply,
                                                   const QPointer<QCNetworkReply> &observer,
                                                   ReplyState newState)
{
    if (newState == ReplyState::Finished) {
        return completeFinishedReply(reply, observer);
    } else if (newState == ReplyState::Error) {
        const NetworkError errorCode = reply->transferState->lifecycle.error();
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

    const auto holder = transferState;
    auto &lifecycle   = holder->lifecycle;
    if (lifecycle.state() == newState || lifecycle.isTerminal()) {
        return Internal::SignalEmissionResult::Alive;
    }

    if (newState == ReplyState::Finished) {
        if (Internal::QCNetworkReplyExecution::restoreRevalidatedCacheResponse(q)
            == Internal::SignalEmissionResult::Destroyed) {
            return Internal::SignalEmissionResult::Destroyed;
        }
    }
    // 304 恢复会发布 readyRead；同步取消已经完成请求时不能再次提交 Finished。
    if (!lifecycle.transition(newState)) {
        return Internal::SignalEmissionResult::Alive;
    }
    if (lifecycle.isTerminal()) {
        holder->trace.stop();
        holder->transportDomain.reset();
        finishPoolRequest();
        if (newState != ReplyState::Finished) {
            holder->response.releaseCacheCandidate();
        }
        if (Internal::clearReplyFlowControlOnTerminalState(this)
            == Internal::SignalEmissionResult::Destroyed) {
            return Internal::SignalEmissionResult::Destroyed;
        }
    }
    if (newState == ReplyState::Finished || newState == ReplyState::Error) {
        holder->response.parseHeaders();
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
    transferState->lifecycle.recordError(error, message);
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
    auto *d = static_cast<QCNetworkReplyPrivate *>(userptr);
    if (!d || !d->q_ptr) {
        return 0;
    }

    if (d->transferState->lifecycle.isTerminal()) {
        return 0;
    }

    if (!d->debugTraceEnabled) {
        return 0;
    }

    const auto logger = d->logger;
    if (!logger) {
        return 0;
    }

    const auto callback
        = Internal::inspectTraceCallback(handle,
                                         d->curlManager.handle(),
                                         QByteArrayView(data, static_cast<qsizetype>(size)));
    const QString message = d->transferState->trace.consume(type, callback);
    if (message.isEmpty()) {
        return 0;
    }

    static_cast<void>(logger->log(NetworkLogLevel::Debug, QStringLiteral("Trace"), message));
    return 0;
}

void QCNetworkReplyPrivate::onCurlMultiFinished(CURLcode curlCode, long httpStatusCode)
{
    Q_Q(QCNetworkReply);

    const auto holder = transferState;
    auto &lifecycle   = holder->lifecycle;
    if (transferRemovalRequested || lifecycle.isTerminal()) {
        return;
    }

    auto &response = holder->response;
    response.commitStatus(static_cast<int>(httpStatusCode));
    response.parseHeaders();
    bool ok                   = false;
    const qint64 existingSize = q->property(Internal::replyproperties::kResumableExistingSize)
                                    .toLongLong(&ok);
    const auto info = lifecycle.classifyNetworkResult(curlCode,
                                                      response.status(),
                                                      holder->bodySource.failure(),
                                                      holder->bodySource.device() != nullptr,
                                                      ok
                                                          && response.rangeAlreadyComplete(
                                                              existingSize));

    if (info.error == NetworkError::NoError) {
        Q_UNUSED(setState(ReplyState::Finished));
        return;
    }

    const auto retry = Internal::advanceReplyRetryIfNeeded(this, info.error);
    if (retry.emissionResult == Internal::SignalEmissionResult::Destroyed
        || lifecycle.isTerminal()) {
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
