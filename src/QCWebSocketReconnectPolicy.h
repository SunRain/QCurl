#ifndef QCWEBSOCKETRECONNECTPOLICY_H
#define QCWEBSOCKETRECONNECTPOLICY_H

#ifdef QCURL_WEBSOCKET_SUPPORT

#include <chrono>
#include <QSet>
#include <QtGlobal>

namespace QCurl {

/**
 * @brief WebSocket 自动重连策略配置
 * 
 * 提供 WebSocket 连接断开后的自动重连参数配置，包括：
 * - 最大重连次数
 * - 指数退避算法参数（初始延迟、退避乘数、最大延迟）
 * - 可重连的 CloseCode 集合
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
 * QCWebSocket socket(QUrl("wss://example.com"));
 * socket.setReconnectPolicy(QCWebSocketReconnectPolicy::standardReconnect());
 * 
 * // 自定义重连策略
 * QCWebSocketReconnectPolicy policy;
 * policy.maxRetries = 5;
 * policy.initialDelay = std::chrono::milliseconds(2000);
 * policy.backoffMultiplier = 1.5;
 * policy.retriableCloseCodes = {1001, 1006, 1011};
 * socket.setReconnectPolicy(policy);
 * @endcode
 * 
 */
class QCWebSocketReconnectPolicy
{
public:
    /**
     * @brief 默认构造函数（不重连）
     * 
     * 创建一个不启用自动重连的策略（maxRetries = 0）。
     * 这是 QCWebSocket 的默认行为。
     */
    QCWebSocketReconnectPolicy();

    // ============================================================
    // 重连参数
    // ============================================================

    /**
     * @brief 最大重连次数
     * 
     * 当连接断开时，最多尝试重连的次数。
     * 
     * - 0：不重连（默认）
     * - >0：最多重连 N 次
     */
    int maxRetries = 0;

    /**
     * @brief 初始延迟时间（毫秒）
     * 
     * 第一次重连尝试的延迟时间。
     * 后续重连延迟将根据指数退避算法递增。
     * 
     * @note 默认值：1000ms (1 秒)
     */
    std::chrono::milliseconds initialDelay{1000};

    /**
     * @brief 退避乘数
     * 
     * 每次重连失败后，延迟时间的乘数因子。
     * 
     * - 1.0：固定延迟（不推荐）
     * - 2.0：标准指数退避（推荐）
     * - 1.5：较温和的退避
     * 
     * @note 默认值：2.0
     */
    double backoffMultiplier = 2.0;

    /**
     * @brief 最大延迟时间（毫秒）
     * 
     * 重连延迟的上限值，防止延迟时间无限增长。
     * 
     * @note 默认值：30000ms (30 秒)
     */
    std::chrono::milliseconds maxDelay{30000};

    /**
     * @brief 可重连的 CloseCode 集合
     * 
     * 当 WebSocket 关闭时，只有这些 CloseCode 才会触发自动重连。
     * 
     * 默认可重连的 CloseCode：
     * - 1001 (GoingAway): 端点离开
     * - 1006 (AbnormalClosure): 异常关闭（如网络中断）
     * - 1011 (InternalError): 服务器内部错误
     * 
     * 不应重连的 CloseCode（通常是客户端错误）：
     * - 1000 (Normal): 正常关闭
     * - 1002 (ProtocolError): 协议错误
     * - 1003 (UnsupportedData): 不支持的数据类型
     * - 1007 (InvalidPayload): 无效载荷
     * - 1008 (PolicyViolation): 策略违规
     */
    QSet<int> retriableCloseCodes = {
        1001,  // GoingAway
        1006,  // AbnormalClosure
        1011   // InternalError
    };

    // ============================================================
    // 核心方法
    // ============================================================

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
    [[nodiscard]] bool shouldRetry(int closeCode, int attemptCount) const;

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

    // ============================================================
    // 静态工厂方法
    // ============================================================

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
};

} // namespace QCurl

#endif // QCURL_WEBSOCKET_SUPPORT
#endif // QCWEBSOCKETRECONNECTPOLICY_H
