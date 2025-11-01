#ifndef QCGLOBAL_H
#define QCGLOBAL_H

#include "QCurlConfig.h"
#include <QtCore/qglobal.h>
#include <curl/curl.h>

// Qt 6 检查（在 QCurlConfig.h 中已包含）
// 这里主要提供全局的类型别名和工具函数

// 导出宏定义（v2.15.0）
#ifndef QCURL_EXPORT
#  if defined(QT_BUILD_QCURL_LIB)
#    define QCURL_EXPORT Q_DECL_EXPORT
#  else
#    define QCURL_EXPORT Q_DECL_IMPORT
#  endif
#endif

QT_BEGIN_NAMESPACE
namespace QCurl {

// 版本信息
constexpr int versionMajor() { return QCURL_VERSION_MAJOR; }
constexpr int versionMinor() { return QCURL_VERSION_MINOR; }
constexpr int versionPatch() { return QCURL_VERSION_PATCH; }
constexpr const char* versionString() { return QCURL_VERSION_STRING; }

// libcurl 版本信息
constexpr const char* libcurlVersion() { return QCURL_LIBCURL_VERSION; }

// 特性检查辅助函数
constexpr bool hasWebSocketSupport() noexcept {
    return QCURL_HAS_WEBSOCKET;
}

constexpr bool hasHttp2Support() noexcept {
    return QCURL_HAS_HTTP2;
}

} // namespace QCurl
QT_END_NAMESPACE

#endif // QCGLOBAL_H
