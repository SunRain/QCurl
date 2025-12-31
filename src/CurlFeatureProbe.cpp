#include "CurlFeatureProbe.h"

#include <curl/curlver.h>

namespace QCurl {

CurlFeatureProbe &CurlFeatureProbe::instance()
{
    static CurlFeatureProbe probe;
    return probe;
}

CurlFeatureProbe::CurlFeatureProbe()
{
    refresh();
}

void CurlFeatureProbe::refresh()
{
    const curl_version_info_data *info = curl_version_info(CURLVERSION_NOW);
    if (!info) {
        m_runtimeVersionNum = 0;
        m_runtimeVersionString.clear();
        m_runtimeFeatures = 0;
        return;
    }

    m_runtimeVersionNum = static_cast<int>(info->version_num);
    m_runtimeVersionString = QString::fromUtf8(info->version ? info->version : "");
    m_runtimeFeatures = static_cast<long>(info->features);
}

int CurlFeatureProbe::compiledVersionNum() const noexcept
{
    return LIBCURL_VERSION_NUM;
}

int CurlFeatureProbe::runtimeVersionNum() const noexcept
{
    return m_runtimeVersionNum;
}

QString CurlFeatureProbe::runtimeVersionString() const
{
    return m_runtimeVersionString;
}

long CurlFeatureProbe::runtimeFeatures() const noexcept
{
    return m_runtimeFeatures;
}

CurlFeatureProbe::Availability CurlFeatureProbe::easyOptionAvailability(CURLoption option) const
{
    // 说明：仅对“需要显式 gate 的选项”提供结论；未纳入探测范围时默认按“可用”处理。
    switch (option) {
    case CURLOPT_MAXLIFETIME_CONN:
        if (m_runtimeVersionNum < 0x075000) {
            return {false, QStringLiteral("运行时 libcurl < 7.80.0，不支持 CURLOPT_MAXLIFETIME_CONN")};
        }
        return {true, QString()};
    default:
        return {true, QString()};
    }
}

CurlFeatureProbe::Availability CurlFeatureProbe::multiOptionAvailability(CURLMoption option) const
{
    // 说明：仅对“需要显式 gate 的选项”提供结论；未纳入探测范围时默认按“可用”处理。
    switch (option) {
    case CURLMOPT_MAX_TOTAL_CONNECTIONS:
        return {true, QString()};
    case CURLMOPT_MAX_HOST_CONNECTIONS:
        return {true, QString()};
    case CURLMOPT_MAX_CONCURRENT_STREAMS:
        return {true, QString()};
    case CURLMOPT_MAXCONNECTS:
        return {true, QString()};
    default:
        return {true, QString()};
    }
}

} // namespace QCurl
