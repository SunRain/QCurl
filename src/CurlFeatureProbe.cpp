#include "CurlFeatureProbe.h"

#include <curl/curlver.h>

namespace QCurl {

namespace {

constexpr int kMinimumRuntimeCurlVersionNum = 0x075500;
constexpr auto kMinimumRuntimeCurlVersionString = "7.85.0";

QString versionNumToString(int versionNum)
{
    if (versionNum <= 0) {
        return QStringLiteral("unknown");
    }

    return QStringLiteral("%1.%2.%3")
        .arg((versionNum >> 16) & 0xff)
        .arg((versionNum >> 8) & 0xff)
        .arg(versionNum & 0xff);
}

#ifdef QCURL_ENABLE_TEST_HOOKS
int forcedRuntimeVersionNum(int detectedVersionNum)
{
    const QByteArray raw = qgetenv("QCURL_TEST_FORCE_RUNTIME_LIBCURL_VERSION_NUM");
    if (raw.trimmed().isEmpty()) {
        return detectedVersionNum;
    }

    bool ok = false;
    const int forcedVersionNum = QString::fromLatin1(raw.trimmed()).toInt(&ok, 0);
    return ok ? forcedVersionNum : detectedVersionNum;
}
#endif

} // namespace

static_assert(LIBCURL_VERSION_NUM >= kMinimumRuntimeCurlVersionNum,
              "QCurl requires libcurl >= 7.85.0");

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

    m_runtimeVersionNum    = static_cast<int>(info->version_num);
    m_runtimeVersionString = QString::fromUtf8(info->version ? info->version : "");
    m_runtimeFeatures      = static_cast<long>(info->features);

#ifdef QCURL_ENABLE_TEST_HOOKS
    m_runtimeVersionNum = forcedRuntimeVersionNum(m_runtimeVersionNum);
    if (m_runtimeVersionString.isEmpty()
        || m_runtimeVersionNum != static_cast<int>(info->version_num)) {
        m_runtimeVersionString = versionNumToString(m_runtimeVersionNum);
    }
#endif
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

CurlFeatureProbe::Availability CurlFeatureProbe::minimumRuntimeAvailability() const
{
    if (m_runtimeVersionNum >= kMinimumRuntimeCurlVersionNum) {
        return {true, QString()};
    }

    const QString runtimeVersion = m_runtimeVersionString.isEmpty()
                                       ? versionNumToString(m_runtimeVersionNum)
                                       : m_runtimeVersionString;
    return {false,
            QStringLiteral("QCurl requires runtime libcurl >= %1, but loaded %2 "
                           "(compiled against %3). 请升级运行时 libcurl 后重试。")
                .arg(QString::fromLatin1(kMinimumRuntimeCurlVersionString),
                     runtimeVersion,
                     QString::fromLatin1(LIBCURL_VERSION))};
}

CurlFeatureProbe::Availability CurlFeatureProbe::easyOptionAvailability(CURLoption option) const
{
    // 说明：仅对“需要显式 gate 的选项”提供结论；未纳入探测范围时默认按“可用”处理。
    switch (option) {
        case CURLOPT_MAXLIFETIME_CONN:
            if (m_runtimeVersionNum < 0x075000) {
                return {false,
                        QStringLiteral("运行时 libcurl < 7.80.0，不支持 CURLOPT_MAXLIFETIME_CONN")};
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

#ifdef QCURL_ENABLE_TEST_HOOKS
void CurlFeatureProbe::refreshForTesting()
{
    refresh();
}
#endif

} // namespace QCurl
