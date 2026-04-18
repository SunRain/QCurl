/**
 * @file
 * @brief 声明 SSL/TLS 配置。
 */

#ifndef QCNETWORKSSLCONFIG_H
#define QCNETWORKSSLCONFIG_H

#include "QCGlobal.h"

#include <QSharedDataPointer>
#include <QString>

#include <optional>

namespace QCurl {

class QCNetworkSslConfigData;

/**
 * @brief TLS 最低版本策略（M5，可选）
 *
 * 映射到 libcurl 的 CURLOPT_SSLVERSION（最低版本）。
 */
enum class QCNetworkTlsVersion {
    Default, ///< 不设置（保持 libcurl 默认）
    Tls1_0,
    Tls1_1,
    Tls1_2,
    Tls1_3
};

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
 * sslConfig.setVerifyPeer(true);
 * sslConfig.setVerifyHost(true);
 * sslConfig.setCaCertPath("/path/to/custom-ca.pem");
 * request.setSslConfig(sslConfig);
 * @endcode
 *
 */
class QCURL_EXPORT QCNetworkSslConfig
{
public:
    QCNetworkSslConfig();
    QCNetworkSslConfig(const QCNetworkSslConfig &other);
    QCNetworkSslConfig(QCNetworkSslConfig &&other);
    ~QCNetworkSslConfig();
    QCNetworkSslConfig &operator=(const QCNetworkSslConfig &other);
    QCNetworkSslConfig &operator=(QCNetworkSslConfig &&other);

    /**
     * @brief 是否验证对等证书
     *
     * 对应 libcurl 的 CURLOPT_SSL_VERIFYPEER 选项。
     * 默认值：true（安全）
     */
    [[nodiscard]] bool verifyPeer() const;
    void setVerifyPeer(bool value);

    /**
     * @brief 是否验证主机名
     *
     * 对应 libcurl 的 CURLOPT_SSL_VERIFYHOST 选项。
     * 默认值：true（安全）
     */
    [[nodiscard]] bool verifyHost() const;
    void setVerifyHost(bool value);

    /**
     * @brief CA 证书文件路径
     *
     * 用于验证服务器证书的 CA 证书包（PEM 格式）。
     * 如果为空，libcurl 使用系统默认 CA 证书。
     */
    [[nodiscard]] QString caCertPath() const;
    void setCaCertPath(const QString &path);

    /**
     * @brief 客户端证书文件路径
     *
     * 用于双向 TLS 认证的客户端证书（PEM 格式）。
     */
    [[nodiscard]] QString clientCertPath() const;
    void setClientCertPath(const QString &path);

    /**
     * @brief 客户端私钥文件路径
     *
     * 客户端证书对应的私钥文件（PEM 格式）。
     */
    [[nodiscard]] QString clientKeyPath() const;
    void setClientKeyPath(const QString &path);

    /**
     * @brief 客户端私钥密码
     *
     * 如果私钥文件有密码保护，在此设置。
     */
    [[nodiscard]] QString clientKeyPassword() const;
    void setClientKeyPassword(const QString &password);

    /**
     * @brief 公钥 pin（可选）
     *
     * 映射到 CURLOPT_PINNEDPUBLICKEY。
     * 允许设置文件路径或 libcurl 支持的 "sha256//..." 形式。
     *
     * 默认空（不设置），避免 silent behavior change。
     */
    [[nodiscard]] QString pinnedPublicKey() const;
    void setPinnedPublicKey(const QString &pinnedPublicKey);

    /**
     * @brief TLS 最低版本（可选）
     *
     * 默认 std::nullopt（不设置）。
     */
    [[nodiscard]] std::optional<QCNetworkTlsVersion> minTlsVersion() const;
    void setMinTlsVersion(const std::optional<QCNetworkTlsVersion> &version);

    /**
     * @brief TLS cipher 列表（可选，OpenSSL 风格）
     *
     * 映射到 CURLOPT_SSL_CIPHER_LIST。
     */
    [[nodiscard]] QString cipherList() const;
    void setCipherList(const QString &cipherList);

    /**
     * @brief TLS 1.3 cipher 列表（可选）
     *
     * 映射到 CURLOPT_TLS13_CIPHERS。
     */
    [[nodiscard]] QString tls13Ciphers() const;
    void setTls13Ciphers(const QString &cipherList);

    /**
     * @brief 安全相关能力不可用时的处理策略
     *
     * 默认 Fail（更安全）。
     */
    [[nodiscard]] QCUnsupportedSecurityOptionPolicy unsupportedSecurityPolicy() const;
    void setUnsupportedSecurityPolicy(QCUnsupportedSecurityOptionPolicy policy);

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

private:
    QSharedDataPointer<QCNetworkSslConfigData> d;
};

} // namespace QCurl

#endif // QCNETWORKSSLCONFIG_H
