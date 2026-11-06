/**
 * @file
 * @brief 实现请求 phase、跨重试计时与进度事实。
 */

#include "private/QCReplyLifecycle_p.h"

namespace QCurl::Internal {

bool QCReplyLifecycle::isTerminal() const noexcept
{
    return m_state == ReplyState::Finished || m_state == ReplyState::Error
           || m_state == ReplyState::Cancelled;
}

bool QCReplyLifecycle::transition(ReplyState state)
{
    if (m_state == state || isTerminal()) {
        return false;
    }
    m_state = state;
    if (state == ReplyState::Running && !m_clock.isValid()) {
        m_clock.start();
    }
    if (isTerminal() && m_clock.isValid() && m_durationMs < 0) {
        m_durationMs = m_clock.elapsed();
    }
    return true;
}

void QCReplyLifecycle::resetForRetry()
{
    if (isTerminal()) {
        return;
    }
    m_state = ReplyState::Idle;
    m_error = NetworkError::NoError;
    m_errorMessage.clear();
    resetReplayObservations();
}

void QCReplyLifecycle::resetReplayObservations()
{
    if (isTerminal()) {
        return;
    }
    m_nativeCode      = 0;
    m_bytesDownloaded = 0;
    m_bytesUploaded   = 0;
    m_downloadTotal   = -1;
    m_uploadTotal     = -1;
}

void QCReplyLifecycle::recordError(NetworkError error, const QString &message)
{
    if (!isTerminal()) {
        m_error        = error;
        m_errorMessage = message;
    }
}

void QCReplyLifecycle::clearNativeDiagnostic()
{
    if (!isTerminal()) {
        m_nativeCode = 0;
    }
}

void QCReplyLifecycle::acceptCachedResult()
{
    clearNativeDiagnostic();
    recordError(NetworkError::NoError, {});
}

void QCReplyLifecycle::cancelSilently()
{
    clearNativeDiagnostic();
    recordError(NetworkError::OperationCancelled,
                QCurl::errorString(NetworkError::OperationCancelled));
    static_cast<void>(transition(ReplyState::Cancelled));
}

int QCReplyLifecycle::diagnosticCurlCode() const noexcept
{
    return isTerminal() ? m_nativeCode : 0;
}

qint64 QCReplyLifecycle::elapsedMs() const noexcept
{
    return m_durationMs >= 0 ? m_durationMs : (m_clock.isValid() ? m_clock.elapsed() : 0);
}

void QCReplyLifecycle::observeNetworkProgress(qint64 downloadTotal,
                                              qint64 downloaded,
                                              qint64 uploadTotal,
                                              qint64 uploaded)
{
    if (!isTerminal()) {
        m_downloadTotal   = downloadTotal;
        m_bytesDownloaded = downloaded;
        m_uploadTotal     = uploadTotal;
        m_bytesUploaded   = uploaded;
    }
}

void QCReplyLifecycle::observeBodyChunk(qint64 bytes)
{
    if (!isTerminal()) {
        m_bytesDownloaded += bytes;
    }
}

void QCReplyLifecycle::beginReplay(qint64 total)
{
    if (!isTerminal()) {
        m_downloadTotal   = total;
        m_bytesDownloaded = 0;
    }
}

void QCReplyLifecycle::observeReplayedBody(qint64 bytes)
{
    if (!isTerminal()) {
        m_bytesDownloaded = bytes;
    }
}

void QCReplyLifecycle::appendWarnings(const QStringList &warnings)
{
    m_warnings.append(warnings);
}

} // namespace QCurl::Internal
