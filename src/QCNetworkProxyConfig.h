#ifndef QCNETWORKPROXYCONFIG_H
#define QCNETWORKPROXYCONFIG_H

#include <QString>
#include <QtCore/qglobal.h>

QT_BEGIN_NAMESPACE

namespace QCurl {

/**
 * @brief 代理配置类
 *
 * 用于配置 HTTP/SOCKS 代理，支持多种代理类型和认证。
 *
 * @par 示例：HTTP 代理
 * @code
 * QCNetworkProxyConfig proxy;
 * proxy.type = QCNetworkProxyConfig::ProxyType::Http;
 * proxy.hostName = "proxy.example.com";
 * proxy.port = 8080;
 * request.setProxyConfig(proxy);
 * @endcode
 *
 * @par 示例：SOCKS5 代理（带认证）
 * @code
 * QCNetworkProxyConfig proxy;
 * proxy.type = QCNetworkProxyConfig::ProxyType::Socks5;
 * proxy.hostName = "socks.example.com";
 * proxy.port = 1080;
 * proxy.userName = "user";
 * proxy.password = "pass";
 * request.setProxyConfig(proxy);
 * @endcode
 *
 */
class QCNetworkProxyConfig
{
public:
    /**
     * @brief 代理类型枚举
     */
    enum class ProxyType {
        None,               ///< 无代理（直连）
        Http,               ///< HTTP 代理
        Https,              ///< HTTPS 代理（通过 CONNECT 方法）
        Socks4,             ///< SOCKS4 代理
        Socks4A,            ///< SOCKS4A 代理（支持域名解析）
        Socks5,             ///< SOCKS5 代理
        Socks5Hostname      ///< SOCKS5 代理（远程域名解析）
    };

    /**
     * @brief 代理类型
     *
     * 默认值：None（无代理）
     */
    ProxyType type = ProxyType::None;

    /**
     * @brief 代理服务器主机名或 IP 地址
     */
    QString hostName;

    /**
     * @brief 代理服务器端口号
     *
     * 常见端口：
     * - HTTP: 8080, 3128
     * - SOCKS5: 1080
     */
    quint16 port = 0;

    /**
     * @brief 代理认证用户名（可选）
     */
    QString userName;

    /**
     * @brief 代理认证密码（可选）
     */
    QString password;

    /**
     * @brief 检查配置是否有效
     *
     * 验证规则：
     * - type == None 时始终有效
     * - type != None 时要求 hostName 非空且 port > 0
     *
     * @return bool true 表示配置有效
     */
    [[nodiscard]] bool isValid() const noexcept;
};

} // namespace QCurl
QT_END_NAMESPACE

#endif // QCNETWORKPROXYCONFIG_H
