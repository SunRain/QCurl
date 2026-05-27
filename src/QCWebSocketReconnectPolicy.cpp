#include "QCWebSocketReconnectPolicy.h"

#ifdef QCURL_WEBSOCKET_SUPPORT

#include <QSharedData>

#include <algorithm>
#include <cmath>

namespace QCurl {

/// WebSocket 重连策略的共享负载；不保存 socket 运行时状态。
class QCWebSocketReconnectPolicyData : public QSharedData
{
public:
    int maxRetries = 0;
    std::chrono::milliseconds initialDelay{1000};
    double backoffMultiplier = 2.0;
    std::chrono::milliseconds maxDelay{30000};
    QSet<int> retriableCloseCodes = {1001, 1006, 1011};
};

QCWebSocketReconnectPolicy::QCWebSocketReconnectPolicy()
    : d(new QCWebSocketReconnectPolicyData)
{
}

QCWebSocketReconnectPolicy::QCWebSocketReconnectPolicy(
    const QCWebSocketReconnectPolicy &other) = default;

QCWebSocketReconnectPolicy::QCWebSocketReconnectPolicy(
    QCWebSocketReconnectPolicy &&other) noexcept = default;

QCWebSocketReconnectPolicy::~QCWebSocketReconnectPolicy() = default;

QCWebSocketReconnectPolicy &QCWebSocketReconnectPolicy::operator=(
    const QCWebSocketReconnectPolicy &other) = default;

QCWebSocketReconnectPolicy &QCWebSocketReconnectPolicy::operator=(
    QCWebSocketReconnectPolicy &&other) noexcept = default;

int QCWebSocketReconnectPolicy::maxRetries() const noexcept
{
    return d->maxRetries;
}

void QCWebSocketReconnectPolicy::setMaxRetries(int maxRetries) noexcept
{
    d->maxRetries = maxRetries;
}

std::chrono::milliseconds QCWebSocketReconnectPolicy::initialDelay() const noexcept
{
    return d->initialDelay;
}

void QCWebSocketReconnectPolicy::setInitialDelay(std::chrono::milliseconds delay) noexcept
{
    d->initialDelay = delay;
}

double QCWebSocketReconnectPolicy::backoffMultiplier() const noexcept
{
    return d->backoffMultiplier;
}

void QCWebSocketReconnectPolicy::setBackoffMultiplier(double multiplier) noexcept
{
    d->backoffMultiplier = multiplier;
}

std::chrono::milliseconds QCWebSocketReconnectPolicy::maxDelay() const noexcept
{
    return d->maxDelay;
}

void QCWebSocketReconnectPolicy::setMaxDelay(std::chrono::milliseconds delay) noexcept
{
    d->maxDelay = delay;
}

QSet<int> QCWebSocketReconnectPolicy::retriableCloseCodes() const
{
    return d->retriableCloseCodes;
}

void QCWebSocketReconnectPolicy::setRetriableCloseCodes(const QSet<int> &closeCodes)
{
    d->retriableCloseCodes = closeCodes;
}

bool QCWebSocketReconnectPolicy::shouldRetry(int closeCode, int attemptCount) const
{
    if (d->maxRetries <= 0 || attemptCount > d->maxRetries) {
        return false;
    }

    return d->retriableCloseCodes.contains(closeCode);
}

std::chrono::milliseconds QCWebSocketReconnectPolicy::delayForAttempt(int attemptCount) const
{
    if (attemptCount <= 0) {
        return std::chrono::milliseconds(0);
    }

    double exponent        = static_cast<double>(attemptCount - 1);
    double multiplier      = std::pow(d->backoffMultiplier, exponent);
    double calculatedDelay = d->initialDelay.count() * multiplier;

    auto delayMs = static_cast<qint64>(
        std::min(calculatedDelay, static_cast<double>(d->maxDelay.count())));

    return std::chrono::milliseconds(delayMs);
}

QCWebSocketReconnectPolicy QCWebSocketReconnectPolicy::noReconnect()
{
    QCWebSocketReconnectPolicy policy;
    policy.setMaxRetries(0);
    return policy;
}

QCWebSocketReconnectPolicy QCWebSocketReconnectPolicy::standardReconnect()
{
    QCWebSocketReconnectPolicy policy;
    policy.setMaxRetries(3);
    policy.setInitialDelay(std::chrono::milliseconds(1000));
    policy.setBackoffMultiplier(2.0);
    policy.setMaxDelay(std::chrono::milliseconds(30000));
    policy.setRetriableCloseCodes({1001, 1006, 1011});
    return policy;
}

QCWebSocketReconnectPolicy QCWebSocketReconnectPolicy::aggressiveReconnect()
{
    QCWebSocketReconnectPolicy policy;
    policy.setMaxRetries(10);
    policy.setInitialDelay(std::chrono::milliseconds(500));
    policy.setBackoffMultiplier(1.5);
    policy.setMaxDelay(std::chrono::milliseconds(60000));
    policy.setRetriableCloseCodes({1001, 1006, 1011});
    return policy;
}

} // namespace QCurl

#endif // QCURL_WEBSOCKET_SUPPORT
