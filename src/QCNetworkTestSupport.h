/**
 * @file
 * @brief 声明显式 Test Support 的 manager 绑定入口。
 */

#ifndef QCNETWORKTESTSUPPORT_H
#define QCNETWORKTESTSUPPORT_H

#include "QCGlobal.h"

namespace QCurl {

class QCNetworkAccessManager;
class QCNetworkMockHandler;

namespace TestSupport {

/**
 * @brief 为 manager 绑定测试专用 mock handler；manager 不持有 handler。
 *
 * 该入口只属于显式 Test Support 安装面，默认生产 Core 不安装本头文件。
 */
QCURL_EXPORT void setMockHandler(QCNetworkAccessManager *manager, QCNetworkMockHandler *handler);

/// 返回通过 Test Support 绑定到 manager 的 mock handler；未绑定时返回 nullptr。
[[nodiscard]] QCURL_EXPORT QCNetworkMockHandler *mockHandler(const QCNetworkAccessManager *manager);

} // namespace TestSupport

} // namespace QCurl

#endif // QCNETWORKTESTSUPPORT_H
