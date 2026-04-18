/**
 * @file
 * @brief 声明重试策略。
 */

#ifndef QCNETWORKRETRYPOLICY_H
#define QCNETWORKRETRYPOLICY_H

#include "QCNetworkError.h"

#include <QSet>
#include <QSharedDataPointer>

#include <chrono>
#include <optional>

namespace QCurl {

class QCNetworkRetryPolicyData;

/**
 * @brief 网络请求重试策略配置类
 *
 * 定义了网络请求失败后的重试行为，包括：
 * - 最大重试次数
 * - 指数退避算法参数
 * - 可重试的错误类型
 *
 * @par 指数退避算法
 * 延迟时间计算公式：
 * \code
 * delay = min(initialDelay * (backoffMultiplier ^ attemptCount), maxDelay)
 * \endcode
 *
 * @par 使用示例
 * \code
 * QCNetworkRetryPolicy policy;
 * policy.setMaxRetries(3);               // 最多重试 3 次
 * policy.setInitialDelay(std::chrono::milliseconds(1000)); // 初始延迟 1 秒
 * policy.setBackoffMultiplier(2.0);      // 每次延迟翻倍
 * policy.setMaxDelay(std::chrono::milliseconds(30000));    // 最大延迟 30 秒
 *
 * QCNetworkRequest request(QUrl("https://example.com/api"));
 * request.setRetryPolicy(policy);
 * \endcode
 *
 */
class QCURL_EXPORT QCNetworkRetryPolicy
{
public:
    /**
     * @brief 默认构造函数
     *
     * 创建一个禁用重试的策略（maxRetries = 0）
     */
    QCNetworkRetryPolicy();

    /**
     * @brief 创建启用重试的策略
     *
     * @param retries 最大重试次数（>0 表示启用重试）
     * @param initialDelay 初始延迟
     * @param backoff 指数退避倍数
     */
    explicit QCNetworkRetryPolicy(
        int retries,
        std::chrono::milliseconds initialDelay = std::chrono::milliseconds{1000},
        double backoff = 2.0);

    QCNetworkRetryPolicy(const QCNetworkRetryPolicy &other);
    QCNetworkRetryPolicy(QCNetworkRetryPolicy &&other);
    ~QCNetworkRetryPolicy();
    QCNetworkRetryPolicy &operator=(const QCNetworkRetryPolicy &other);
    QCNetworkRetryPolicy &operator=(QCNetworkRetryPolicy &&other);

    // ==================
    // 配置参数
    // ==================

    /**
     * @brief 最大重试次数
     *
     * - 0 = 禁用重试（默认）
     * - >0 = 失败后最多重试的次数
     *
     * @note 总请求次数 = 1 + maxRetries
     */
    [[nodiscard]] int maxRetries() const;
    void setMaxRetries(int retries);

    /**
     * @brief 初始延迟时间
     *
     * 第一次重试前的等待时间。
     * 后续延迟将根据指数退避算法递增。
     */
    [[nodiscard]] std::chrono::milliseconds initialDelay() const;
    void setInitialDelay(std::chrono::milliseconds delay);

    /**
     * @brief 指数退避倍数
     *
     * 每次重试的延迟倍数：
     * - 1.0 = 固定延迟
     * - 2.0 = 每次延迟翻倍（推荐）
     * - 1.5 = 每次延迟增加 50%
     */
    [[nodiscard]] double backoffMultiplier() const;
    void setBackoffMultiplier(double multiplier);

    /**
     * @brief 最大延迟时间
     *
     * 防止延迟时间无限增长的上限。
     */
    [[nodiscard]] std::chrono::milliseconds maxDelay() const;
    void setMaxDelay(std::chrono::milliseconds delay);

    /**
     * @brief 可重试的错误集合
     *
     * 只有这些错误类型会触发重试。
     * 默认包含临时性网络错误和 HTTP 5xx 错误。
     */
    [[nodiscard]] QSet<NetworkError> retryableErrors() const;
    void setRetryableErrors(const QSet<NetworkError> &errors);

