/**
 * @file
 * @brief 声明 Blocking Extras 的 Core 协议配置辅助函数。
 */

#ifndef QCBLOCKINGCURLPROTOCOLSETUP_P_H
#define QCBLOCKINGCURLPROTOCOLSETUP_P_H

#include <curl/curl.h>

namespace QCurl {

class QCNetworkRequest;

namespace Internal {

struct RequestOptionStorage;

[[nodiscard]] bool configureProtocolOptions(CURL *handle,
                                            const QCNetworkRequest &request,
                                            RequestOptionStorage *storage);

} // namespace Internal
} // namespace QCurl

#endif // QCBLOCKINGCURLPROTOCOLSETUP_P_H
