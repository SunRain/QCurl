/**
 * @file
 * @brief 声明 Blocking Extras 的 libcurl HTTP method 配置辅助函数。
 */

#ifndef QCBLOCKINGCURLMETHODSETUP_P_H
#define QCBLOCKINGCURLMETHODSETUP_P_H

#include "QCNetworkHttpMethod.h"
#include "private/QCBlockingRequestBody_p.h"

#include <QByteArray>

#include <curl/curl.h>

namespace QCurl::Internal {

[[nodiscard]] bool configureBlockingCurlMethod(CURL *handle,
                                               HttpMethod method,
                                               const QByteArray &customMethod,
                                               QCBlockingRequestBodyReadState *readState);

} // namespace QCurl::Internal

#endif // QCBLOCKINGCURLMETHODSETUP_P_H
