/**
 * @file
 * @brief 编排重试输出恢复、信号、等待与 capability 发布。
 */

#include "QCNetworkReply_p.h"
#include "private/QCNetworkReplyResponse_p.h"
#include "private/QCNetworkReplyRuntime_p.h"
#include "private/QCReplyRetry_p.h"

#include <QDebug>
#include <QTimer>

#include <ctime>

namespace QCurl::Internal {

void appendReplyCapabilityWarning(QCNetworkReplyPrivate *reply, const QString &message)
{
    if (!reply) {
        return;
    }

    reply->transferState->lifecycle.appendWarnings({message});
    // 同时写入 capabilityWarnings 与日志，便于测试断言和现场排障复用同一事实源。
    qWarning() << "QCNetworkReply capability warning:" << message;
}

ReplyRetryAdvanceResult advanceReplyRetryIfNeeded(QCNetworkReplyPrivate *d, NetworkError error)
{
    if (!d) {
        return {};
    }
    const auto holder = d->transferState;
    ReplyRetryAttemptFacts facts;
    facts.error            = error;
    facts.bodyReplayable   = holder->bodySource.replayable();
    facts.bodyDelivered    = holder->response.bodyDelivered();
    facts.outputRestorable = bool(d->restoreResponseBeforeRetry);
    facts.elapsed          = std::chrono::milliseconds(holder->lifecycle.elapsedMs());
    if (error == NetworkError::HttpTooManyRequests) {
        holder->response.parseHeaders();
        facts.retryAfter = holder->response.retryAfterDelay(std::time(nullptr));
    }
    const auto proposal = d->retry.evaluate(d->request.retryPolicy(),
                                            d->httpMethod,
                                            d->request,
                                            facts);
    if (!proposal) {
        return {};
    }
    const QPointer<QCNetworkReply> observer(d->qObject());
    const auto restoreOutput = d->restoreResponseBeforeRetry;
    const auto outputError   = restoreOutput ? restoreOutput() : std::nullopt;
    if (!observer) {
        return {SignalEmissionResult::Destroyed, std::nullopt};
    }
    if (holder->lifecycle.isTerminal()) {
        return {};
    }
    if (outputError) {
        d->setError(NetworkError::OutputDeviceError, *outputError);
        return {d->setState(ReplyState::Error), std::nullopt};
    }
    d->retry.commit(*proposal);
    const int attemptCount = d->retry.attemptCount();
    if (emitReplySignal(observer,
                        [attemptCount, error](QCNetworkReply *reply) {
                            Q_EMIT reply->retryAttempt(attemptCount, error);
                        })
        == SignalEmissionResult::Destroyed) {
        return {SignalEmissionResult::Destroyed, std::nullopt};
    }
    return {SignalEmissionResult::Alive,
            holder->lifecycle.isTerminal() ? std::nullopt : std::optional(proposal->delay)};
}

void resetReplyForRetry(QCNetworkReplyPrivate *d, bool setIdleState)
{
    if (!d) {
        return;
    }

    const auto holder = d->transferState;
    if (holder->lifecycle.isTerminal()) {
        return;
    }
    // 回放只清观察事实；重试才允许回到 Idle，跨 attempt 的时钟和 warning 不清零。
    holder->trace.stop();
    if (setIdleState) {
        holder->receiveIdle.reset();
        holder->lifecycle.resetForRetry();
    } else {
        holder->lifecycle.resetReplayObservations();
    }
    holder->response.resetForAttempt();
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

        if (d->transferState->lifecycle.isTerminal()) {
            return;
        }

        resetReplyForRetry(d, true);
        safeReply->execute();
    });
}

} // namespace QCurl::Internal
