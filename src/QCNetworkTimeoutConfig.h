/**
 * @file
 * @brief 声明超时配置。
 */

#ifndef QCNETWORKTIMEOUTCONFIG_H
#define QCNETWORKTIMEOUTCONFIG_H

#include "QCGlobal.h"

#include <QSharedDataPointer>

#include <chrono>
#include <optional>

namespace QCurl {

class QCNetworkTimeoutConfigData;

/**
 * @brief 超时配置类
 *
 * 使用 C++17 std::optional 和 std::chrono 提供类型安全的超时配置。
 *
 * @par 超时类型说明
 * - connectTimeout: 连接建立超时（DNS、TCP 与 TLS/QUIC 握手）
 * - receiveIdleTimeout: 建连后等待响应及接收停滞超时，不限制响应总时长
 * - totalTimeout: 整个请求的总超时时间
 * - lowSpeedTime + lowSpeedLimit: 低速检测（长时间低于某速率则超时）
 *
 * @par 示例：设置 30 秒总超时
 * @code
 * QCNetworkTimeoutConfig timeout;
 * timeout.setTotalTimeout(std::chrono::seconds(30));
 * request.setTimeoutConfig(timeout);
 * @endcode
 *
 * @par 示例：低速检测（10秒内速度低于1KB/s则超时）
 * @code
 * QCNetworkTimeoutConfig timeout;
 * timeout.setLowSpeedTime(std::chrono::seconds(10));
 * timeout.setLowSpeedLimit(1024); // bytes/sec
 * request.setTimeoutConfig(timeout);
 * @endcode
 *
 */
class QCURL_EXPORT QCNetworkTimeoutConfig
{
public:
    QCNetworkTimeoutConfig();
    QCNetworkTimeoutConfig(const QCNetworkTimeoutConfig &other);
    QCNetworkTimeoutConfig(QCNetworkTimeoutConfig &&other);
    ~QCNetworkTimeoutConfig();
    QCNetworkTimeoutConfig &operator=(const QCNetworkTimeoutConfig &other);
    QCNetworkTimeoutConfig &operator=(QCNetworkTimeoutConfig &&other);

    /**
     * @brief 连接超时（可选）
     *
     * 对应 libcurl 的 CURLOPT_CONNECTTIMEOUT_MS。
     * 默认：无（使用 libcurl 默认值，通常为 300 秒）
     */
    [[nodiscard]] std::optional<std::chrono::milliseconds> connectTimeout() const;
    void setConnectTimeout(const std::optional<std::chrono::milliseconds> &timeout);

    /**
     * @brief 接收空闲超时；默认未设置，不改变既有行为。
     *
     * 建连完成后及每次实际响应头/正文推进后获得完整时限；上传和空进度不续期。
     * 重试/重定向重新建连不消耗接收时限；接收暂停与背压期间不计时，恢复重新计时。
     * 未设置、nullopt 和 0ms 均禁用；生效范围为 1..2,147,483,647ms。
     * 负值或超界值发送前以 InvalidRequest 拒绝，不截断或饱和。
     * Blocking Extras 在共同值校验后仅对生效正值报 UnsupportedCapability，且不发送请求。
     */
    [[nodiscard]] std::optional<std::chrono::milliseconds> receiveIdleTimeout() const;
    /// 保存可选时限；nullopt 或 0ms 禁用。非法值保留到发送前统一校验。
    void setReceiveIdleTimeout(std::optional<std::chrono::milliseconds> timeout);

    /**
     * @brief 总超时（可选）
     *
     * 整个请求的最大时间，包括连接、发送、接收。
     * 对应 libcurl 的 CURLOPT_TIMEOUT_MS。
     * 默认：无（无限制）
     */
    [[nodiscard]] std::optional<std::chrono::milliseconds> totalTimeout() const;
    void setTotalTimeout(const std::optional<std::chrono::milliseconds> &timeout);

    /**
     * @brief 低速检测时间（可选）
     *
     * 如果在此时间内传输速度低于 lowSpeedLimit，则超时。
     * 对应 libcurl 的 CURLOPT_LOW_SPEED_TIME。
     * 默认：无
     */
    [[nodiscard]] std::optional<std::chrono::seconds> lowSpeedTime() const;
    void setLowSpeedTime(const std::optional<std::chrono::seconds> &timeout);

    /**
     * @brief 低速限制（字节/秒，可选）
     *
     * 与 lowSpeedTime 配合使用。
     * 对应 libcurl 的 CURLOPT_LOW_SPEED_LIMIT。
     * 默认：无
     */
    [[nodiscard]] std::optional<long> lowSpeedLimit() const;
    void setLowSpeedLimit(const std::optional<long> &bytesPerSecond);

    /**
     * @brief 返回默认配置
     *
     * 所有超时选项均未设置（std::nullopt），使用 libcurl 默认行为。
     *
     * @return QCNetworkTimeoutConfig 默认配置实例
     */
    [[nodiscard]] static QCNetworkTimeoutConfig defaultConfig();

private:
    QSharedDataPointer<QCNetworkTimeoutConfigData> d;
};

} // namespace QCurl

#endif // QCNETWORKTIMEOUTCONFIG_H
