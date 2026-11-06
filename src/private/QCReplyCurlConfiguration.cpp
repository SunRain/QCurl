/**
 * @file
 * @brief 保存只读请求计划并统一非 TLS 选项诊断。
 */

#include "private/QCReplyCurlConfiguration_p.h"

#include <utility>

namespace QCurl::Internal {

QCReplyCurlConfiguration::QCReplyCurlConfiguration(CurlPlan plan)
    : m_plan(std::move(plan))
    , m_policy(compileRequestPolicy(m_plan.normalized.request))
{}

ReplyCurlConfigurationResult QCReplyCurlConfiguration::validationResult() const
{
    ReplyCurlConfigurationResult result;
    if (!m_policy.policy) {
        result.failure = m_policy.failure;
    }
    return result;
}

bool QCReplyCurlConfiguration::required(CURLcode code,
                                        const char *optionName,
                                        ReplyCurlConfigurationResult &result)
{
    if (code == CURLE_OK) {
        return true;
    }
    result.failure = QStringLiteral("设置 %1 失败（%2）")
                         .arg(QString::fromUtf8(optionName))
                         .arg(QString::fromUtf8(curl_easy_strerror(code)));
    return false;
}

bool QCReplyCurlConfiguration::optional(CURLcode code,
                                        const char *optionName,
                                        ReplyCurlConfigurationResult &result)
{
    if (code == CURLE_OK) {
        return true;
    }
    const QString message = CurlOptions::isCapabilityRelatedError(code)
                                ? QStringLiteral("libcurl 不支持 %1（%2）")
                                : QStringLiteral("设置 %1 失败（%2）");
    warn(result,
         message.arg(QString::fromUtf8(optionName), QString::fromUtf8(curl_easy_strerror(code))));
    return false;
}

void QCReplyCurlConfiguration::warn(ReplyCurlConfigurationResult &result, const QString &message)
{
    result.warnings.append({ReplyCurlWarningKind::Capability, message});
}

ReplyCurlConfigurationResult QCReplyCurlConfiguration::classifyRequiredOption(CURLcode code,
                                                                              const char *optionName)
{
    ReplyCurlConfigurationResult result;
    Q_UNUSED(required(code, optionName, result));
    return result;
}

} // namespace QCurl::Internal
