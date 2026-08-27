/**
 * @file
 * @brief 声明 Test Support 注入 Core 的不透明进程期 provider 合同。
 */

#ifndef QCNETWORKMOCKPROVIDER_P_H
#define QCNETWORKMOCKPROVIDER_P_H

#include "QCGlobal.h"
#include "QCNetworkError.h"
#include "QCNetworkHttpMethod.h"

#include <QByteArray>
#include <QList>
#include <QMap>
#include <QPair>
#include <QUrl>

#include <optional>

namespace QCurl {

class QCNetworkAccessManager;

namespace Internal {

/** @brief 描述 Test Support 向 Core 传递的单次 mock 结果。 */
struct QCNetworkMockData
{
    QByteArray response;
    int statusCode = 200;
    QMap<QByteArray, QByteArray> headers;
    std::optional<QByteArray> rawHeaderData;
    NetworkError error = NetworkError::NoError;
    bool isError       = false;
};

/** @brief 描述 Core 回传给 Test Support 的请求捕获快照。 */
struct QCNetworkCapturedRequestSnapshot
{
    QUrl url;
    HttpMethod method = HttpMethod::Get;
    QByteArray customMethod;
    QList<QPair<QByteArray, QByteArray>> headers;
    QByteArray bodyPreview;
    qsizetype bodySize = 0;
    bool followLocation = false;
    std::optional<qint64> connectTimeoutMs;
    std::optional<qint64> totalTimeoutMs;
};

/** @brief 定义 Test Support 安装到 Core 的进程期回调表。 */
struct QCNetworkMockProvider
{
    void *(*handlerForManager)(const QCNetworkAccessManager *manager) = nullptr;
    bool (*captureEnabled)(void *handler)                             = nullptr;
    int (*captureBodyPreviewLimit)(void *handler)                     = nullptr;
    void (*recordRequest)(void *handler,
                          const QCNetworkCapturedRequestSnapshot &request) = nullptr;
    bool (*hasMock)(void *handler, HttpMethod method, const QUrl &url)      = nullptr;
    bool (*consumeMock)(void *handler,
                        HttpMethod method,
                        const QUrl &url,
                        QCNetworkMockData &out) = nullptr;
    int (*globalDelay)(void *handler) = nullptr;
};

/** @brief 返回已安装的 provider；未链接 Test Support 时返回 nullptr。 */
[[nodiscard]] const QCNetworkMockProvider *networkMockProvider() noexcept;

} // namespace Internal

/** @brief 注册进程期有效的 Test Support 回调表。 */
[[nodiscard]] QCURL_EXPORT bool installNetworkMockProvider(
    const Internal::QCNetworkMockProvider *provider) noexcept;

} // namespace QCurl

#endif // QCNETWORKMOCKPROVIDER_P_H
