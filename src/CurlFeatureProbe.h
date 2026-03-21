#ifndef CURLFEATUREPROBE_H
#define CURLFEATUREPROBE_H

#include <QString>

#include <curl/curl.h>

namespace QCurl {

/**
 * @brief internal libcurl 能力快照
 *
 * 缓存运行库版本与 feature bitmask，并为库内 capability gating 提供统一判定入口。
 */
class CurlFeatureProbe
{
public:
    struct Availability
    {
        bool supported = true;
        QString reason; // supported==false 时给出原因
    };

    static CurlFeatureProbe &instance();

    [[nodiscard]] int compiledVersionNum() const noexcept;
    [[nodiscard]] int runtimeVersionNum() const noexcept;
    [[nodiscard]] QString runtimeVersionString() const;
    [[nodiscard]] long runtimeFeatures() const noexcept;

    [[nodiscard]] Availability easyOptionAvailability(CURLoption option) const;
    [[nodiscard]] Availability multiOptionAvailability(CURLMoption option) const;

private:
    CurlFeatureProbe();
    void refresh();

    int m_runtimeVersionNum = 0;
    QString m_runtimeVersionString;
    long m_runtimeFeatures = 0;
};

} // namespace QCurl

#endif // CURLFEATUREPROBE_H
