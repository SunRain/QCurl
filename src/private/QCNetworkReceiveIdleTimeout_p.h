/**
 * @file
 * @brief 保存接收阶段的单调时间与尝试事实，不持有原生句柄或 Qt 调度对象。
 */
#ifndef QCNETWORKRECEIVEIDLETIMEOUT_P_H
#define QCNETWORKRECEIVEIDLETIMEOUT_P_H

#include <QtGlobal>

#include <chrono>
#include <optional>

namespace QCurl::Internal {

class Q_DECL_HIDDEN QCNetworkReceiveIdleTimeout final
{
public:
    using Clock     = std::chrono::steady_clock;
    using TimePoint = Clock::time_point;

    void initialize(std::optional<std::chrono::milliseconds> timeout);
    void beginRequest(TimePoint now);
    void receivedBytes(TimePoint now);
    void setReceivePaused(bool paused, TimePoint now);
    void stop();
    void reset();
    [[nodiscard]] bool expired() const noexcept { return m_expired; }
    [[nodiscard]] quint64 generation() const noexcept { return m_generation; }
    [[nodiscard]] bool enabled() const noexcept { return m_timeout.has_value(); }
    [[nodiscard]] bool active() const noexcept { return m_active; }
    [[nodiscard]] std::optional<std::chrono::milliseconds> remaining(TimePoint now) const;
    [[nodiscard]] bool expireIfDue(TimePoint now);

private:
    std::optional<std::chrono::milliseconds> m_timeout;
    TimePoint m_lastReceive;
    quint64 m_generation = 0;
    bool m_active        = false;
    bool m_paused        = false;
    bool m_expired       = false;
};

} // namespace QCurl::Internal
#endif
