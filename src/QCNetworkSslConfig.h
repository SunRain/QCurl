#ifndef QCNETWORKSSLCONFIG_H
#define QCNETWORKSSLCONFIG_H

#include <QString>

QT_BEGIN_NAMESPACE

namespace QCurl {

/**
 * @brief SSL/TLS 配置类
 *
 * 用于配置 HTTPS 请求的 SSL/TLS 选项，包括证书验证、客户端证书等。
 * 默认启用安全配置（验证对等证书和主机名）。
 *
 * @par 安全提示
 * - 生产环境应始终启用 verifyPeer 和 verifyHost
 * - 仅在测试环境中使用 insecureConfig()
 * - 客户端证书用于双向 TLS 认证
 *
 * @par 示例：默认安全配置
 * @code
 * QCNetworkRequest request(url);
 * request.setSslConfig(QCNetworkSslConfig::defaultConfig());
 * @endcode
 *
 * @par 示例：自定义 CA 证书
 * @code
 * QCNetworkSslConfig sslConfig;
 * sslConfig.verifyPeer = true;
 * sslConfig.verifyHost = true;
 * sslConfig.caCertPath = "/path/to/custom-ca.pem";
 * request.setSslConfig(sslConfig);
 * @endcode
 *
 */
class QCNetworkSslConfig
{
public:
    /**
     * @brief 是否验证对等证书
     *
     * 对应 libcurl 的 CURLOPT_SSL_VERIFYPEER 选项。
     * 默认值：true（安全）
     */
    bool verifyPeer = true;

    /**
     * @brief 是否验证主机名
     *
     * 对应 libcurl 的 CURLOPT_SSL_VERIFYHOST 选项。
     * 默认值：true（安全）
     */
    bool verifyHost = true;

    /**
     * @brief CA 证书文件路径
     *
     * 用于验证服务器证书的 CA 证书包（PEM 格式）。
     * 如果为空，libcurl 使用系统默认 CA 证书。
     */
    QString caCertPath;

    /**
     * @brief 客户端证书文件路径
     *
     * 用于双向 TLS 认证的客户端证书（PEM 格式）。
     */
    QString clientCertPath;

    /**
     * @brief 客户端私钥文件路径
     *
     * 客户端证书对应的私钥文件（PEM 格式）。
     */
    QString clientKeyPath;

    /**
     * @brief 客户端私钥密码
     *
     * 如果私钥文件有密码保护，在此设置。
     */
    QString clientKeyPassword;

    /**
     * @brief 返回默认的安全配置
     *
     * - verifyPeer = true
     * - verifyHost = true
     * - 使用系统默认 CA 证书
     *
     * @return QCNetworkSslConfig 默认配置实例
     */
    [[nodiscard]] static QCNetworkSslConfig defaultConfig();

    /**
     * @brief 返回不安全的配置（禁用所有验证）
     *
     * ⚠️ 警告：仅用于测试！不要在生产环境使用！
     *
     * - verifyPeer = false
     * - verifyHost = false
     *
     * @return QCNetworkSslConfig 不安全配置实例
     */
    [[nodiscard]] static QCNetworkSslConfig insecureConfig();
};

} // namespace QCurl
QT_END_NAMESPACE

#endif // QCNETWORKSSLCONFIG_H
