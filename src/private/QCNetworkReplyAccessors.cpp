/**
 * @file
 * @brief 将公共查询与消费操作连接到领域模块。
 */

#include "QCNetworkReply.h"
#include "QCNetworkReply_p.h"
#include "private/QCNetworkReplyFlowControlExecution_p.h"
#include "private/QCNetworkReplyRuntime_p.h"

#include <QDebug>

#include <ctime>

namespace QCurl {

std::optional<QByteArray> QCNetworkReply::readAll()
{
    Q_D(QCNetworkReply);
    auto &response = d->transferState->response;
    if (response.bytesAvailable() == 0) {
        // 终态空 body 与运行中尚无数据必须可区分。
        return d->transferState->lifecycle.isTerminal() ? std::optional<QByteArray>(QByteArray())
                                                        : std::nullopt;
    }
    QByteArray body = response.takeAvailableBody();
    Internal::scheduleReplyBackpressureResumeAfterRead(this, d);
    return body;
}

#ifdef QCURL_ENABLE_TEST_HOOKS
qint64 QCNetworkReply::retainedCacheBodyBytesForTesting() const
{
    Q_D(const QCNetworkReply);
    return d->transferState->response.retainedCacheBodyBytes();
}
#endif

QList<RawHeaderPair> QCNetworkReply::rawHeaders() const
{
    Q_D(const QCNetworkReply);
    if (!d || d->transferState->lifecycle.state() == ReplyState::Idle) {
        qWarning() << "QCNetworkReply::rawHeaders: Invalid state"
                   << (d ? static_cast<int>(d->transferState->lifecycle.state()) : -1);
        return {};
    }
    return d->transferState->response.rawHeaders();
}

QByteArray QCNetworkReply::rawHeader(const QByteArray &name) const
{
    Q_D(const QCNetworkReply);
    if (!d || d->transferState->lifecycle.state() == ReplyState::Idle) {
        return {};
    }
    return d->transferState->response.rawHeader(name);
}

bool QCNetworkReply::hasRawHeader(const QByteArray &name) const
{
    Q_D(const QCNetworkReply);
    return d && d->transferState->lifecycle.state() != ReplyState::Idle
           && d->transferState->response.hasRawHeader(name);
}

QByteArray QCNetworkReply::rawHeaderData() const
{
    Q_D(const QCNetworkReply);
    if (!d || d->transferState->lifecycle.state() == ReplyState::Idle) {
        qWarning() << "QCNetworkReply::rawHeaderData: Invalid state";
        return {};
    }
    return d->transferState->response.rawHeaderData();
}

QUrl QCNetworkReply::url() const
{
    Q_D(const QCNetworkReply);
    return d->request.url();
}

QCNetworkDiagnosticQueryPolicy QCNetworkReply::diagnosticQueryPolicy() const
{
    Q_D(const QCNetworkReply);
    return d->request.diagnosticQueryPolicy();
}

HttpMethod QCNetworkReply::method() const noexcept
{
    Q_D(const QCNetworkReply);
    return d->httpMethod;
}

int QCNetworkReply::httpStatusCode() const noexcept
{
    Q_D(const QCNetworkReply);
    return d->transferState->response.status();
}

qint64 QCNetworkReply::durationMs() const noexcept
{
    Q_D(const QCNetworkReply);
    return d->transferState->lifecycle.durationMs();
}

std::optional<std::chrono::milliseconds> QCNetworkReply::retryAfterDelay() const
{
    Q_D(const QCNetworkReply);
    return d->transferState->response.retryAfterDelay(std::time(nullptr));
}

int QCNetworkReply::retryAttemptCount() const noexcept
{
    Q_D(const QCNetworkReply);
    return d->retry.attemptCount();
}

std::chrono::milliseconds QCNetworkReply::cumulativeRetryDelay() const noexcept
{
    Q_D(const QCNetworkReply);
    return d->retry.cumulativeDelay();
}

qint64 QCNetworkReply::bytesAvailable() const noexcept
{
    Q_D(const QCNetworkReply);
    return d->transferState->response.bytesAvailable();
}

QStringList QCNetworkReply::capabilityWarnings() const
{
    Q_D(const QCNetworkReply);
    return d->transferState->lifecycle.warnings();
}

ReplyState QCNetworkReply::state() const noexcept
{
    Q_D(const QCNetworkReply);
    return d->transferState->lifecycle.state();
}

NetworkError QCNetworkReply::error() const noexcept
{
    Q_D(const QCNetworkReply);
    return d->transferState->lifecycle.error();
}

QString QCNetworkReply::errorString() const
{
    Q_D(const QCNetworkReply);
    return d->transferState->lifecycle.errorMessage();
}

int QCNetworkReply::diagnosticCurlCode() const noexcept
{
    Q_D(const QCNetworkReply);
    return d->transferState->lifecycle.diagnosticCurlCode();
}

bool QCNetworkReply::isFinished() const noexcept
{
    Q_D(const QCNetworkReply);
    return d->transferState->lifecycle.isTerminal();
}

bool QCNetworkReply::isRunning() const noexcept
{
    Q_D(const QCNetworkReply);
    return d->transferState->lifecycle.state() == ReplyState::Running;
}

bool QCNetworkReply::isPaused() const noexcept
{
    Q_D(const QCNetworkReply);
    return d->transferState->lifecycle.state() == ReplyState::Paused;
}

bool QCNetworkReply::isBackpressureActive() const noexcept
{
    Q_D(const QCNetworkReply);
    return d->transferState->flow.backpressureActive();
}

bool QCNetworkReply::isUploadSendPaused() const noexcept
{
    Q_D(const QCNetworkReply);
    return !d->transferState->lifecycle.isTerminal() && d->transferState->flow.uploadSendPaused();
}

qint64 QCNetworkReply::backpressureLimitBytes() const noexcept
{
    Q_D(const QCNetworkReply);
    return d->transferState->flow.limitBytes();
}

qint64 QCNetworkReply::backpressureResumeBytes() const noexcept
{
    Q_D(const QCNetworkReply);
    return d->transferState->flow.resumeBytes();
}

qint64 QCNetworkReply::backpressureBufferedBytesPeak() const noexcept
{
    Q_D(const QCNetworkReply);
    return d->transferState->flow.peakBufferedBytes();
}

qint64 QCNetworkReply::bytesReceived() const noexcept
{
    Q_D(const QCNetworkReply);
    return d->transferState->lifecycle.bytesDownloaded();
}

qint64 QCNetworkReply::bytesTotal() const noexcept
{
    Q_D(const QCNetworkReply);
    return d->transferState->lifecycle.downloadTotal();
}

QByteArray Internal::testCurlPlanDigest(const QCNetworkReply *reply)
{
    if (!reply) {
        return {};
    }
#ifdef QCURL_ENABLE_TEST_HOOKS
    return reply->property(Internal::kTestCurlPlanDigestProperty).toByteArray();
#else
    return {};
#endif
}

QMap<QByteArray, QByteArray> QCNetworkReply::parseResponseHeaders()
{
    Q_D(QCNetworkReply);
    return d->transferState->response.parsedResponseHeaders();
}

} // namespace QCurl
