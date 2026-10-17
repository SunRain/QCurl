/**
 * @file
 * @brief QCNetworkReply retry/capability runtime helpers.
 */

#include "QCNetworkReply_p.h"
#include "QCNetworkRetryPolicy.h"
#include "private/QCNetworkReplyResponse_p.h"
#include "private/QCNetworkReplyRuntime_p.h"
#include "private/QCNetworkRetryDecision_p.h"

#include <QDebug>
#include <QTimer>

#include <ctime>

namespace QCurl::Internal {

bool isReplyCapabilityRelatedCurlError(CURLcode code) noexcept
{
    return code == CURLE_UNKNOWN_OPTION || code == CURLE_NOT_BUILT_IN;
}

void appendReplyCapabilityWarning(QCNetworkReplyPrivate *reply, const QString &message)
{
    if (!reply) {
        return;
    }

    reply->capabilityWarnings.append(message);
    // 同时写入 capabilityWarnings 与日志，便于测试断言和现场排障复用同一事实源。
    qWarning() << "QCNetworkReply capability warning:" << message;
}

namespace {

bool exceedsRetryBudget(std::chrono::milliseconds used,
                        std::chrono::milliseconds next,
                        std::optional<std::chrono::milliseconds> limit)
{
    return limit && (used > *limit || next > *limit - used);
}

std::optional<std::chrono::milliseconds> retryDelayWithinBudget(QCNetworkReplyPrivate *reply,
                                                                const QCNetworkRetryPolicy &policy,
                                                                NetworkError error)
{
    std::optional<std::chrono::milliseconds> retryAfter;
    if (error == NetworkError::HttpTooManyRequests) {
        reply->parseHeaders();
        retryAfter = parseReplyRetryAfterDelay(reply->finalHeaderList, std::time(nullptr));
    }
    const auto delay   = policy.delayForAttempt(reply->attemptCount, retryAfter);
    const auto elapsed = std::chrono::milliseconds(
        reply->elapsedTimerStarted ? reply->elapsedTimer.elapsed() : 0);
    if (exceedsRetryBudget(reply->cumulativeRetryDelay, delay, policy.maxCumulativeDelay())
        || exceedsRetryBudget(elapsed, delay, policy.maxElapsedTime())) {
        return std::nullopt;
    }
    return delay;
}

} // namespace

ReplyRetryAdvanceResult advanceReplyRetryIfNeeded(QCNetworkReplyPrivate *d, NetworkError error)
{
    if (!d) {
        return {};
    }

    const QCNetworkRetryPolicy policy = d->request.retryPolicy();
    const bool bodyReplayable = !d->requestBodySource.device || d->requestBodySource.seekable;
    const auto decision       = Internal::QCNetworkRetryDecision::evaluate(policy,
                                                                           d->httpMethod,
                                                                           d->request,
                                                                           bodyReplayable,
                                                                           error,
                                                                           d->attemptCount);
    if (!decision.allowed) {
        return {};
    }
    if (d->responseBodyDelivered && !d->restoreResponseBeforeRetry) {
        return {};
    }
    const auto delay = retryDelayWithinBudget(d, policy, error);
    if (!delay.has_value()) {
        return {};
    }
    // 预算拒绝不得截断已写出的响应；只有决定重试后才回滚本次输出。
    if (d->restoreResponseBeforeRetry) {
        if (const auto outputError = d->restoreResponseBeforeRetry(); outputError.has_value()) {
            d->setError(NetworkError::OutputDeviceError, outputError.value());
            return {d->setState(ReplyState::Error), std::nullopt};
        }
    }

    d->attemptCount++;
    const auto remaining = std::chrono::milliseconds::max() - d->cumulativeRetryDelay;
    d->cumulativeRetryDelay += std::min(delay.value(), remaining);
    const int attemptCount = d->attemptCount;
    const QPointer<QCNetworkReply> observer(d->qObject());
    if (emitReplySignal(observer,
                        [attemptCount, error](QCNetworkReply *reply) {
                            Q_EMIT reply->retryAttempt(attemptCount, error);
                        })
        == SignalEmissionResult::Destroyed) {
        return {SignalEmissionResult::Destroyed, std::nullopt};
    }

    return {SignalEmissionResult::Alive, delay};
}

void resetReplyForRetry(QCNetworkReplyPrivate *d, bool setIdleState)
{
    if (!d) {
        return;
    }

    d->diagnosticCurlCode = 0;
    if (setIdleState) {
        d->state     = ReplyState::Idle;
        d->errorCode = NetworkError::NoError;
        d->errorMessage.clear();
    }

    d->bodyBuffer.clear();
    d->responseBodyDelivered = false;
    d->cacheBodyBuffer.clear();
    d->headerData.clear();
    d->finalHeaderList.clear();
    d->finalHeaderMap.clear();
    d->bytesDownloaded = 0;
    d->bytesUploaded   = 0;
    d->downloadTotal   = -1;
    d->uploadTotal     = -1;
    d->httpStatusCode  = 0;
}

void scheduleAsyncReplyRetry(QPointer<QCNetworkReply> safeReply,
                             QCNetworkReplyPrivate *d,
                             std::chrono::milliseconds delay)
{
    if (!safeReply || !d) {
        return;
    }

    QTimer::singleShot(delay, safeReply.data(), [safeReply, d]() {
        if (!safeReply) {
            return;
        }

        if (d->state == ReplyState::Cancelled || d->state == ReplyState::Finished
            || d->state == ReplyState::Error) {
            return;
        }

        resetReplyForRetry(d, true);
        safeReply->execute();
    });
}

} // namespace QCurl::Internal
