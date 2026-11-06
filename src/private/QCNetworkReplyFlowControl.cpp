/**
 * @file
 * @brief 实现暂停原因合并和原生 pause 提交/回滚。
 */

#include "private/QCNetworkReplyFlowControl_p.h"

namespace QCurl::Internal {
namespace {

#ifdef QCURL_ENABLE_TEST_HOOKS
thread_local ReplyPauseTestHook s_pauseHook = nullptr;
#endif

CURLcode pauseNative(CURL *handle, int mask)
{
#ifdef QCURL_ENABLE_TEST_HOOKS
    if (s_pauseHook) {
        return s_pauseHook(handle, mask);
    }
#endif
    return curl_easy_pause(handle, mask);
}

} // namespace

#ifdef QCURL_ENABLE_TEST_HOOKS
void setReplyPauseTestHook(ReplyPauseTestHook hook)
{
    s_pauseHook = hook;
}
#endif

QCReplyFlowControl::QCReplyFlowControl(qint64 limit, qint64 resume)
    : m_limit(limit > 0 ? limit : 0)
    , m_resume(limit > 0 ? (resume > 0 && resume < limit ? resume : limit / 2) : 0)
{}

int QCReplyFlowControl::modeMask(PauseMode mode)
{
    switch (mode) {
        case PauseMode::Recv:
            return CURLPAUSE_RECV;
        case PauseMode::Send:
            return CURLPAUSE_SEND;
        case PauseMode::All:
            return CURLPAUSE_ALL;
    }
    return CURLPAUSE_ALL;
}

ReplyFlowChange QCReplyFlowControl::apply(CURL *handle, int mask)
{
    ReplyFlowChange change;
    const int normalized = mask & CURLPAUSE_ALL;
    change.nativeMask    = normalized;
    if (!handle) {
        change.accepted = false;
        return change;
    }
    if (normalized == m_appliedMask) {
        return change;
    }

    const int previous = m_appliedMask;
    // 先呈现本次原生目标，允许恢复中的 callback 再次暂停；返回后保留它的最新提交。
    m_appliedMask   = normalized;
    change.error    = pauseNative(handle, normalized == 0 ? CURLPAUSE_CONT : normalized);
    change.accepted = change.error == CURLE_OK;
    if (!change.accepted && m_appliedMask == normalized) {
        m_appliedMask = previous;
    }
    change.wake = change.accepted && (previous & ~m_appliedMask) != 0;
    return change;
}

ReplyFlowChange QCReplyFlowControl::pauseUser(CURL *handle, PauseMode mode, bool inCallback)
{
    const int previous  = m_userMask;
    const int requested = modeMask(mode);
    m_userMask          = requested;
    if (inCallback && requested == CURLPAUSE_RECV) {
        return {};
    }
    auto change = apply(handle, desiredMask());
    if (!change.accepted && m_userMask == requested) {
        m_userMask = previous;
    }
    return change;
}

ReplyFlowChange QCReplyFlowControl::resumeUser(CURL *handle)
{
    const int previous = m_userMask;
    m_userMask         = 0;
    auto change        = apply(handle, desiredMask());
    if (!change.accepted && m_userMask == 0) {
        m_userMask = previous;
    }
    return change;
}

bool QCReplyFlowControl::pauseWriteIfRequested()
{
    if ((m_userMask & CURLPAUSE_RECV) && !(m_appliedMask & CURLPAUSE_RECV)) {
        m_appliedMask |= CURLPAUSE_RECV;
        return true;
    }
    return false;
}

void QCReplyFlowControl::observeBufferedBytes(qint64 bytes)
{
    if (m_limit > 0) {
        m_peak = qMax(m_peak, bytes);
    }
}

ReplyFlowChange QCReplyFlowControl::afterReadyRead(CURL *handle, qint64 bytes, bool terminal)
{
    if (terminal || m_limit <= 0 || bytes < m_limit || (m_userMask & CURLPAUSE_RECV)) {
        return {};
    }
    const int previous = m_internalMask;
    m_internalMask |= CURLPAUSE_RECV;
    auto change = apply(handle, desiredMask());
    if (!change.accepted) {
        m_internalMask = previous;
    } else if ((m_internalMask & CURLPAUSE_RECV) && !m_backpressureActive) {
        m_backpressureActive       = true;
        change.backpressureChanged = true;
    }
    return change;
}

ReplyFlowChange QCReplyFlowControl::resumeAfterRead(CURL *handle, qint64 bytes, bool terminal)
{
    if (terminal || !recvResumePending() || bytes > m_resume) {
        return {};
    }
    m_internalMask &= ~CURLPAUSE_RECV;
    auto change = apply(handle, desiredMask());
    if (!change.accepted) {
        m_internalMask |= CURLPAUSE_RECV;
    } else if (!(m_internalMask & CURLPAUSE_RECV) && m_backpressureActive) {
        m_backpressureActive       = false;
        change.backpressureChanged = true;
    }
    return change;
}

bool QCReplyFlowControl::sourceWait()
{
    m_internalMask |= CURLPAUSE_SEND;
    m_appliedMask |= CURLPAUSE_SEND;
    const bool changed = !m_uploadSendPaused;
    m_uploadSendPaused = true;
    return changed;
}

ReplyFlowChange QCReplyFlowControl::resumeReadySource(CURL *handle, bool ready, bool terminal)
{
    if (terminal || !ready || !(m_internalMask & CURLPAUSE_SEND)) {
        return {};
    }
    m_internalMask &= ~CURLPAUSE_SEND;
    auto change = apply(handle, desiredMask());
    if (!change.accepted) {
        m_internalMask |= CURLPAUSE_SEND;
    } else if (!(m_internalMask & CURLPAUSE_SEND) && m_uploadSendPaused) {
        m_uploadSendPaused        = false;
        change.uploadPauseChanged = true;
    }
    return change;
}

bool QCReplyFlowControl::leaveBackpressure()
{
    m_internalMask &= ~CURLPAUSE_RECV;
    const bool changed   = m_backpressureActive;
    m_backpressureActive = false;
    return changed;
}

bool QCReplyFlowControl::leaveUploadPause()
{
    m_internalMask &= ~CURLPAUSE_SEND;
    const bool changed = m_uploadSendPaused;
    m_uploadSendPaused = false;
    return changed;
}

void QCReplyFlowControl::pauseReplay(PauseMode mode)
{
    m_userMask = modeMask(mode);
}

void QCReplyFlowControl::resumeReplay()
{
    m_userMask = 0;
}

bool QCReplyFlowControl::recvResumePending() const noexcept
{
    return m_limit > 0 && (m_internalMask & CURLPAUSE_RECV);
}

bool QCReplyFlowControl::uploadSendPaused() const noexcept
{
    return m_uploadSendPaused && (m_internalMask & CURLPAUSE_SEND);
}

} // namespace QCurl::Internal
