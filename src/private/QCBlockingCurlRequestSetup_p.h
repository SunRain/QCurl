/**
 * @file
 * @brief 声明 Blocking Extras 的 libcurl 请求配置辅助函数。
 */

#ifndef QCBLOCKINGCURLREQUESTSETUP_P_H
#define QCBLOCKINGCURLREQUESTSETUP_P_H

#include "QCBlockingCookieStore.h"
#include "QCBlockingNetworkResult.h"
#include "private/QCCurlOptionAdapter_p.h"
#include "private/QCNetworkRequestPolicy_p.h"

#include <QByteArray>

#include <curl/curl.h>

namespace QCurl {

class QCNetworkRequest;

namespace Internal {

/// 保存一次阻塞请求配置期间必须保持存活的 libcurl 选项数据。
struct RequestOptionStorage
{
    std::optional<RequestPolicy> policy;
    QByteArray caInfo;
    // easy handle 结束后由 Blocking 适配器分别释放；两个原生选项不能共用同一列表。
    curl_slist *resolveList   = nullptr;
    curl_slist *connectToList = nullptr;
    QString failureMessage;
    bool unsupportedCapability = false;
};

/// 在各执行插点应用必需选项，失败只记录原生选项名和错误，不回显输入值。
template<typename T>
[[nodiscard]] bool applyBlockingRequestOption(CURL *handle,
                                              RequestOptionStorage *storage,
                                              CurlOptions::Option option,
                                              T value)
{
    const auto code = CurlOptions::setWithTestHook(handle, option, value);
    if (code == CURLE_OK) {
        return true;
    }
    storage->failureMessage = QStringLiteral("Blocking Extras failed to set %1: %2")
                                  .arg(QString::fromUtf8(option.name),
                                       QString::fromUtf8(curl_easy_strerror(code)));
    return false;
}

[[nodiscard]] bool configureRequestOptions(CURL *handle,
                                           const QCNetworkRequest &request,
                                           RequestOptionStorage *storage);
[[nodiscard]] bool configureProxyOptions(CURL *handle,
                                         const RequestPolicy &policy,
                                         RequestOptionStorage *storage);
[[nodiscard]] bool configureTransferOptions(CURL *handle,
                                            const RequestPolicy &policy,
                                            RequestOptionStorage *storage);
[[nodiscard]] bool configureAuthOptions(CURL *handle,
                                        const RequestPolicy &policy,
                                        RequestOptionStorage *storage);
[[nodiscard]] bool appendRequestHeaders(CURL *handle,
                                        const QCNetworkRequest &request,
                                        curl_slist **headers);
[[nodiscard]] Q_DECL_HIDDEN bool appendRequestHeaders(CURL *handle,
                                                      const QCNetworkRequest &request,
                                                      curl_slist **headers,
                                                      QString *error);
[[nodiscard]] QByteArray cookieHeaderValue(const QCCookieSnapshot &snapshot);
[[nodiscard]] QCCookieDelta extractCookieDelta(const QCBlockingNetworkResult::HeaderList &headers);

} // namespace Internal
} // namespace QCurl

#endif // QCBLOCKINGCURLREQUESTSETUP_P_H
