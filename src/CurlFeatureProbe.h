/**
 * @file
 * @brief 声明 libcurl 能力探测接口。
 */

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
    /// 单个 capability 探测项的可用性结果。
    struct Availability
    {
        bool supported = true;
        QString reason; ///< supported==false 时给出失败原因
    };

    /// 返回进程内共享的 capability 快照实例。
    static CurlFeatureProbe &instance();

    /// 返回编译期链接的 libcurl 版本号。
    [[nodiscard]] int compiledVersionNum() const noexcept;
    /// 返回运行期实际加载的 libcurl 版本号。
    [[nodiscard]] int runtimeVersionNum() const noexcept;
    /// 返回运行期 libcurl 版本字符串。
    [[nodiscard]] QString runtimeVersionString() const;
    /// 返回运行期 libcurl 的 feature bitmask。
    [[nodiscard]] long runtimeFeatures() const noexcept;

    /// 检查指定 easy option 在当前运行库中的可用性。
    [[nodiscard]] Availability easyOptionAvailability(CURLoption option) const;
    /// 检查指定 multi option 在当前运行库中的可用性。
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
