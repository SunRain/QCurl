/**
 * @file
 * @brief 声明 Blocking Extras 的 libcurl 请求配置辅助函数。
 */

#ifndef QCBLOCKINGCURLREQUESTSETUP_P_H
#define QCBLOCKINGCURLREQUESTSETUP_P_H

#include "QCBlockingCookieStore.h"
#include "QCBlockingNetworkResult.h"

#include <QByteArray>

#include <curl/curl.h>

namespace QCurl {

class QCNetworkRequest;

namespace Internal {

/// 保存一次阻塞请求配置期间必须保持存活的 libcurl 选项数据。
struct RequestOptionStorage
{
    QByteArray url;
    QByteArray range;
    QByteArray proxyHost;
    QByteArray proxyUser;
    QByteArray proxyPassword;
    QByteArray caInfo;
    QByteArray referer;
    QByteArray acceptEncoding;
    QByteArray allowedProtocols;
    QByteArray allowedRedirectProtocols;
    QByteArray httpAuthUser;
    QByteArray httpAuthPassword;
    curl_slist *resolveList   = nullptr;
    curl_slist *connectToList = nullptr;
    QString failureMessage;
    bool unsupportedCapability = false;
};

[[nodiscard]] bool configureRequestOptions(CURL *handle,
                                           const QCNetworkRequest &request,
                                           RequestOptionStorage *storage);
[[nodiscard]] bool configureProxyOptions(CURL *handle,
                                         const QCNetworkRequest &request,
                                         RequestOptionStorage *storage);
[[nodiscard]] bool configureTransferOptions(CURL *handle,
                                            const QCNetworkRequest &request,
                                            RequestOptionStorage *storage);
[[nodiscard]] bool configureAuthOptions(CURL *handle,
                                        const QCNetworkRequest &request,
                                        RequestOptionStorage *storage);
[[nodiscard]] bool appendRequestHeaders(CURL *handle,
                                        const QCNetworkRequest &request,
                                        curl_slist **headers);
[[nodiscard]] Q_DECL_HIDDEN bool appendRequestHeaders(CURL *handle,
                                                      const QCNetworkRequest &request,
                                                      curl_slist **headers,
                                                      QString *error);
[[nodiscard]] QByteArray cookieHeaderValue(const QCCookieSnapshot &snapshot);
[[nodiscard]] QCCookieDelta extractCookieDelta(
    const QCBlockingNetworkResult::HeaderList &headers);

} // namespace Internal
} // namespace QCurl

#endif // QCBLOCKINGCURLREQUESTSETUP_P_H
