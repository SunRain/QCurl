/**
 * @file
 * @brief 应用超时、Cookie 文件和 HSTS/Alt-Svc 持久化选项。
 */

#include "QCNetworkAccessManager.h"
#include "QCNetworkTimeoutConfig.h"
#include "private/QCReplyCurlConfiguration_p.h"

namespace QCurl::Internal {

ReplyCurlConfigurationResult QCReplyCurlConfiguration::applyTimeouts(CURL *easy) const
{
    ReplyCurlConfigurationResult result;
    const auto timeout = m_plan.normalized.request.timeoutConfig();
    if (const auto value = timeout.connectTimeout(); value && value->count() > 0) {
        if (!required(CurlOptions::setConnectTimeout(easy, *value),
                      QCURL_CURL_OPTION(CURLOPT_CONNECTTIMEOUT_MS).name,
                      result)) {
            return result;
        }
    }
    if (const auto value = timeout.totalTimeout(); value && value->count() > 0) {
        if (!requiredOption(easy,
                            QCURL_CURL_OPTION(CURLOPT_TIMEOUT_MS),
                            static_cast<long>(value->count()),
                            result)) {
            return result;
        }
    }
    if (const auto value = timeout.lowSpeedTime(); value && value->count() > 0) {
        if (!requiredOption(easy,
                            QCURL_CURL_OPTION(CURLOPT_LOW_SPEED_TIME),
                            static_cast<long>(value->count()),
                            result)) {
            return result;
        }
    }
    if (const auto value = timeout.lowSpeedLimit(); value && *value > 0) {
        Q_UNUSED(requiredOption(easy,
                                QCURL_CURL_OPTION(CURLOPT_LOW_SPEED_LIMIT),
                                static_cast<long>(*value),
                                result));
    }
    return result;
}

void QCReplyCurlConfiguration::inheritCookieFile(int mode, const QString &path)
{
    if (m_cookieMode == 0 && m_cookiePathBytes.isEmpty() && mode != 0 && !path.isEmpty()) {
        m_cookieMode      = mode;
        m_cookiePathBytes = path.toUtf8();
    }
}

ReplyCurlConfigurationResult QCReplyCurlConfiguration::applyCookieFile(CURL *easy) const
{
    ReplyCurlConfigurationResult result;
    if (!easy || m_cookieMode == 0 || m_cookiePathBytes.isEmpty()) {
        return result;
    }
    const int read  = static_cast<int>(QCNetworkAccessManager::ReadOnly);
    const int write = static_cast<int>(QCNetworkAccessManager::WriteOnly);
    if ((m_cookieMode & read)
        && !requiredOption(easy,
                           QCURL_CURL_OPTION(CURLOPT_COOKIEFILE),
                           m_cookiePathBytes.constData(),
                           result)) {
        return result;
    }
    if (m_cookieMode & write) {
        Q_UNUSED(requiredOption(easy,
                                QCURL_CURL_OPTION(CURLOPT_COOKIEJAR),
                                m_cookiePathBytes.constData(),
                                result));
    }
    return result;
}

ReplyCurlConfigurationResult QCReplyCurlConfiguration::applyHttpStateFiles(CURL *easy,
                                                                           const QString &hstsPath,
                                                                           const QString &altSvcPath)
{
    ReplyCurlConfigurationResult result;
    if (!easy) {
        return result;
    }
    m_hstsCachePathBytes   = hstsPath.toUtf8();
    m_altSvcCachePathBytes = altSvcPath.toUtf8();
    if (!m_hstsCachePathBytes.isEmpty()
        && !requiredOption(easy,
                           QCURL_CURL_OPTION(CURLOPT_HSTS),
                           m_hstsCachePathBytes.constData(),
                           result)) {
        return result;
    }
    if (!m_altSvcCachePathBytes.isEmpty()) {
        Q_UNUSED(requiredOption(easy,
                                QCURL_CURL_OPTION(CURLOPT_ALTSVC),
                                m_altSvcCachePathBytes.constData(),
                                result));
    }
    return result;
}

ReplyCurlConfigurationResult QCReplyCurlConfiguration::flushCookieJar(CURL *easy) const
{
    ReplyCurlConfigurationResult result;
    if (!easy || !(m_cookieMode & static_cast<int>(QCNetworkAccessManager::WriteOnly))
        || m_cookiePathBytes.isEmpty()) {
        return result;
    }
    // FLUSH 沿用真实原生调用，不改变 share/Cookie store 的事务或故障注入合同。
    const CURLcode code = curl_easy_setopt(easy, CURLOPT_COOKIELIST, "FLUSH");
    if (code == CURLE_OK) {
        return result;
    }
    if (CurlOptions::isCapabilityRelatedError(code)) {
        warn(result,
             QStringLiteral("libcurl 不支持 Cookie flush（CURLOPT_COOKIELIST，%1），CookieJAR "
                            "可能不会立即落盘")
                 .arg(QString::fromUtf8(curl_easy_strerror(code))));
    } else {
        warn(result,
             QStringLiteral("Cookie flush 失败（%1）")
                 .arg(QString::fromUtf8(curl_easy_strerror(code))));
    }
    return result;
}

} // namespace QCurl::Internal
