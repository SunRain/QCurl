/**
 * @file
 * @brief 将接收阶段规则适配到 reply owner thread、Qt timer 和原生连接事实。
 */
#ifndef QCNETWORKRECEIVEIDLEEXECUTION_P_H
#define QCNETWORKRECEIVEIDLEEXECUTION_P_H

#include "private/QCNetworkReceiveIdleTimeout_p.h"

#include <QPointer>
#include <QTimer>

#include <curl/curl.h>
#include <functional>

namespace QCurl::Internal {

class Q_DECL_HIDDEN QCNetworkReceiveIdleExecution final
{
public:
    void initialize(QObject *owner,
                    std::optional<std::chrono::milliseconds> timeout,
                    std::function<void()> expire);
    void beginRequest(CURL *easy);
    void receivedBytes();
    void synchronizeConnection(CURL *easy);
    void setReceivePaused(bool paused);
    void stop();
    void reset();
    [[nodiscard]] bool expired() const noexcept { return m_rules.expired(); }
    [[nodiscard]] quint64 generation() const noexcept { return m_rules.generation(); }
    [[nodiscard]] bool enabled() const noexcept { return m_rules.enabled(); }
    [[nodiscard]] bool expireIfDue(CURL *easy);

private:
    void schedule();

    QCNetworkReceiveIdleTimeout m_rules;
    // reply 唯一拥有 timer；共享 transfer backing 只弱观察，可安全晚于 reply 释放。
    QPointer<QTimer> m_timer;
    long m_redirectCount      = 0;
    curl_off_t m_redirectTime = 0;
};

} // namespace QCurl::Internal
#endif
