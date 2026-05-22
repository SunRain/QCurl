/**
 * @file
 * @brief 声明 Blocking Extras 的 libcurl 阻塞执行适配器。
 */

#ifndef QCBLOCKINGCURLADAPTER_P_H
#define QCBLOCKINGCURLADAPTER_P_H

#include "QCBlockingNetworkClient.h"
#include "QCBlockingNetworkResult.h"
#include "QCBlockingCookieStore.h"
#include "QCNetworkHttpMethod.h"
#include "QCNetworkRequest.h"
#include "private/QCBlockingRequestBody_p.h"

#include <QByteArray>
#include <QByteArrayView>

class QIODevice;

namespace QCurl::Internal {

[[nodiscard]] QCBlockingNetworkResult performBlockingRequest(const QCNetworkRequest &request,
                                                             HttpMethod method,
                                                             const QByteArray &body,
                                                             const QCBlockingRequestOptions
                                                                 &options = {});
[[nodiscard]] QCBlockingNetworkResult performBlockingRequest(const QCNetworkRequest &request,
                                                             HttpMethod method,
                                                             QCBlockingRequestBody body,
                                                             const QCBlockingRequestOptions
                                                                 &options = {});
[[nodiscard]] QCBlockingNetworkResult performBlockingRequest(const QCNetworkRequest &request,
                                                             HttpMethod method,
                                                             QCBlockingRequestBody body,
                                                             const QCCookieSnapshot &cookies,
                                                             const QCBlockingRequestOptions
                                                                 &options = {});
[[nodiscard]] QCBlockingNetworkResult performBlockingCustomRequest(
    const QCNetworkRequest &request,
    QByteArrayView method,
    QCBlockingRequestBody body,
    const QCCookieSnapshot &cookies,
    const QCBlockingRequestOptions &options = {});
[[nodiscard]] QCBlockingNetworkResult performBlockingDownloadToDevice(
    const QCNetworkRequest &request,
    HttpMethod method,
    QCBlockingRequestBody body,
    QIODevice *output,
    const QCBlockingRequestOptions &options = {},
    qint64 abortAfterBytes = -1);

} // namespace QCurl::Internal

#endif // QCBLOCKINGCURLADAPTER_P_H
