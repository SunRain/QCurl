/**
 * @file
 * @brief TLS 模块内部的 required、security 与版本判定。
 */
#ifndef QCTLSCONFIGURATIONPOLICY_P_H
#define QCTLSCONFIGURATIONPOLICY_P_H

#include "QCTlsConfiguration_p.h"

namespace QCurl::Internal::TlsPolicy {

/// required 失败始终终止配置，诊断只包含选项名与原生静态错误。
[[nodiscard]] Q_DECL_HIDDEN bool required(TlsConfigurationResult &result,
                                          CURLcode code,
                                          CurlOptions::Option option);
/// 仅 capability 错误受 Fail/Warn 策略控制，其它错误始终失败。
[[nodiscard]] Q_DECL_HIDDEN bool security(TlsConfigurationResult &result,
                                          CURLcode code,
                                          CurlOptions::Option option,
                                          QCUnsupportedSecurityOptionPolicy policy);
/// 缺省不调用 native；无映射版本沿用当前 scope 的 Fail/Warn 诊断。
[[nodiscard]] Q_DECL_HIDDEN bool minimumVersion(CURL *easy,
                                                CurlOptions::Option option,
                                                std::optional<QCNetworkTlsVersion> version,
                                                QCUnsupportedSecurityOptionPolicy policy,
                                                const QString &scope,
                                                TlsConfigurationResult &result);

} // namespace QCurl::Internal::TlsPolicy
#endif // QCTLSCONFIGURATIONPOLICY_P_H