    /**
     * @brief HTTP 状态码重试的 method 限制（与 legendary 迁移语义对齐）
     *
     * 当启用后：
     * - 对于 HTTP 4xx/5xx（isHttpError() == true）触发的重试，仅允许 GET。
     * - libcurl 层网络错误（ConnectionTimeout 等）不受该限制。
     *
     * 默认关闭以保持兼容性。
     */
    [[nodiscard]] bool retryHttpStatusErrorsForGetOnly() const;
    void setRetryHttpStatusErrorsForGetOnly(bool enabled);

    // ==================
    // 公共方法
    // ==================

    /**
     * @brief 判断是否应该重试
     *
     * 检查以下条件：
     * 1. 重试功能已启用（maxRetries > 0）
     * 2. 当前尝试次数未超过最大重试次数
     * 3. 错误类型在可重试列表中
     *
     * @param error 当前请求的错误码
     * @param attemptCount 当前尝试次数（从 0 开始）
     * @return bool true 表示应该重试
     */
    [[nodiscard]] bool shouldRetry(NetworkError error, int attemptCount) const;

    /**
     * @brief 计算指定尝试次数的延迟时间
     *
     * 使用指数退避算法：
     * \code
     * delay = min(initialDelay * (backoffMultiplier ^ attemptCount), maxDelay)
     * \endcode
     *
     * @param attemptCount 尝试次数（从 0 开始）
     * @return std::chrono::milliseconds 延迟时间
     *
     * @par 示例（initialDelay=1000ms, backoffMultiplier=2.0, maxDelay=30000ms）
     * - attemptCount=0: 1000ms
     * - attemptCount=1: 2000ms
     * - attemptCount=2: 4000ms
     * - attemptCount=3: 8000ms
     * - attemptCount=4: 16000ms
     * - attemptCount=5: 30000ms (达到上限)
     */
    [[nodiscard]] std::chrono::milliseconds delayForAttempt(int attemptCount) const;

    /**
     * @brief 计算延迟时间（支持服务端 Retry-After 覆写）
     *
     * @param attemptCount 尝试次数（从 0 开始）
     * @param serverDelay 服务端建议延迟（例如 Retry-After）；若存在则优先使用，并以 maxDelay 为上限
     */
    [[nodiscard]] std::chrono::milliseconds delayForAttempt(
        int attemptCount, std::optional<std::chrono::milliseconds> serverDelay) const;

    /**
     * @brief 检查重试功能是否启用
     *
     * @return bool true 表示 maxRetries > 0
     */
    [[nodiscard]] bool isEnabled() const noexcept { return maxRetries() > 0; }

    /**
     * @brief 创建默认的重试策略（禁用重试）
     *
     * @return QCNetworkRetryPolicy 禁用重试的策略对象
     */
    [[nodiscard]] static QCNetworkRetryPolicy noRetry();

    /**
     * @brief 创建标准的重试策略
     *
     * 配置：
     * - 最多重试 3 次
     * - 初始延迟 1 秒
     * - 指数退避倍数 2.0
     * - 最大延迟 30 秒
     *
     * @return QCNetworkRetryPolicy 标准重试策略对象
     */
    [[nodiscard]] static QCNetworkRetryPolicy standardRetry();

    /**
     * @brief 创建激进的重试策略（用于关键请求）
     *
     * 配置：
     * - 最多重试 5 次
     * - 初始延迟 500 毫秒
     * - 指数退避倍数 1.5
     * - 最大延迟 20 秒
     *
     * @return QCNetworkRetryPolicy 激进重试策略对象
     */
    [[nodiscard]] static QCNetworkRetryPolicy aggressiveRetry();

private:
    QSharedDataPointer<QCNetworkRetryPolicyData> d;
};

} // namespace QCurl

#endif // QCNETWORKRETRYPOLICY_H
