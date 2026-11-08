#include "private/QCNetworkReceiveIdleTimeout_p.h"

#include <algorithm>

namespace QCurl::Internal {

void QCNetworkReceiveIdleTimeout::initialize(std::optional<std::chrono::milliseconds> timeout)
{
    m_timeout = timeout;
}

void QCNetworkReceiveIdleTimeout::beginRequest(TimePoint now)
{
    // 即使未启用时限，回放也需要独立代次以隔离旧 attempt 的迟到投递。
    ++m_generation;
    if (!m_timeout || m_expired) {
        return;
    }
    m_active      = true;
    m_lastReceive = now;
}

void QCNetworkReceiveIdleTimeout::receivedBytes(TimePoint now)
{
    if (m_active && !m_expired) {
        m_lastReceive = now;
    }
}

void QCNetworkReceiveIdleTimeout::setReceivePaused(bool paused, TimePoint now)
{
    if (m_paused == paused) {
        return;
    }
    m_paused = paused;
    if (!paused && m_active) {
        m_lastReceive = now;
    }
}

void QCNetworkReceiveIdleTimeout::stop()
{
    m_active = false;
}

void QCNetworkReceiveIdleTimeout::reset()
{
    stop();
    ++m_generation;
    m_expired = false;
    m_paused  = false;
}

std::optional<std::chrono::milliseconds> QCNetworkReceiveIdleTimeout::remaining(TimePoint now) const
{
    if (!m_active || m_paused || !m_timeout || m_expired) {
        return std::nullopt;
    }
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - m_lastReceive);
    return std::max(std::chrono::milliseconds(1), *m_timeout - elapsed);
}

bool QCNetworkReceiveIdleTimeout::expireIfDue(TimePoint now)
{
    if (!m_active || m_paused || !m_timeout || m_expired || now - m_lastReceive < *m_timeout) {
        return false;
    }
    m_expired = true;
    stop();
    return true;
}

} // namespace QCurl::Internal
