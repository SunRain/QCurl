/**
 * @file
 * @brief 生成 TLS 选项的有序诊断，不发布 Reply 状态或日志。
 */
#include "QCTlsConfigurationPolicy_p.h"

namespace QCurl::Internal::TlsPolicy {

namespace {

std::optional<long> curlMinimumVersion(QCNetworkTlsVersion version)
{
    switch (version) {
        case QCNetworkTlsVersion::Default:
            return std::nullopt;
        case QCNetworkTlsVersion::Tls1_0:
            return static_cast<long>(CURL_SSLVERSION_TLSv1);
        case QCNetworkTlsVersion::Tls1_1:
            return static_cast<long>(CURL_SSLVERSION_TLSv1_1);
        case QCNetworkTlsVersion::Tls1_2:
            return static_cast<long>(CURL_SSLVERSION_TLSv1_2);
        case QCNetworkTlsVersion::Tls1_3:
            return static_cast<long>(CURL_SSLVERSION_TLSv1_3);
    }
    return std::nullopt;
}

} // namespace

bool required(TlsConfigurationResult &result, CURLcode code, CurlOptions::Option option)
{
    if (code == CURLE_OK) {
        return true;
    }
    const QString name = QString::fromUtf8(option.name);
    result.failure = TlsConfigurationFailure{NetworkError::InvalidRequest,
                                             code,
                                             name,
                                             QStringLiteral("设置 %1 失败（%2）")
                                                 .arg(name,
                                                      QString::fromUtf8(curl_easy_strerror(code)))};
    return false;
}

bool security(TlsConfigurationResult &result,
              CURLcode code,
              CurlOptions::Option option,
              QCUnsupportedSecurityOptionPolicy policy)
{
    if (code == CURLE_OK) {
        return true;
    }
    if (!CurlOptions::isCapabilityRelatedError(code)) {
        return required(result, code, option);
    }
    const QString name    = QString::fromUtf8(option.name);
    const QString message = QStringLiteral("libcurl 不支持 %1（%2）")
                                .arg(name, QString::fromUtf8(curl_easy_strerror(code)));
    if (policy == QCUnsupportedSecurityOptionPolicy::Fail) {
        result.failure = TlsConfigurationFailure{NetworkError::InvalidRequest, code, name, message};
        return false;
    }
    result.warnings.append(message);
    return true;
}

bool minimumVersion(CURL *easy,
                    CurlOptions::Option option,
                    std::optional<QCNetworkTlsVersion> version,
                    QCUnsupportedSecurityOptionPolicy policy,
                    const QString &scope,
                    TlsConfigurationResult &result)
{
    if (!version.has_value()) {
        return true;
    }
    if (const auto mapped = curlMinimumVersion(version.value()); mapped.has_value()) {
        return security(result,
                        CurlOptions::setWithTestHook(easy, option, mapped.value()),
                        option,
                        policy);
    }
    const QString message = QStringLiteral("不支持的 TLS 版本配置（%1）").arg(scope);
    if (policy == QCUnsupportedSecurityOptionPolicy::Fail) {
        result.failure = TlsConfigurationFailure{NetworkError::InvalidRequest,
                                                 CURLE_OK,
                                                 QString::fromUtf8(option.name),
                                                 message};
        return false;
    }
    result.warnings.append(message);
    return true;
}

} // namespace QCurl::Internal::TlsPolicy
