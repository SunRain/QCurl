/**
 * @file
 * @brief Implements offline mock capture and asynchronous reply replay.
 */

#include "QCNetworkAccessManager.h"
#include "QCNetworkReply.h"
#include "QCNetworkReply_p.h"
#include "QCNetworkTimeoutConfig.h"
#include "private/QCNetworkMockProvider_p.h"
#include "private/QCNetworkReplyBodySource_p.h"
#include "private/QCNetworkReplyConfigurationExecution_p.h"
#include "private/QCNetworkReplyExecution_p.h"
#include "private/QCNetworkReplyMockChaos_p.h"
#include "private/QCNetworkReplyResponse_p.h"
#include "private/QCNetworkReplyRuntime_p.h"

#include <QPointer>
#include <QTimer>

namespace QCurl::Internal {
namespace {

void captureMockRequest(const QCNetworkMockProvider &provider,
                        void *handler,
                        const QCNetworkReplyPrivate *reply)
{
    if (!provider.captureEnabled(handler)) {
        return;
    }

    const auto &normalized = reply->transferState->configuration.plan().normalized;
    const auto &body       = normalized.body;
    QCNetworkCapturedRequestSnapshot captured;
    captured.url    = normalized.request.url();
    captured.method = normalized.method;
    if (normalized.method == HttpMethod::Custom) {
        captured.customMethod = body.customMethod;
    }
    captured.followLocation = normalized.request.followLocation();
    const auto timeouts     = normalized.request.timeoutConfig();
    if (timeouts.connectTimeout().has_value()) {
        captured.connectTimeoutMs = timeouts.connectTimeout()->count();
    }
    if (timeouts.totalTimeout().has_value()) {
        captured.totalTimeoutMs = timeouts.totalTimeout()->count();
    }
    for (const auto &name : normalized.request.rawHeaderList()) {
        captured.headers.append({name, normalized.request.rawHeader(name)});
    }

    captured.bodySize      = body.hasKnownSize()
                                 ? static_cast<qsizetype>(qMax<qint64>(0, body.sizeBytes))
                                 : body.inlineBytes.size();
    const int previewLimit = provider.captureBodyPreviewLimit(handler);
    captured.bodyPreview   = previewLimit > 0 ? body.inlineBytes.left(previewLimit) : QByteArray();
    provider.recordRequest(handler, captured);
}

[[nodiscard]] bool isTerminal(const QCNetworkReplyPrivate *reply)
{
    return reply->transferState->lifecycle.isTerminal();
}

SignalEmissionResult applyMockPayload(const QPointer<QCNetworkReply> &reply,
                                      QCNetworkReplyPrivate *replyPrivate,
                                      const QCNetworkMockData &mockData)
{
    resetReplyForRetry(replyPrivate, false);
    auto &response = replyPrivate->transferState->response;
    response.applyReplayHeaders(mockData.headers, mockData.rawHeaderData);
    if (!mockData.response.isEmpty()) {
        response.appendBody(mockData.response);
        replyPrivate->transferState->lifecycle.observeReplayedBody(mockData.response.size());
        if (emitReplySignal(reply, [](QCNetworkReply *observer) { Q_EMIT observer->readyRead(); })
            == SignalEmissionResult::Destroyed) {
            return SignalEmissionResult::Destroyed;
        }
    }
    return SignalEmissionResult::Alive;
}

void finishMockAttempt(const QPointer<QCNetworkReply> &reply,
                       QCNetworkReplyPrivate *replyPrivate,
                       const QCNetworkMockData &mockData)
{
    auto &lifecycle = replyPrivate->transferState->lifecycle;
    if (lifecycle.isTerminal()) {
        return;
    }
    replyPrivate->transferState->response.commitStatus(mockData.statusCode);
    const auto info = lifecycle.classifyMockResult(mockData.isError,
                                                   mockData.error,
                                                   mockData.statusCode,
                                                   replyPrivate->transferState->bodySource.failure());
    if (info.error == NetworkError::NoError) {
        Q_UNUSED(replyPrivate->setState(ReplyState::Finished));
        return;
    }
    const auto retry = advanceReplyRetryIfNeeded(replyPrivate, info.error);
    if (retry.emissionResult == SignalEmissionResult::Destroyed || isTerminal(replyPrivate)) {
        return;
    }
    if (retry.delay.has_value()) {
        scheduleAsyncReplyRetry(reply, replyPrivate, retry.delay.value());
        return;
    }
    replyPrivate->setError(info.error, info.message);
    Q_UNUSED(replyPrivate->setState(ReplyState::Error));
}

void replayMockResponse(const QPointer<QCNetworkReply> &reply,
                        QCNetworkReplyPrivate *replyPrivate,
                        HttpMethod method,
                        const QUrl &url)
{
    if (!reply || isTerminal(replyPrivate)) {
        return;
    }

    auto *manager        = qobject_cast<QCNetworkAccessManager *>(reply->parent());
    const auto *provider = networkMockProvider();
    void *handler        = provider && manager ? provider->handlerForManager(manager) : nullptr;
    if (!handler) {
        replyPrivate->setError(NetworkError::InvalidRequest, QStringLiteral("MockHandler: not set"));
        Q_UNUSED(replyPrivate->setState(ReplyState::Error));
        return;
    }

    QCNetworkMockData mockData;
    if (!provider->consumeMock(handler, method, url, mockData)) {
        replyPrivate
            ->setError(NetworkError::InvalidRequest,
                       QStringLiteral("MockHandler: no mock matched for %1").arg(url.toString()));
        Q_UNUSED(replyPrivate->setState(ReplyState::Error));
        return;
    }
    if (startMockChaosReplay(reply, replyPrivate, mockData, method, url)) {
        return;
    }

    if (applyMockPayload(reply, replyPrivate, mockData) == SignalEmissionResult::Destroyed) {
        return;
    }
    finishMockAttempt(reply, replyPrivate, mockData);
}

} // namespace

bool QCNetworkReplyExecution::dispatchMock(QCNetworkReply *reply, QCNetworkAccessManager *manager)
{
    const auto *provider = networkMockProvider();
    void *handler        = provider && manager ? provider->handlerForManager(manager) : nullptr;
    if (!handler) {
        return false;
    }

    auto *d                = reply->d_func();
    const auto &normalized = d->transferState->configuration.plan().normalized;
    captureMockRequest(*provider, handler, d);
    if (!provider->hasMock(handler, normalized.method, normalized.request.url())) {
        return false;
    }

    Q_UNUSED(
        publishReplyCurlConfigurationResult(d,
                                            d->transferState->configuration.mockConflictWarnings()));
    if (d->setState(ReplyState::Running) == SignalEmissionResult::Destroyed) {
        return true;
    }
    QPointer<QCNetworkReply> safeReply(reply);
    const HttpMethod method = normalized.method;
    const QUrl url          = normalized.request.url();
    QTimer::singleShot(qMax(0, provider->globalDelay(handler)),
                       reply,
                       [safeReply, d, method, url]() {
                           replayMockResponse(safeReply, d, method, url);
                       });
    return true;
}

} // namespace QCurl::Internal
