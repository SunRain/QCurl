/**
 * @file
 * @brief 声明代理配置。
 */

#ifndef QCNETWORKPROXYCONFIG_H
#define QCNETWORKPROXYCONFIG_H

#include "QCGlobal.h"
#include "QCNetworkSslConfig.h"

#include <QSharedDataPointer>
#include <QString>

#include <optional>

namespace QCurl {

class QCNetworkProxyConfigData;
class QCNetworkProxyTlsConfigData;

/**
 * @brief 代理配置类
 *
 * 用于配置 HTTP/SOCKS 代理，支持多种代理类型和认证。
 *
 * @par 示例：HTTP 代理
 * @code
 * QCNetworkProxyConfig proxy;
 * proxy.setType(QCNetworkProxyConfig::ProxyType::Http);
 * proxy.setHostName("proxy.example.com");
 * proxy.setPort(8080);
 * request.setProxyConfig(proxy);
 * @endcode
 *
 * @par 示例：SOCKS5 代理（带认证）
 * @code
 * QCNetworkProxyConfig proxy;
 * proxy.setType(QCNetworkProxyConfig::ProxyType::Socks5);
 * proxy.setHostName("socks.example.com");
 * proxy.setPort(1080);
 * proxy.setUserName("user");
 * proxy.setPassword("pass");
 * request.setProxyConfig(proxy);
 * @endcode
 *
 */
class QCURL_EXPORT QCNetworkProxyConfig
{
public:
    /**
     * @brief 代理类型枚举
     */
    enum class ProxyType {
        None,          ///< 无代理（直连）
        Http,          ///< HTTP 代理
        Https,         ///< HTTPS 代理（通过 CONNECT 方法）
        Socks4,        ///< SOCKS4 代理
        Socks4A,       ///< SOCKS4A 代理（支持域名解析）
        Socks5,        ///< SOCKS5 代理
        Socks5Hostname ///< SOCKS5 代理（远程域名解析）
    };

    /**
     * @brief HTTPS proxy 的 TLS 配置（可选）
     *
     * 仅在 type == ProxyType::Https 且 tlsConfig.has_value() 时生效。
     * 默认不设置，避免 silent behavior change。
     */
    class QCURL_EXPORT ProxyTlsConfig
    {
    public:
        ProxyTlsConfig();
        ProxyTlsConfig(const ProxyTlsConfig &other);
        ProxyTlsConfig(ProxyTlsConfig &&other);
        ~ProxyTlsConfig();
        ProxyTlsConfig &operator=(const ProxyTlsConfig &other);
        ProxyTlsConfig &operator=(ProxyTlsConfig &&other);

        [[nodiscard]] bool verifyPeer() const;
        void setVerifyPeer(bool value);

        [[nodiscard]] bool verifyHost() const;
        void setVerifyHost(bool value);

        [[nodiscard]] QString caCertPath() const;
        void setCaCertPath(const QString &path);

        [[nodiscard]] std::optional<QCNetworkTlsVersion> minTlsVersion() const;
        void setMinTlsVersion(const std::optional<QCNetworkTlsVersion> &version);

        [[nodiscard]] QString cipherList() const;
        void setCipherList(const QString &cipherList);

        [[nodiscard]] QString tls13Ciphers() const;
        void setTls13Ciphers(const QString &cipherList);

        [[nodiscard]] QCUnsupportedSecurityOptionPolicy unsupportedSecurityPolicy() const;
        void setUnsupportedSecurityPolicy(QCUnsupportedSecurityOptionPolicy policy);

    private:
        QSharedDataPointer<QCNetworkProxyTlsConfigData> d;
    };

    QCNetworkProxyConfig();
    QCNetworkProxyConfig(const QCNetworkProxyConfig &other);
    QCNetworkProxyConfig(QCNetworkProxyConfig &&other);
    ~QCNetworkProxyConfig();
    QCNetworkProxyConfig &operator=(const QCNetworkProxyConfig &other);
    QCNetworkProxyConfig &operator=(QCNetworkProxyConfig &&other);

    [[nodiscard]] ProxyType type() const;
    void setType(ProxyType value);

    [[nodiscard]] QString hostName() const;
    void setHostName(const QString &hostName);

    [[nodiscard]] quint16 port() const;
    void setPort(quint16 port);

    [[nodiscard]] QString userName() const;
    void setUserName(const QString &userName);

    [[nodiscard]] QString password() const;
    void setPassword(const QString &password);

    [[nodiscard]] std::optional<ProxyTlsConfig> tlsConfig() const;

    /// 设置 HTTPS proxy 的 TLS 配置；清空请使用 clearTlsConfig()。
    void setTlsConfig(const ProxyTlsConfig &config);

    /// 清空 HTTPS proxy 的 TLS 配置，恢复为“不显式设置”。
    void clearTlsConfig();

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

private:
    QSharedDataPointer<QCNetworkProxyConfigData> d;
};

} // namespace QCurl

#endif // QCNETWORKPROXYCONFIG_H
