#ifdef QCURL_WEBSOCKET_SUPPORT

#include "QCWebSocketReconnectPolicy.h"
#include <cmath>
#include <algorithm>

namespace QCurl {

// ============================================================
// 构造函数
// ============================================================

QCWebSocketReconnectPolicy::QCWebSocketReconnectPolicy()
    : maxRetries(0)
    , initialDelay(1000)
    , backoffMultiplier(2.0)
    , maxDelay(30000)
{
    // 默认可重连的 CloseCode
    retriableCloseCodes = {
        1001,  // GoingAway
        1006,  // AbnormalClosure
        1011   // InternalError
    };
}

// ============================================================
// 核心方法
// ============================================================

bool QCWebSocketReconnectPolicy::shouldRetry(int closeCode, int attemptCount) const
{
    // 检查是否超过最大重连次数
    if (maxRetries <= 0 || attemptCount > maxRetries) {
        return false;
    }

    // 检查 CloseCode 是否可重连
    return retriableCloseCodes.contains(closeCode);
}

std::chrono::milliseconds QCWebSocketReconnectPolicy::delayForAttempt(int attemptCount) const
{
    if (attemptCount <= 0) {
        return std::chrono::milliseconds(0);
    }

    // 计算指数退避延迟：initialDelay * (backoffMultiplier ^ (attemptCount - 1))
    double exponent = static_cast<double>(attemptCount - 1);
    double multiplier = std::pow(backoffMultiplier, exponent);
    double calculatedDelay = initialDelay.count() * multiplier;

    // 限制在最大延迟范围内
    auto delayMs = static_cast<qint64>(std::min(calculatedDelay, static_cast<double>(maxDelay.count())));
    
    return std::chrono::milliseconds(delayMs);
}

// ============================================================
// 静态工厂方法
// ============================================================

QCWebSocketReconnectPolicy QCWebSocketReconnectPolicy::noReconnect()
{
    QCWebSocketReconnectPolicy policy;
    policy.maxRetries = 0;
    return policy;
}

QCWebSocketReconnectPolicy QCWebSocketReconnectPolicy::standardReconnect()
{
    QCWebSocketReconnectPolicy policy;
    policy.maxRetries = 3;
    policy.initialDelay = std::chrono::milliseconds(1000);
    policy.backoffMultiplier = 2.0;
    policy.maxDelay = std::chrono::milliseconds(30000);
    policy.retriableCloseCodes = {1001, 1006, 1011};
    return policy;
}

QCWebSocketReconnectPolicy QCWebSocketReconnectPolicy::aggressiveReconnect()
{
    QCWebSocketReconnectPolicy policy;
    policy.maxRetries = 10;
    policy.initialDelay = std::chrono::milliseconds(500);
    policy.backoffMultiplier = 1.5;
    policy.maxDelay = std::chrono::milliseconds(60000);
    policy.retriableCloseCodes = {1001, 1006, 1011};
    return policy;
}

} // namespace QCurl

#endif // QCURL_WEBSOCKET_SUPPORT
