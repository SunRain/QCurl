/**
 * @file
 * @brief 声明 QCurl 导出宏与全局开关。
 */

#ifndef QCGLOBAL_H
#define QCGLOBAL_H

#include "QCurlConfig.h"

#include <QtGlobal>

// Qt 6 检查（在 QCurlConfig.h 中已包含）
// 这里主要提供全局的类型别名和工具函数

// 导出宏定义（Core / Stable）
#ifndef QCURL_EXPORT
#if defined(QCURL_STATIC_DEFINE)
#define QCURL_EXPORT
#elif defined(QCURL_BUILDING_LIBRARY)
#define QCURL_EXPORT Q_DECL_EXPORT
#else
#define QCURL_EXPORT Q_DECL_IMPORT
#endif
#endif

#ifndef QCURL_OTHER_EXTRAS_EXPORT
#if defined(QCURL_OTHER_EXTRAS_STATIC_DEFINE)
#define QCURL_OTHER_EXTRAS_EXPORT
#elif defined(QCURL_BUILDING_OTHER_EXTRAS_LIBRARY)
#define QCURL_OTHER_EXTRAS_EXPORT Q_DECL_EXPORT
#else
#define QCURL_OTHER_EXTRAS_EXPORT Q_DECL_IMPORT
#endif
#endif

namespace QCurl {

// 版本信息
constexpr int versionMajor()
{
    return QCURL_VERSION_MAJOR;
}
constexpr int versionMinor()
{
    return QCURL_VERSION_MINOR;
}
constexpr int versionPatch()
{
    return QCURL_VERSION_PATCH;
}
constexpr const char *versionString()
{
    return QCURL_VERSION_STRING;
}

// libcurl 版本信息
constexpr const char *libcurlVersion()
{
    return QCURL_LIBCURL_VERSION;
}

/**
 * @brief 初始化 QCurl 的进程级公共运行时注册项。
 *
 * 该函数会注册 QCurl 公共 Qt 元类型，供 queued connection、QSignalSpy、QVariant
 * 和 QMetaType::fromName() 使用。shared library 通常会在库加载时自动完成注册；
 * static library consumer 若只使用头文件类型，应在 main() 早期显式调用一次。
 */
QCURL_EXPORT void initialize();

/**
 * @brief 安全相关能力不可用时的处理策略
 *
 * 说明：
 * - Fail：默认更安全的策略。若用户显式启用某安全能力但运行时不可用，则请求失败并返回可诊断错误。
 * - Warn：兼容性策略。若运行时不可用，则忽略该能力并输出 warning（不得包含敏感信息）。
 */
enum class QCUnsupportedSecurityOptionPolicy { Fail, Warn };

// 特性检查辅助函数
constexpr bool hasWebSocketSupport() noexcept
{
    return QCURL_HAS_WEBSOCKET;
}

constexpr bool hasHttp2Support() noexcept
{
    return QCURL_HAS_HTTP2;
}

} // namespace QCurl

#endif // QCGLOBAL_H
