/**
 * @file
 * @brief QCNetworkReply retry/capability runtime helpers.
 */

#include "QCNetworkReply_p.h"
#include "QCNetworkRetryPolicy.h"
#include "private/QCNetworkReplyRuntime_p.h"
#include "private/QCNetworkRetryDecision_p.h"

#include <QDebug>
#include <QTimer>

#include <cmath>
#include <ctime>
#include <limits>

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

std::optional<std::chrono::milliseconds> parseReplyRetryAfterDelay(
    const QMap<QString, QString> &headers)
{
    for (auto it = headers.cbegin(); it != headers.cend(); ++it) {
        if (it.key().compare(QStringLiteral("Retry-After"), Qt::CaseInsensitive) != 0) {
            continue;
        }

        const QString raw = it.value().trimmed();
        if (raw.isEmpty()) {
            qWarning() << "QCurl ignored empty Retry-After header";
            return std::nullopt;
        }

        bool ok              = false;
        const qint64 seconds = raw.toLongLong(&ok);
        if (ok) {
            if (seconds < 0) {
                qWarning() << "QCurl ignored negative Retry-After header";
                return std::nullopt;
            }
            if (seconds > (std::numeric_limits<qint64>::max() / 1000)) {
                qWarning() << "QCurl ignored overflowing Retry-After header";
                return std::nullopt;
            }
            return std::chrono::milliseconds(seconds * 1000);
        }

        const QByteArray dateBytes = raw.toUtf8();
        const time_t parsed        = curl_getdate(dateBytes.constData(), nullptr);
        if (parsed < 0) {
            qWarning() << "QCurl ignored invalid Retry-After HTTP-date";
            return std::nullopt;
        }

        const time_t now = std::time(nullptr);
        if (parsed <= now) {
            return std::chrono::milliseconds(0);
        }

        const long double deltaSeconds = static_cast<long double>(parsed)
                                         - static_cast<long double>(now);
        if (!std::isfinite(deltaSeconds)
            || deltaSeconds
                   > (static_cast<long double>(std::numeric_limits<qint64>::max()) / 1000.0L)) {
            qWarning() << "QCurl ignored overflowing Retry-After HTTP-date";
            return std::nullopt;
        }

        return std::chrono::milliseconds(static_cast<qint64>(deltaSeconds * 1000.0L));
    }
    return std::nullopt;
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

    std::optional<std::chrono::milliseconds> retryAfter;
    if (error == NetworkError::HttpTooManyRequests) {
        d->parseHeaders();
        retryAfter = parseReplyRetryAfterDelay(d->headerMap);
    }

    const auto delay = policy.delayForAttempt(d->attemptCount, retryAfter);

    d->attemptCount++;
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

    if (setIdleState) {
        d->state     = ReplyState::Idle;
        d->errorCode = NetworkError::NoError;
        d->errorMessage.clear();
    }

    d->bodyBuffer.clear();
    d->cacheBodyBuffer.clear();
    d->headerData.clear();
    d->finalHeaderList.clear();
    d->finalHeaderMap.clear();
    d->headerMap.clear();
    d->bytesDownloaded = 0;
    d->bytesUploaded   = 0;
    d->downloadTotal   = -1;
    d->uploadTotal     = -1;
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
