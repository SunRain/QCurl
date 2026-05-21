/**
 * @file
 * @brief 声明请求级认证、重定向与传输配置族。
 */

#ifndef QCNETWORKREQUESTCONFIG_H
#define QCNETWORKREQUESTCONFIG_H

#include "QCGlobal.h"

#include <QSharedDataPointer>
#include <QString>
#include <QStringList>

#include <chrono>
#include <optional>

namespace QCurl {

class QCNetworkHttpAuthConfigData;
class QCNetworkRedirectConfigData;
class QCNetworkTransferConfigData;

/** POST 请求在 301/302/303 重定向下的处理策略。 */
enum class QCNetworkPostRedirectPolicy {
    Default,
    KeepPost301,
    KeepPost302,
    KeepPost303,
    KeepPostAll,
};

/** HTTP 认证方式。 */
enum class QCNetworkHttpAuthMethod {
    Basic,
    Any,
    AnySafe,
};

/** IP 族选择策略。 */
enum class QCNetworkIpResolve {
    Any,
    Ipv4,
    Ipv6,
};

/** 请求级 HTTP 认证配置。 */
class QCURL_EXPORT QCNetworkHttpAuthConfig
{
public:
    QCNetworkHttpAuthConfig();
    QCNetworkHttpAuthConfig(const QCNetworkHttpAuthConfig &other);
    QCNetworkHttpAuthConfig(QCNetworkHttpAuthConfig &&other);
    ~QCNetworkHttpAuthConfig();
    QCNetworkHttpAuthConfig &operator=(const QCNetworkHttpAuthConfig &other);
    QCNetworkHttpAuthConfig &operator=(QCNetworkHttpAuthConfig &&other);

    /// 返回 HTTP 认证用户名；默认空字符串。
    [[nodiscard]] QString userName() const;
    /// 设置 HTTP 认证用户名；空字符串表示不提供用户名。
    void setUserName(const QString &userName);

    /// 返回 HTTP 认证密码；默认空字符串。
    [[nodiscard]] QString password() const;
    /// 设置 HTTP 认证密码；调用者负责避免泄漏敏感值。
    void setPassword(const QString &password);

    /// 返回认证方式；默认 `Basic`。
    [[nodiscard]] QCNetworkHttpAuthMethod method() const;
    /// 设置认证方式；`Any` 可能允许 libcurl 选择不安全方式。
    void setMethod(QCNetworkHttpAuthMethod method);

    /// 返回是否允许认证信息在重定向中不受限制地继续发送。
    [[nodiscard]] bool allowUnrestrictedAuth() const;
    /// 设置认证重定向策略；开启后可能把凭据发送给重定向目标。
    void setAllowUnrestrictedAuth(bool enabled);

    /// 返回是否在 HTTP 明文 Basic Auth 场景发出告警；默认开启。
    [[nodiscard]] bool warnIfBasicOverHttp() const;
    /// 设置明文 Basic Auth 告警策略；关闭后不改变传输安全性。
    void setWarnIfBasicOverHttp(bool enabled);

private:
    QSharedDataPointer<QCNetworkHttpAuthConfigData> d;
};

/** 请求级重定向配置。 */
class QCURL_EXPORT QCNetworkRedirectConfig
{
public:
    QCNetworkRedirectConfig();
    QCNetworkRedirectConfig(const QCNetworkRedirectConfig &other);
    QCNetworkRedirectConfig(QCNetworkRedirectConfig &&other);
    ~QCNetworkRedirectConfig();
    QCNetworkRedirectConfig &operator=(const QCNetworkRedirectConfig &other);
    QCNetworkRedirectConfig &operator=(QCNetworkRedirectConfig &&other);

    /// 返回是否跟随重定向；默认开启。
    [[nodiscard]] bool followLocation() const;
    /// 设置是否跟随重定向。
    void setFollowLocation(bool enabled);

    /// 返回最大重定向次数；`std::nullopt` 表示使用底层默认。
    [[nodiscard]] std::optional<int> maxRedirects() const;
    /// 设置最大重定向次数；负值会被忽略并清空显式上限。
    void setMaxRedirects(std::optional<int> maxRedirects);

    /// 返回 POST 在 301/302/303 下是否保持 POST 的策略。
    [[nodiscard]] QCNetworkPostRedirectPolicy postRedirectPolicy() const;
    /// 设置 POST 重定向策略。
    void setPostRedirectPolicy(QCNetworkPostRedirectPolicy policy);

    /// 返回是否自动维护 Referer；默认关闭。
    [[nodiscard]] bool autoRefererEnabled() const;
    /// 设置自动 Referer 策略。
    void setAutoRefererEnabled(bool enabled);

