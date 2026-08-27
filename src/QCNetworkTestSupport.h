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
 *
 * @note QObject 借用合同：manager 必须非空并在 owner thread 调用；handler 可为空且为
 * non-owning 借用，由调用方保活。handler 被替换或销毁后，旧裸指针立即失效。
 */
QCURL_TEST_SUPPORT_EXPORT void setMockHandler(QCNetworkAccessManager *manager,
                                              QCNetworkMockHandler *handler);

/// 返回通过 Test Support 绑定到 manager 的 mock handler；未绑定时返回 nullptr。
[[nodiscard]] QCURL_TEST_SUPPORT_EXPORT QCNetworkMockHandler *mockHandler(
    const QCNetworkAccessManager *manager);

} // namespace TestSupport

} // namespace QCurl

#endif // QCNETWORKTESTSUPPORT_H
