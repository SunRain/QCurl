#include "QCNetworkCancelToken.h"
#include "private/QCNetworkDiagnosticsOperation_p.h"

#include <QDateTime>

#include <limits>
#include <utility>

namespace QCurl::Internal {

DiagnosticsOperation::DiagnosticsOperation(QString operationName,
                                           QString target,
                                           int timeoutMs,
                                           QCNetworkCancelToken *cancelToken)
    : m_operationName(std::move(operationName))
    , m_target(std::move(target))
{
    m_promise.start();
    m_elapsed.start();

    m_deadline.setSingleShot(true);
    QObject::connect(&m_deadline, &QTimer::timeout, this, [this]() { timeout(); });
    m_deadline.start(timeoutMs);

    QObject::connect(&m_cancelWatcher, &QFutureWatcher<DiagResult>::canceled, this, [this]() {
        cancel();
    });
    m_cancelWatcher.setFuture(m_promise.future());

    if (cancelToken) {
        QObject::connect(cancelToken, &QCNetworkCancelToken::cancelled, this, [this]() {
            cancel();
        });
        QObject::connect(cancelToken, &QObject::destroyed, this, [this]() { cancel(); });
        if (cancelToken->isCancelled()) {
            QTimer::singleShot(0, this, [this]() { cancel(); });
        }
    }
}

DiagnosticsOperation::~DiagnosticsOperation()
{
    if (!m_finished) {
        DiagResult result;
        result.setSuccess(false);
        result.setSummary(QStringLiteral("%1 owner 已销毁: %2").arg(m_operationName, m_target));
        result.setErrorString(QStringLiteral("OwnerDestroyed"));
        result.setDetail(QStringLiteral("target"), m_target);
        result.setDetail(QStringLiteral("ownerDestroyed"), true);
        result.setDurationMs(m_elapsed.elapsed());
        m_promise.addResult(std::move(result));
        m_promise.finish();
    }
}

QFuture<DiagResult> DiagnosticsOperation::future() const
{
    return m_promise.future();
}

qint64 DiagnosticsOperation::elapsedMs() const
{
    return m_elapsed.elapsed();
}

bool DiagnosticsOperation::isFinished() const noexcept
{
    return m_finished || m_stopping;
}

void DiagnosticsOperation::setCancelHandler(std::function<void()> handler)
{
    m_cancelHandler = std::move(handler);
}

void DiagnosticsOperation::setTimeoutHandler(std::function<void()> handler)
{
    m_timeoutHandler = std::move(handler);
}

void DiagnosticsOperation::finish(DiagResult result)
{
    if (m_finished) {
        return;
    }

    m_finished = true;
    m_stopping = false;
    m_deadline.stop();
    result.setDurationMs(m_elapsed.elapsed());
    if (!result.timestamp().isValid()) {
        result.setTimestamp(QDateTime::currentDateTime());
    }
    m_promise.addResult(std::move(result));
    m_promise.finish();
    deleteLater();
}

void DiagnosticsOperation::fail(const QString &summary, const QString &errorString)
{
    DiagResult result;
    result.setSuccess(false);
    result.setSummary(summary);
    result.setErrorString(errorString);
    result.setDetail(QStringLiteral("target"), m_target);
    finish(std::move(result));
}

void DiagnosticsOperation::cancel()
{
    if (isFinished()) {
        return;
    }
    m_stopping = true;
    if (m_cancelHandler) {
        m_cancelHandler();
    }

    DiagResult result;
    result.setSuccess(false);
    result.setSummary(QStringLiteral("%1 已取消: %2").arg(m_operationName, m_target));
    result.setErrorString(QStringLiteral("Cancelled"));
    result.setDetail(QStringLiteral("target"), m_target);
    result.setDetail(QStringLiteral("cancelled"), true);
    finish(std::move(result));
}

void DiagnosticsOperation::timeout()
{
    if (isFinished()) {
        return;
    }
    m_stopping = true;
    if (m_timeoutHandler) {
        m_timeoutHandler();
    } else if (m_cancelHandler) {
        m_cancelHandler();
    }

    DiagResult result;
    result.setSuccess(false);
    result.setSummary(QStringLiteral("%1 超时: %2").arg(m_operationName, m_target));
    result.setErrorString(QStringLiteral("Timeout"));
    result.setDetail(QStringLiteral("target"), m_target);
    result.setDetail(QStringLiteral("timedOut"), true);
    finish(std::move(result));
}

QFuture<DiagResult> finishedDiagnosticsFuture(DiagResult result)
{
    QPromise<DiagResult> promise;
    promise.start();
    promise.addResult(std::move(result));
    promise.finish();
    return promise.future();
}

int diagnosticsTimeoutMs(const QCNetworkDiagnosticsOptions &options)
{
    const auto timeout = options.timeout().count();
    return static_cast<int>(std::min<qint64>(timeout, std::numeric_limits<int>::max()));
}

QVariantMap diagResultToVariantMap(const DiagResult &result)
{
    QVariantMap map = result.details();
    map.insert(QStringLiteral("success"), result.success());
    map.insert(QStringLiteral("summary"), result.summary());
    map.insert(QStringLiteral("durationMs"), result.durationMs());
    if (!result.errorString().isEmpty()) {
        map.insert(QStringLiteral("errorString"), result.errorString());
    }
    return map;
}

} // namespace QCurl::Internal