    /// 返回显式 Referer；默认空字符串。
    [[nodiscard]] QString referer() const;
    /// 设置显式 Referer；空字符串表示不设置该值。
    void setReferer(const QString &referer);

    /// 返回是否允许敏感 header 在重定向中不受限制地继续发送。
    [[nodiscard]] bool allowUnrestrictedSensitiveHeadersOnRedirect() const;
    /// 设置敏感 header 重定向策略；开启后可能把凭据发送给重定向目标。
    void setAllowUnrestrictedSensitiveHeadersOnRedirect(bool enabled);

private:
    QSharedDataPointer<QCNetworkRedirectConfigData> d;
};

/** 请求级传输配置。 */
class QCURL_EXPORT QCNetworkTransferConfig
{
public:
    QCNetworkTransferConfig();
    QCNetworkTransferConfig(const QCNetworkTransferConfig &other);
    QCNetworkTransferConfig(QCNetworkTransferConfig &&other);
    ~QCNetworkTransferConfig();
    QCNetworkTransferConfig &operator=(const QCNetworkTransferConfig &other);
    QCNetworkTransferConfig &operator=(QCNetworkTransferConfig &&other);

    /// 返回是否启用自动解压；默认关闭。
    [[nodiscard]] bool autoDecompressionEnabled() const;
    /// 设置自动解压；关闭时会清空 accepted encodings。
    void setAutoDecompressionEnabled(bool enabled);

    /// 返回接受的压缩编码；空列表表示不显式请求压缩。
    [[nodiscard]] QStringList acceptedEncodings() const;
    /// 设置接受的压缩编码；空白项会被忽略，空列表会关闭自动解压。
    void setAcceptedEncodings(const QStringList &encodings);

    /// 返回下载限速；`std::nullopt` 表示不限速。
    [[nodiscard]] std::optional<qint64> maxDownloadBytesPerSec() const;
    /// 设置下载限速；`0` 清空限速，负值会被忽略并清空限速。
    void setMaxDownloadBytesPerSec(qint64 bytesPerSec);

    /// 返回上传限速；`std::nullopt` 表示不限速。
    [[nodiscard]] std::optional<qint64> maxUploadBytesPerSec() const;
    /// 设置上传限速；`0` 清空限速，负值会被忽略并清空限速。
    void setMaxUploadBytesPerSec(qint64 bytesPerSec);

    /// 返回 backpressure 高水位；`0` 表示关闭内部接收流控。
    [[nodiscard]] qint64 backpressureLimitBytes() const noexcept;
    /// 设置 backpressure 高水位；负值按 `0` 处理。
    void setBackpressureLimitBytes(qint64 bytes);

    /// 返回 backpressure 恢复低水位；`0` 表示使用默认低水位或未启用。
    [[nodiscard]] qint64 backpressureResumeBytes() const noexcept;
    /// 设置 backpressure 恢复低水位；必须小于高水位，否则回到默认。
    void setBackpressureResumeBytes(qint64 bytes);

    /// 返回 Expect: 100-continue 超时；`std::nullopt` 表示使用底层默认。
    [[nodiscard]] std::optional<std::chrono::milliseconds> expect100ContinueTimeout() const;
    /// 设置 Expect: 100-continue 超时；负值会被忽略并清空显式配置。
    void setExpect100ContinueTimeout(std::chrono::milliseconds timeout);

    /// 返回 IP 族选择；`std::nullopt` 表示不强制 IPv4/IPv6。
    [[nodiscard]] std::optional<QCNetworkIpResolve> ipResolve() const;
    /// 设置 IP 族选择；`Any` 会清空显式配置。
    void setIpResolve(QCNetworkIpResolve resolve);

    /// 返回允许的 URL 协议；`std::nullopt` 表示使用默认策略。
    [[nodiscard]] std::optional<QStringList> allowedProtocols() const;
    /// 设置允许的 URL 协议；空白项会被忽略，空列表清空显式配置。
    void setAllowedProtocols(const QStringList &protocols);

    /// 返回允许的重定向目标协议；`std::nullopt` 表示使用默认策略。
    [[nodiscard]] std::optional<QStringList> allowedRedirectProtocols() const;
    /// 设置允许的重定向目标协议；空白项会被忽略，空列表清空显式配置。
    void setAllowedRedirectProtocols(const QStringList &protocols);

    /// 返回当前运行时不支持显式安全选项时的处理策略。
    [[nodiscard]] QCUnsupportedSecurityOptionPolicy unsupportedSecurityOptionPolicy() const;
    /// 设置不支持显式安全选项时的处理策略。
    void setUnsupportedSecurityOptionPolicy(QCUnsupportedSecurityOptionPolicy policy);

private:
    QSharedDataPointer<QCNetworkTransferConfigData> d;
};

} // namespace QCurl

#endif // QCNETWORKREQUESTCONFIG_H
