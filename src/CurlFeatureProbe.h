#ifndef CURLFEATUREPROBE_H
#define CURLFEATUREPROBE_H

#include <QString>

#include <curl/curl.h>

namespace QCurl {

/**
 * @brief libcurl 能力探测器（运行时缓存）
 *
 * 用于在运行时缓存 libcurl 版本与 feature bitmask，并为“可选能力/门禁”提供统一判定入口。
 *
 * 说明：
 * - QCurl 在编译期要求 libcurl >= 8.0，但仍可能遇到“编译头与运行库不一致”或“编译特性缺失”的场景。
 * - 该类只负责给出“可用/不可用 + 原因”的判断，不负责日志脱敏与业务策略（由上层负责）。
 */
class CurlFeatureProbe
{
public:
    struct Availability {
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

