#include "QCNetworkRetryPolicy.h"

#include <QSharedData>

#include <algorithm>
#include <cmath>

namespace QCurl {

class QCNetworkRetryPolicyData : public QSharedData
{
public:
    int maxRetries = 0;
    std::chrono::milliseconds initialDelay{1000};
    double backoffMultiplier = 2.0;
    std::chrono::milliseconds maxDelay{30000};
    QSet<NetworkError> retryableErrors = {
        // 网络层临时性错误
        NetworkError::ConnectionRefused,
        NetworkError::ConnectionTimeout,
        NetworkError::HostNotFound,

        // HTTP 临时性错误
        NetworkError::HttpTimeout,
        NetworkError::HttpTooManyRequests,
        NetworkError::HttpInternalServerError,
        NetworkError::HttpNotImplemented,
        NetworkError::HttpBadGateway,
        NetworkError::HttpServiceUnavailable,
        NetworkError::HttpGatewayTimeout};
    bool retryHttpStatusErrorsForGetOnly = false;
};

QCNetworkRetryPolicy::QCNetworkRetryPolicy()
    : d(new QCNetworkRetryPolicyData)
{
}

QCNetworkRetryPolicy::QCNetworkRetryPolicy(
    int retries, std::chrono::milliseconds initialDelay, double backoff)
    : d(new QCNetworkRetryPolicyData)
{
    d->maxRetries        = retries;
    d->initialDelay      = initialDelay;
    d->backoffMultiplier = backoff;
}

QCNetworkRetryPolicy::QCNetworkRetryPolicy(const QCNetworkRetryPolicy &other) = default;

QCNetworkRetryPolicy::QCNetworkRetryPolicy(QCNetworkRetryPolicy &&other) = default;

QCNetworkRetryPolicy::~QCNetworkRetryPolicy() = default;

QCNetworkRetryPolicy &QCNetworkRetryPolicy::operator=(const QCNetworkRetryPolicy &other) = default;

QCNetworkRetryPolicy &QCNetworkRetryPolicy::operator=(QCNetworkRetryPolicy &&other) = default;

int QCNetworkRetryPolicy::maxRetries() const
{
    return d->maxRetries;
}

void QCNetworkRetryPolicy::setMaxRetries(int retries)
{
    d->maxRetries = retries;
}

std::chrono::milliseconds QCNetworkRetryPolicy::initialDelay() const
{
    return d->initialDelay;
}

void QCNetworkRetryPolicy::setInitialDelay(std::chrono::milliseconds delay)
{
    d->initialDelay = delay;
}

double QCNetworkRetryPolicy::backoffMultiplier() const
{
    return d->backoffMultiplier;
}

void QCNetworkRetryPolicy::setBackoffMultiplier(double multiplier)
{
    d->backoffMultiplier = multiplier;
}

std::chrono::milliseconds QCNetworkRetryPolicy::maxDelay() const
{
    return d->maxDelay;
}

void QCNetworkRetryPolicy::setMaxDelay(std::chrono::milliseconds delay)
{
    d->maxDelay = delay;
}

QSet<NetworkError> QCNetworkRetryPolicy::retryableErrors() const
{
    return d->retryableErrors;
}

void QCNetworkRetryPolicy::setRetryableErrors(const QSet<NetworkError> &errors)
{
    d->retryableErrors = errors;
}

bool QCNetworkRetryPolicy::retryHttpStatusErrorsForGetOnly() const
{
    return d->retryHttpStatusErrorsForGetOnly;
}

void QCNetworkRetryPolicy::setRetryHttpStatusErrorsForGetOnly(bool enabled)
{
    d->retryHttpStatusErrorsForGetOnly = enabled;
}

bool QCNetworkRetryPolicy::shouldRetry(NetworkError error, int attemptCount) const
{
    if (maxRetries() <= 0) {
        return false;
    }

    if (attemptCount >= maxRetries()) {
        return false;
    }

    if (!retryableErrors().contains(error)) {
        return false;
    }

    return true;
}

std::chrono::milliseconds QCNetworkRetryPolicy::delayForAttempt(int attemptCount) const
{
    if (attemptCount < 0) {
        return initialDelay();
    }

    double delayMs = initialDelay().count() * std::pow(backoffMultiplier(), attemptCount);
    delayMs = std::min(delayMs, static_cast<double>(maxDelay().count()));

    return std::chrono::milliseconds(static_cast<long long>(delayMs));
}

std::chrono::milliseconds QCNetworkRetryPolicy::delayForAttempt(
    int attemptCount, std::optional<std::chrono::milliseconds> serverDelay) const
{
    if (serverDelay.has_value()) {
        const auto raw = serverDelay.value();
        if (raw.count() <= 0) {
            return std::chrono::milliseconds(0);
        }
        return std::min(raw, maxDelay());
    }
    return delayForAttempt(attemptCount);
}

QCNetworkRetryPolicy QCNetworkRetryPolicy::noRetry()
{
    return QCNetworkRetryPolicy();
}

QCNetworkRetryPolicy QCNetworkRetryPolicy::standardRetry()
{
    QCNetworkRetryPolicy policy;
    policy.setMaxRetries(3);
    policy.setInitialDelay(std::chrono::milliseconds(1000)); // 1 秒
    policy.setBackoffMultiplier(2.0);
    policy.setMaxDelay(std::chrono::milliseconds(30000)); // 30 秒
    return policy;
}

QCNetworkRetryPolicy QCNetworkRetryPolicy::aggressiveRetry()
{
    QCNetworkRetryPolicy policy;
    policy.setMaxRetries(5);
    policy.setInitialDelay(std::chrono::milliseconds(500)); // 500 毫秒
    policy.setBackoffMultiplier(1.5);
    policy.setMaxDelay(std::chrono::milliseconds(20000)); // 20 秒
    return policy;
}

} // namespace QCurl
