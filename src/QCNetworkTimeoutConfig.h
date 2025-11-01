#ifndef QCNETWORKTIMEOUTCONFIG_H
#define QCNETWORKTIMEOUTCONFIG_H

#include <chrono>
#include <optional>
#include <QtCore/qglobal.h>

QT_BEGIN_NAMESPACE

namespace QCurl {

/**
 * @brief 超时配置类
 *
 * 使用 C++17 std::optional 和 std::chrono 提供类型安全的超时配置。
 *
 * @par 超时类型说明
 * - connectTimeout: 连接建立超时（TCP 三次握手）
 * - totalTimeout: 整个请求的总超时时间
 * - lowSpeedTime + lowSpeedLimit: 低速检测（长时间低于某速率则超时）
 *
 * @par 示例：设置 30 秒总超时
 * @code
 * QCNetworkTimeoutConfig timeout;
 * timeout.totalTimeout = std::chrono::seconds(30);
 * request.setTimeoutConfig(timeout);
 * @endcode
 *
 * @par 示例：低速检测（10秒内速度低于1KB/s则超时）
 * @code
 * QCNetworkTimeoutConfig timeout;
 * timeout.lowSpeedTime = std::chrono::seconds(10);
 * timeout.lowSpeedLimit = 1024; // bytes/sec
 * request.setTimeoutConfig(timeout);
 * @endcode
 *
 */
class QCNetworkTimeoutConfig
{
public:
    /**
     * @brief 连接超时（可选）
     *
     * 对应 libcurl 的 CURLOPT_CONNECTTIMEOUT_MS。
     * 默认：无（使用 libcurl 默认值，通常为 300 秒）
     */
    std::optional<std::chrono::milliseconds> connectTimeout;

    /**
     * @brief 总超时（可选）
     *
     * 整个请求的最大时间，包括连接、发送、接收。
     * 对应 libcurl 的 CURLOPT_TIMEOUT_MS。
     * 默认：无（无限制）
     */
    std::optional<std::chrono::milliseconds> totalTimeout;

    /**
     * @brief 低速检测时间（可选）
     *
     * 如果在此时间内传输速度低于 lowSpeedLimit，则超时。
     * 对应 libcurl 的 CURLOPT_LOW_SPEED_TIME。
     * 默认：无
     */
    std::optional<std::chrono::seconds> lowSpeedTime;

    /**
     * @brief 低速限制（字节/秒，可选）
     *
     * 与 lowSpeedTime 配合使用。
     * 对应 libcurl 的 CURLOPT_LOW_SPEED_LIMIT。
     * 默认：无
     */
    std::optional<long> lowSpeedLimit; // bytes per second

    /**
     * @brief 返回默认配置
     *
     * 所有超时选项均未设置（std::nullopt），使用 libcurl 默认行为。
     *
     * @return QCNetworkTimeoutConfig 默认配置实例
     */
    [[nodiscard]] static QCNetworkTimeoutConfig defaultConfig();
};

} // namespace QCurl
QT_END_NAMESPACE

#endif // QCNETWORKTIMEOUTCONFIG_H
