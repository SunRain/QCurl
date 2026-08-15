/**
 * @file
 * @brief 声明 Core 与 Other Extras 的 persistent transfer 窄桥接。
 */

#ifndef QCCURLPERSISTENTTRANSFERBRIDGE_P_H
#define QCCURLPERSISTENTTRANSFERBRIDGE_P_H

#include "QCGlobal.h"

#include <QString>

#include <curl/curl.h>
#include <functional>

namespace QCurl {

using QCCurlTransferToken                  = quintptr;
using QCCurlPersistentTransferConfigurator = std::function<bool(CURL *handle, QString *error)>;
using QCCurlPersistentTransferCompletionHandler
    = std::function<void(QCCurlTransferToken token, CURLcode result, long httpStatus)>;

/**
 * @brief 在 Core 内创建、配置并注册 persistent easy handle。
 *
 * 配置回调只在 Core 创建的 easy handle 仍由内部生命周期守卫持有时同步执行；
 * Other Extras 不需要链接或实例化内部 handle manager。注册成功后，跨库只保留
 * opaque transfer token。
 */
[[nodiscard]] QCURL_EXPORT bool registerPersistentTransfer(
    QCCurlPersistentTransferConfigurator configure,
    QCCurlPersistentTransferCompletionHandler completion,
    QCCurlTransferToken *token = nullptr,
    QString *error             = nullptr);

/// 通过 opaque token 请求移除 Core-owned persistent transfer。
QCURL_EXPORT void removePersistentTransfer(QCCurlTransferToken token);

} // namespace QCurl

#endif // QCCURLPERSISTENTTRANSFERBRIDGE_P_H
