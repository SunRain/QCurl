/**
 * @file
 * @brief 声明 Blocking Extras 的 libcurl 阻塞执行适配器。
 */

#ifndef QCBLOCKINGCURLADAPTER_P_H
#define QCBLOCKINGCURLADAPTER_P_H

#include "QCBlockingNetworkResult.h"
#include "QCBlockingCookieStore.h"
#include "QCNetworkHttpMethod.h"
#include "QCNetworkRequest.h"
#include "private/QCBlockingRequestBody_p.h"

#include <QByteArray>

namespace QCurl::Internal {

[[nodiscard]] QCBlockingNetworkResult performBlockingRequest(const QCNetworkRequest &request,
                                                             HttpMethod method,
                                                             const QByteArray &body);
[[nodiscard]] QCBlockingNetworkResult performBlockingRequest(const QCNetworkRequest &request,
                                                             HttpMethod method,
                                                             QCBlockingRequestBody body);
[[nodiscard]] QCBlockingNetworkResult performBlockingRequest(const QCNetworkRequest &request,
                                                             HttpMethod method,
                                                             QCBlockingRequestBody body,
                                                             const QCCookieSnapshot &cookies);

} // namespace QCurl::Internal

#endif // QCBLOCKINGCURLADAPTER_P_H
