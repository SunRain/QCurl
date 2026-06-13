/**
 * @file
 * @brief 声明 WebSocket 重连策略。
 */

#ifndef QCWEBSOCKETRECONNECTPOLICY_H
#define QCWEBSOCKETRECONNECTPOLICY_H

#ifdef QCURL_WEBSOCKET_SUPPORT

#include "QCGlobal.h"
#include "QCWebSocket.h"

#include <QSet>
#include <QSharedDataPointer>

#include <chrono>

namespace QCurl {

class QCWebSocketReconnectPolicyData;

/**
 * @brief WebSocket 自动重连策略配置
 *
 * 提供 WebSocket 连接断开后的自动重连参数配置，包括：
 * - 最大重连次数
 * - 指数退避算法参数（初始延迟、退避乘数、最大延迟）
 * - 可重连的强类型 CloseCode 集合
 *
 * 该类型使用 accessor-only shared-data 形式保持 ABI 友好。
 *
 * @par 指数退避算法
 * 延迟时间计算公式：
 * @code
 * delay = min(initialDelay * (backoffMultiplier ^ (attemptCount - 1)), maxDelay)
 * @endcode
 *
 * @par 示例
 * @code
 * // 使用标准重连策略
 * QCWebSocketOptions options;
 * options.setReconnectPolicy(QCWebSocketReconnectPolicy::standardReconnect());
 * QCWebSocket socket(QUrl("wss://example.com"), options);
 *
 * // 自定义重连策略
 * QCWebSocketReconnectPolicy policy;
 * policy.setMaxRetries(5);
 * policy.setInitialDelay(std::chrono::milliseconds(2000));
 * policy.setBackoffMultiplier(1.5);
 * policy.setRetriableCloseCodes({QCWebSocket::CloseCode::GoingAway,
                                QCWebSocket::CloseCode::AbnormalClosure,
                                QCWebSocket::CloseCode::InternalError});
 * options.setReconnectPolicy(policy);
 * @endcode
 *
 */
class QCURL_OTHER_EXTRAS_EXPORT QCWebSocketReconnectPolicy
{
public:
    /**
     * @brief 默认构造函数（不重连）
     *
     * 创建一个不启用自动重连的策略（maxRetries = 0）。
     * 这是 QCWebSocket 的默认行为。
     */
    QCWebSocketReconnectPolicy();
    QCWebSocketReconnectPolicy(const QCWebSocketReconnectPolicy &other);
    QCWebSocketReconnectPolicy(QCWebSocketReconnectPolicy &&other) noexcept;
    ~QCWebSocketReconnectPolicy();

    QCWebSocketReconnectPolicy &operator=(const QCWebSocketReconnectPolicy &other);
    QCWebSocketReconnectPolicy &operator=(QCWebSocketReconnectPolicy &&other) noexcept;

    // ==================
    // 重连参数
    // ==================

    /// 返回最大重连次数；0 表示不重连。
    [[nodiscard]] int maxRetries() const noexcept;
    void setMaxRetries(int maxRetries) noexcept;

    /// 返回第一次重连尝试的延迟。
    [[nodiscard]] std::chrono::milliseconds initialDelay() const noexcept;
    void setInitialDelay(std::chrono::milliseconds delay) noexcept;

    /// 返回指数退避乘数。
    [[nodiscard]] double backoffMultiplier() const noexcept;
    void setBackoffMultiplier(double multiplier) noexcept;

    /// 返回重连延迟上限。
    [[nodiscard]] std::chrono::milliseconds maxDelay() const noexcept;
    void setMaxDelay(std::chrono::milliseconds delay) noexcept;

    /// 返回允许触发重连的 WebSocket CloseCode 集合。
    using CloseCodeSet = QSet<QCWebSocket::CloseCode>;

    [[nodiscard]] CloseCodeSet retriableCloseCodes() const;
    void setRetriableCloseCodes(const CloseCodeSet &closeCodes);

    // ==================
    // 核心方法
    // ==================

    /**
     * @brief 判断是否应该重连
     *
     * @param closeCode WebSocket 关闭状态码
     * @param attemptCount 当前重连尝试次数（从 1 开始）
     * @return true 如果应该重连，false 否则
     *
     * 判断条件：
     * 1. attemptCount <= maxRetries
     * 2. closeCode 在 retriableCloseCodes 集合中
     */
    [[nodiscard]] bool shouldRetry(QCWebSocket::CloseCode closeCode, int attemptCount) const;

    /**
     * @brief 计算重连延迟时间
     *
     * 使用指数退避算法计算延迟：
     * delay = min(initialDelay * (backoffMultiplier ^ (attemptCount - 1)), maxDelay)
     *
     * @param attemptCount 重连尝试次数（从 1 开始）
     * @return 延迟时间（毫秒）
     *
     * @par 示例（standardReconnect）
     * - attemptCount = 1: 1000 * (2^0) = 1000ms (1 秒)
     * - attemptCount = 2: 1000 * (2^1) = 2000ms (2 秒)
     * - attemptCount = 3: 1000 * (2^2) = 4000ms (4 秒)
     */
    [[nodiscard]] std::chrono::milliseconds delayForAttempt(int attemptCount) const;

    // ==================
    // 静态工厂方法
    // ==================

    /**
     * @brief 创建无重连策略（默认）
     *
     * @return 不启用自动重连的策略（maxRetries = 0）
     */
    [[nodiscard]] static QCWebSocketReconnectPolicy noReconnect();

    /**
     * @brief 创建标准重连策略
     *
     * 适用于大多数场景的重连策略：
     * - maxRetries: 3
     * - initialDelay: 1000ms (1 秒)
     * - backoffMultiplier: 2.0
     * - maxDelay: 30000ms (30 秒)
     *
     * 延迟序列：1s → 2s → 4s
     *
     * @return 标准重连策略
     */
    [[nodiscard]] static QCWebSocketReconnectPolicy standardReconnect();

    /**
     * @brief 创建激进重连策略
     *
     * 适用于需要快速恢复连接的场景：
     * - maxRetries: 10
     * - initialDelay: 500ms (0.5 秒)
     * - backoffMultiplier: 1.5
     * - maxDelay: 60000ms (60 秒)
     *
     * 延迟序列：0.5s → 0.75s → 1.125s → 1.688s → 2.531s → ...
     *
     * @return 激进重连策略
     */
    [[nodiscard]] static QCWebSocketReconnectPolicy aggressiveReconnect();

private:
    QSharedDataPointer<QCWebSocketReconnectPolicyData> d;
};

} // namespace QCurl

#endif // QCURL_WEBSOCKET_SUPPORT
#endif // QCWEBSOCKETRECONNECTPOLICY_H
