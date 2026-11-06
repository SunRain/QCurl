/**
 * @file
 * @brief 封装 endpoint 与 HTTPS proxy 的 TLS 行为及借用存储。
 */
#ifndef QCTLSCONFIGURATION_P_H
#define QCTLSCONFIGURATION_P_H

#include "QCNetworkError.h"
#include "QCNetworkProxyConfig.h"
#include "QCNetworkSslConfig.h"
#include "private/QCCurlOptionAdapter_p.h"
#include "private/QCNetworkRequestPolicy_p.h"

#include <QStringList>

#include <optional>

namespace QCurl::Internal {

/// @brief 一次 TLS 配置的首次 fatal，保留原生证据和脱敏诊断。
struct TlsConfigurationFailure
{
    NetworkError error  = NetworkError::InvalidRequest;
    CURLcode nativeCode = CURLE_OK;
    QString optionName;
    QString message;
};

/// @brief 按执行顺序保留 warnings，并区分可继续与首次 fatal。
struct TlsConfigurationResult
{
    QStringList warnings;
    std::optional<TlsConfigurationFailure> failure;
};

/// @brief 代理调用方提供的窄输入，不携带 Reply 或整个请求状态。
struct ProxyTlsInput
{
    QCNetworkProxyConfig::ProxyType requestedType = QCNetworkProxyConfig::ProxyType::None;
    long effectiveCurlProxyType                   = CURLPROXY_HTTP;
    std::optional<QCNetworkProxyConfig::ProxyTlsConfig> config;
};

/// 原生 easy 的全部 TLS 字节存储；和原 transfer backing 一起保活至 easy cleanup 之后。
class Q_DECL_HIDDEN QCTlsConfiguration final
{
public:
    QCTlsConfiguration() = default;
    Q_DISABLE_COPY_MOVE(QCTlsConfiguration)

    /// 在原配置时点应用 endpoint TLS，required 失败不能降级为 warning。
    [[nodiscard]] TlsConfigurationResult applyEndpoint(CURL *easy,
                                                       const RequestEndpointTlsPolicy &common,
                                                       const QCNetworkSslConfig &config);
    /// 仅对实际 HTTPS proxy 应用可选 TLS 配置，策略与 endpoint 独立。
    [[nodiscard]] TlsConfigurationResult applyProxy(CURL *easy, const ProxyTlsInput &input);

private:
    [[nodiscard]] bool configureCa(CURL *easy,
                                   const RequestEndpointTlsPolicy &common,
                                   TlsConfigurationResult &result);
    [[nodiscard]] bool configureCredentials(CURL *easy,
                                            const QCNetworkSslConfig &config,
                                            TlsConfigurationResult &result);
    [[nodiscard]] static bool requiredString(CURL *easy,
                                             CurlOptions::Option option,
                                             const QString &value,
                                             QByteArray &storage,
                                             TlsConfigurationResult &result);
    [[nodiscard]] static bool securityString(CURL *easy,
                                             CurlOptions::Option option,
                                             const QString &value,
                                             QByteArray &storage,
                                             QCUnsupportedSecurityOptionPolicy policy,
                                             TlsConfigurationResult &result);
    QByteArray m_sslCaCertPathBytes;
    QByteArray m_sslClientCertPathBytes;
    QByteArray m_sslClientKeyPathBytes;
    QByteArray m_sslClientKeyPasswordBytes;
    QByteArray m_sslPinnedPublicKeyBytes;
    QByteArray m_sslCipherListBytes;
    QByteArray m_sslTls13CiphersBytes;
    QByteArray m_proxySslCaCertPathBytes;
    QByteArray m_proxySslCipherListBytes;
    QByteArray m_proxySslTls13CiphersBytes;
};

} // namespace QCurl::Internal
#endif // QCTLSCONFIGURATION_P_H
