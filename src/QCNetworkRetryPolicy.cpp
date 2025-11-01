#include "QCNetworkRetryPolicy.h"
#include <cmath>
#include <algorithm>

QT_BEGIN_NAMESPACE

namespace QCurl {

// ============================================================================
// 构造函数
// ============================================================================

QCNetworkRetryPolicy::QCNetworkRetryPolicy()
    : maxRetries(0)
    , initialDelay(1000)
    , backoffMultiplier(2.0)
    , maxDelay(30000)
{
}

QCNetworkRetryPolicy::QCNetworkRetryPolicy(int retries, int initialDelayMs, double backoff)
    : maxRetries(retries)
    , initialDelay(initialDelayMs)
    , backoffMultiplier(backoff)
    , maxDelay(30000)
{
}

// ============================================================================
// 核心重试逻辑
// ============================================================================

bool QCNetworkRetryPolicy::shouldRetry(NetworkError error, int attemptCount) const
{
    // 1. 检查重试功能是否启用
    if (maxRetries <= 0) {
        return false;
    }

    // 2. 检查是否已达到最大重试次数
    // attemptCount 从 0 开始，第一次尝试是 attemptCount=0
    // 如果 maxRetries=3，那么 attemptCount 可以是 0,1,2,3（共4次尝试）
    if (attemptCount >= maxRetries) {
        return false;
    }

    // 3. 检查错误是否在可重试列表中
    if (!retryableErrors.contains(error)) {
        return false;
    }

    return true;
}

std::chrono::milliseconds QCNetworkRetryPolicy::delayForAttempt(int attemptCount) const
{
    // 指数退避算法：delay = initialDelay * (backoffMultiplier ^ attemptCount)
    // attemptCount=0 时：delay = initialDelay * 1 = initialDelay
    // attemptCount=1 时：delay = initialDelay * backoffMultiplier
    // attemptCount=2 时：delay = initialDelay * (backoffMultiplier ^ 2)
    // ...

    if (attemptCount < 0) {
        return initialDelay;
    }

    // 计算延迟（使用 double 避免整数溢出）
    double delayMs = initialDelay.count() * std::pow(backoffMultiplier, attemptCount);

    // 限制在 maxDelay 范围内
    delayMs = std::min(delayMs, static_cast<double>(maxDelay.count()));

    return std::chrono::milliseconds(static_cast<long long>(delayMs));
}

// ============================================================================
// 静态工厂方法
// ============================================================================

QCNetworkRetryPolicy QCNetworkRetryPolicy::noRetry()
{
    return QCNetworkRetryPolicy();  // maxRetries = 0
}

QCNetworkRetryPolicy QCNetworkRetryPolicy::standardRetry()
{
    QCNetworkRetryPolicy policy;
    policy.maxRetries = 3;
    policy.initialDelay = std::chrono::milliseconds(1000);   // 1 秒
    policy.backoffMultiplier = 2.0;
    policy.maxDelay = std::chrono::milliseconds(30000);      // 30 秒
    return policy;
}

QCNetworkRetryPolicy QCNetworkRetryPolicy::aggressiveRetry()
{
    QCNetworkRetryPolicy policy;
    policy.maxRetries = 5;
    policy.initialDelay = std::chrono::milliseconds(500);    // 500 毫秒
    policy.backoffMultiplier = 1.5;
    policy.maxDelay = std::chrono::milliseconds(20000);      // 20 秒
    return policy;
}

} // namespace QCurl
QT_END_NAMESPACE
