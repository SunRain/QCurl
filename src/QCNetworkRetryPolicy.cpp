#include "QCNetworkRetryPolicy.h"

#include "private/QCNetworkRetryPolicy_p.h"

#include <QRandomGenerator>
#include <QSharedData>

#include <algorithm>
#include <cmath>
#include <utility>

namespace QCurl {

namespace {

constexpr int kNoRetries                      = 0;
constexpr int kStandardMaxRetries             = 3;
constexpr int kAggressiveMaxRetries           = 5;
constexpr double kDefaultBackoffMultiplier    = 2.0;
constexpr double kAggressiveBackoffMultiplier = 1.5;
constexpr std::chrono::milliseconds kDefaultInitialDelay{1000};
constexpr std::chrono::milliseconds kDefaultMaxDelay{30000};
constexpr std::chrono::milliseconds kAggressiveInitialDelay{500};
constexpr std::chrono::milliseconds kAggressiveMaxDelay{20000};

} // namespace

/// @brief 保存请求重试与退避规则的隐式共享策略。
class QCNetworkRetryPolicyData : public QSharedData
{
public:
    int maxRetries                         = kNoRetries;
    std::chrono::milliseconds initialDelay = kDefaultInitialDelay;
    double backoffMultiplier               = kDefaultBackoffMultiplier;
    std::chrono::milliseconds maxDelay     = kDefaultMaxDelay;
    QSet<NetworkError> retryableErrors     = {// 网络层临时性错误
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
    QCNetworkRetryMethodPolicy retryMethodPolicy = QCNetworkRetryMethodPolicy::GetHeadOnly;
};

using Milliseconds   = std::chrono::milliseconds;
using MillisecondRep = Milliseconds::rep;

[[nodiscard]] MillisecondRep boundedExponentialDelay(const QCNetworkRetryPolicyData &data,
                                                     int attemptCount)
{
    const MillisecondRep initial = data.initialDelay.count();
    const MillisecondRep maximum = data.maxDelay.count();
    if (attemptCount < 0 || initial <= 0 || maximum <= 0) {
        return std::min(initial, maximum);
    }
    if (initial >= maximum) {
        return maximum;
    }

    const long double multiplier = static_cast<long double>(data.backoffMultiplier);
    if (multiplier <= 1.0L) {
        const long double factor = std::pow(multiplier, attemptCount);
        if (!std::isfinite(factor)) {
            return maximum;
        }
        const long double value = static_cast<long double>(initial) * factor;
        if (!std::isfinite(value) || value >= static_cast<long double>(maximum)) {
            return maximum;
        }
        return static_cast<MillisecondRep>(std::max(0.0L, value));
    }

    long double value = static_cast<long double>(initial);
    if (value == 0.0L) {
        return 0;
    }
    for (int index = 0; index < attemptCount; ++index) {
        if (value >= static_cast<long double>(maximum)) {
            return maximum;
        }
        const long double next = value * multiplier;
        if (!std::isfinite(next) || next >= static_cast<long double>(maximum)) {
            return maximum;
        }
        value = next;
    }

    return static_cast<MillisecondRep>(std::max(0.0L, value));
}

[[nodiscard]] double jitterFraction()
{
#ifdef QCURL_ENABLE_TEST_HOOKS
    bool ok                = false;
    const double testValue = qEnvironmentVariable(Internal::kRetryJitterFractionTestEnv)
                                 .toDouble(&ok);
    if (ok && std::isfinite(testValue)) {
        return std::clamp(testValue, 0.0, 1.0);
    }
#endif
    return QRandomGenerator::global()->generateDouble();
}

[[nodiscard]] MillisecondRep equalJitterDelay(const QCNetworkRetryPolicyData &data, int attemptCount)
{
    const MillisecondRep base = boundedExponentialDelay(data, attemptCount);
    if (base <= 0) {
        return 0;
    }

    const MillisecondRep half = base / 2;
    const MillisecondRep span = base - half;
    const auto jitter         = static_cast<MillisecondRep>(jitterFraction()
                                                            * static_cast<long double>(span));
    return std::min(base, static_cast<MillisecondRep>(half + jitter));
}

QCNetworkRetryPolicy::QCNetworkRetryPolicy()
    : d(new QCNetworkRetryPolicyData)
{}

QCNetworkRetryPolicy::QCNetworkRetryPolicy(const QCNetworkRetryPolicy &other) = default;

QCNetworkRetryPolicy::QCNetworkRetryPolicy(QCNetworkRetryPolicy &&other) = default;

QCNetworkRetryPolicy::~QCNetworkRetryPolicy() = default;

QCNetworkRetryPolicy &QCNetworkRetryPolicy::operator=(const QCNetworkRetryPolicy &other) = default;

QCNetworkRetryPolicy &QCNetworkRetryPolicy::operator=(QCNetworkRetryPolicy &&other) = default;

int QCNetworkRetryPolicy::maxRetries() const
{
    return d->maxRetries;
}

QCNetworkRetryPolicy::UpdateResult QCNetworkRetryPolicy::setMaxRetries(int retries)
{
    if (retries < 0) {
        return UpdateResult::InvalidArgument;
    }
    d->maxRetries = retries;
    return UpdateResult::Applied;
}

std::chrono::milliseconds QCNetworkRetryPolicy::initialDelay() const
{
    return d->initialDelay;
}

QCNetworkRetryPolicy::UpdateResult QCNetworkRetryPolicy::setInitialDelay(
    std::chrono::milliseconds delay)
{
    if (delay.count() < 0) {
        return UpdateResult::InvalidArgument;
    }
    d->initialDelay = delay;
    return UpdateResult::Applied;
}

double QCNetworkRetryPolicy::backoffMultiplier() const
{
    return d->backoffMultiplier;
}

QCNetworkRetryPolicy::UpdateResult QCNetworkRetryPolicy::setBackoffMultiplier(double multiplier)
{
    if (!std::isfinite(multiplier) || multiplier < 0.0) {
        return UpdateResult::InvalidArgument;
    }
    d->backoffMultiplier = multiplier;
    return UpdateResult::Applied;
}

std::chrono::milliseconds QCNetworkRetryPolicy::maxDelay() const
{
    return d->maxDelay;
}

QCNetworkRetryPolicy::UpdateResult QCNetworkRetryPolicy::setMaxDelay(
    std::chrono::milliseconds delay)
{
    if (delay.count() < 0) {
        return UpdateResult::InvalidArgument;
    }
    d->maxDelay = delay;
    return UpdateResult::Applied;
}

QSet<NetworkError> QCNetworkRetryPolicy::retryableErrors() const
{
    return d->retryableErrors;
}

void QCNetworkRetryPolicy::setRetryableErrors(const QSet<NetworkError> &errors)
{
    d->retryableErrors = errors;
}

QCNetworkRetryMethodPolicy QCNetworkRetryPolicy::retryMethodPolicy() const
{
    return d->retryMethodPolicy;
}

QCNetworkRetryPolicy::UpdateResult QCNetworkRetryPolicy::setRetryMethodPolicy(
    QCNetworkRetryMethodPolicy policy)
{
    switch (policy) {
        case QCNetworkRetryMethodPolicy::GetHeadOnly:
        case QCNetworkRetryMethodPolicy::AllowExplicitIdempotencyKey:
            d->retryMethodPolicy = policy;
            return UpdateResult::Applied;
    }
    return UpdateResult::InvalidArgument;
}

QCNetworkRetryPolicy::UpdateResult QCNetworkRetryPolicy::tryCreate(
    int retries,
    std::chrono::milliseconds initialDelay,
    double backoff,
    QCNetworkRetryPolicy *output)
{
    if (!output) {
        return UpdateResult::InvalidArgument;
    }

    QCNetworkRetryPolicy candidate;
    if (candidate.setMaxRetries(retries) != UpdateResult::Applied
        || candidate.setInitialDelay(initialDelay) != UpdateResult::Applied
        || candidate.setBackoffMultiplier(backoff) != UpdateResult::Applied) {
        return UpdateResult::InvalidArgument;
    }

    *output = std::move(candidate);
    return UpdateResult::Applied;
}

bool QCNetworkRetryPolicy::shouldRetry(NetworkError error, int attemptCount) const
{
    if (maxRetries() <= 0) {
        return false;
    }

    if (attemptCount < 0 || attemptCount >= maxRetries()) {
        return false;
    }

    if (!retryableErrors().contains(error)) {
        return false;
    }

    return true;
}

std::chrono::milliseconds QCNetworkRetryPolicy::delayForAttempt(int attemptCount) const
{
    return Milliseconds(equalJitterDelay(*d, attemptCount));
}

std::chrono::milliseconds QCNetworkRetryPolicy::delayForAttempt(
    int attemptCount, std::optional<std::chrono::milliseconds> serverDelay) const
{
    const Milliseconds clientDelay = delayForAttempt(attemptCount);
    if (!serverDelay.has_value() || serverDelay->count() <= 0) {
        return clientDelay;
    }

    const Milliseconds serverLowerBound = std::min(serverDelay.value(), maxDelay());
    return std::min(std::max(clientDelay, serverLowerBound), maxDelay());
}

QCNetworkRetryPolicy QCNetworkRetryPolicy::noRetry()
{
    return QCNetworkRetryPolicy();
}

QCNetworkRetryPolicy QCNetworkRetryPolicy::standardRetry()
{
    QCNetworkRetryPolicy policy;
    policy.d->maxRetries         = kStandardMaxRetries;
    policy.d->initialDelay       = kDefaultInitialDelay;
    policy.d->backoffMultiplier = kDefaultBackoffMultiplier;
    policy.d->maxDelay           = kDefaultMaxDelay;
    return policy;
}

QCNetworkRetryPolicy QCNetworkRetryPolicy::aggressiveRetry()
{
    QCNetworkRetryPolicy policy;
    policy.d->maxRetries         = kAggressiveMaxRetries;
    policy.d->initialDelay       = kAggressiveInitialDelay;
    policy.d->backoffMultiplier = kAggressiveBackoffMultiplier;
    policy.d->maxDelay           = kAggressiveMaxDelay;
    return policy;
}

} // namespace QCurl
