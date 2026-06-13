#include "QCWebSocketReconnectPolicy.h"

#ifdef QCURL_WEBSOCKET_SUPPORT

#include <QSharedData>

#include <algorithm>
#include <cmath>

namespace QCurl {

namespace {

constexpr int kNoRetries            = 0;
constexpr int kStandardMaxRetries   = 3;
constexpr int kAggressiveMaxRetries = 10;
constexpr std::chrono::milliseconds kDefaultInitialDelay{1000};
constexpr std::chrono::milliseconds kAggressiveInitialDelay{500};
constexpr double kDefaultBackoffMultiplier    = 2.0;
constexpr double kAggressiveBackoffMultiplier = 1.5;
constexpr std::chrono::milliseconds kDefaultMaxDelay{30000};
constexpr std::chrono::milliseconds kAggressiveMaxDelay{60000};

const QCWebSocketReconnectPolicy::CloseCodeSet kDefaultRetriableCloseCodes{
    QCWebSocket::CloseCode::GoingAway,
    QCWebSocket::CloseCode::AbnormalClosure,
    QCWebSocket::CloseCode::InternalError,
};

} // namespace

/// WebSocket 重连策略的共享负载；不保存 socket 运行时状态。
class QCWebSocketReconnectPolicyData : public QSharedData
{
public:
    int maxRetries                                               = kNoRetries;
    std::chrono::milliseconds initialDelay                       = kDefaultInitialDelay;
    double backoffMultiplier                                     = kDefaultBackoffMultiplier;
    std::chrono::milliseconds maxDelay                           = kDefaultMaxDelay;
    QCWebSocketReconnectPolicy::CloseCodeSet retriableCloseCodes = kDefaultRetriableCloseCodes;
};

QCWebSocketReconnectPolicy::QCWebSocketReconnectPolicy()
    : d(new QCWebSocketReconnectPolicyData)
{}

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

QCWebSocketReconnectPolicy::CloseCodeSet QCWebSocketReconnectPolicy::retriableCloseCodes() const
{
    return d->retriableCloseCodes;
}

void QCWebSocketReconnectPolicy::setRetriableCloseCodes(const CloseCodeSet &closeCodes)
{
    d->retriableCloseCodes = closeCodes;
}

bool QCWebSocketReconnectPolicy::shouldRetry(QCWebSocket::CloseCode closeCode,
                                             int attemptCount) const
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
    policy.setMaxRetries(kNoRetries);
    return policy;
}

QCWebSocketReconnectPolicy QCWebSocketReconnectPolicy::standardReconnect()
{
    QCWebSocketReconnectPolicy policy;
    policy.setMaxRetries(kStandardMaxRetries);
    policy.setInitialDelay(kDefaultInitialDelay);
    policy.setBackoffMultiplier(kDefaultBackoffMultiplier);
    policy.setMaxDelay(kDefaultMaxDelay);
    policy.setRetriableCloseCodes(kDefaultRetriableCloseCodes);
    return policy;
}

QCWebSocketReconnectPolicy QCWebSocketReconnectPolicy::aggressiveReconnect()
{
    QCWebSocketReconnectPolicy policy;
    policy.setMaxRetries(kAggressiveMaxRetries);
    policy.setInitialDelay(kAggressiveInitialDelay);
    policy.setBackoffMultiplier(kAggressiveBackoffMultiplier);
    policy.setMaxDelay(kAggressiveMaxDelay);
    policy.setRetriableCloseCodes(kDefaultRetriableCloseCodes);
    return policy;
}

} // namespace QCurl

#endif // QCURL_WEBSOCKET_SUPPORT
