/**
 * @file
 * @brief 实现 Blocking Extras 的 libcurl 请求配置辅助函数。
 */

#include "QCNetworkHttpVersion.h"
#include "QCNetworkRequest.h"
#include "QCNetworkRequestConfig.h"
#include "QCNetworkSslConfig.h"
#include "QCNetworkTimeoutConfig.h"
#include "private/QCBlockingCurlProtocolSetup_p.h"
#include "private/QCBlockingCurlRequestSetup_p.h"
#include "private/QCCurlOptionAdapter_p.h"

#include <QStringList>

#include <utility>

namespace QCurl::Internal {
namespace {

bool setStringOption(CURL *handle,
                     CURLoption option,
                     const char *optionName,
                     const QByteArray &value)
{
    return CurlOptions::setWithTestHook(handle, option, optionName, value.constData()) == CURLE_OK;
}

bool setLongOption(CURL *handle, CURLoption option, const char *optionName, long value)
{
    return CurlOptions::setWithTestHook(handle, option, optionName, value) == CURLE_OK;
}

long curlHttpVersion(QCNetworkHttpVersion version)
{
    switch (version) {
        case QCNetworkHttpVersion::Http1_0:
            return CURL_HTTP_VERSION_1_0;
        case QCNetworkHttpVersion::Http1_1:
            return CURL_HTTP_VERSION_1_1;
        case QCNetworkHttpVersion::Http2:
        case QCNetworkHttpVersion::Http2TLS:
            return CURL_HTTP_VERSION_2TLS;
        case QCNetworkHttpVersion::Http3:
#ifdef CURL_HTTP_VERSION_3
            return CURL_HTTP_VERSION_3;
#else
            return CURL_HTTP_VERSION_NONE;
#endif
        case QCNetworkHttpVersion::Http3Only:
#ifdef CURL_HTTP_VERSION_3ONLY
            return CURL_HTTP_VERSION_3ONLY;
#elif defined(CURL_HTTP_VERSION_3)
            return CURL_HTTP_VERSION_3;
#else
            return CURL_HTTP_VERSION_NONE;
#endif
        case QCNetworkHttpVersion::HttpAny:
            return CURL_HTTP_VERSION_NONE;
    }

    return CURL_HTTP_VERSION_NONE;
}

long curlPostRedirectPolicy(QCNetworkPostRedirectPolicy policy)
{
    switch (policy) {
        case QCNetworkPostRedirectPolicy::Default:
            return Internal::CurlOptions::kDisabled;
        case QCNetworkPostRedirectPolicy::KeepPost301:
            return CURL_REDIR_POST_301;
        case QCNetworkPostRedirectPolicy::KeepPost302:
            return CURL_REDIR_POST_302;
        case QCNetworkPostRedirectPolicy::KeepPost303:
            return CURL_REDIR_POST_303;
        case QCNetworkPostRedirectPolicy::KeepPostAll:
            return CURL_REDIR_POST_ALL;
    }

    return Internal::CurlOptions::kDisabled;
}

bool failOption(RequestOptionStorage *storage, const char *optionName)
{
    storage->failureMessage = QStringLiteral("Blocking Extras failed to set %1")
                                  .arg(QString::fromUtf8(optionName));
    return false;
}

#ifdef QCURL_ENABLE_ADVANCED_REQUEST_NETWORK_PATH_API
[[nodiscard]] bool appendStringList(curl_slist **list,
                                    const QStringList &values,
                                    QString *error,
                                    const char *optionName)
{
    return Internal::CurlOptions::buildCurlSlist(values, list, error, optionName);
}
#endif

} // namespace

bool appendRequestHeaders(CURL *handle, const QCNetworkRequest &request, curl_slist **headers)
{
    QString ignoredError;
    return appendRequestHeaders(handle, request, headers, &ignoredError);
}

bool appendRequestHeaders(CURL *handle,
                          const QCNetworkRequest &request,
                          curl_slist **headers,
                          QString *error)
{
    if (!error) {
        return appendRequestHeaders(handle, request, headers);
    }

    const QList<QByteArray> names = request.rawHeaderList();
    for (const QByteArray &name : names) {
        const QByteArray line = name + QByteArrayLiteral(": ") + request.rawHeader(name);
        if (Internal::CurlOptions::shouldForceSlistAppendFailure("CURLOPT_HTTPHEADER")) {
            if (error) {
                *error = QStringLiteral("追加 CURLOPT_HTTPHEADER 失败（测试故障注入）");
            }
            return false;
        }
        curl_slist *next = curl_slist_append(*headers, line.constData());
        if (!next) {
            if (error) {
                *error = QStringLiteral("追加 CURLOPT_HTTPHEADER 失败");
            }
            return false;
        }
        *headers = next;
    }

    if (!*headers) {
        return true;
    }

    const CURLcode rc = CurlOptions::setWithTestHook(handle,
                                                     CURLOPT_HTTPHEADER,
                                                     "CURLOPT_HTTPHEADER",
                                                     *headers);
    if (rc == CURLE_OK) {
        return true;
    }
    if (error) {
        *error = QStringLiteral("设置 CURLOPT_HTTPHEADER 失败（%1）")
                     .arg(QString::fromUtf8(curl_easy_strerror(rc)));
    }
    return false;
}

[[nodiscard]] bool configureRedirectOptions(CURL *handle,
                                            const QCNetworkRequest &request,
                                            RequestOptionStorage *storage)
{
    if (Internal::CurlOptions::setEnabled(handle, CURLOPT_FOLLOWLOCATION, request.followLocation())
        != CURLE_OK) {
        return failOption(storage, "CURLOPT_FOLLOWLOCATION");
    }
    if (const auto redirects = request.maxRedirects(); redirects.has_value()) {
        if (!setLongOption(handle,
                           CURLOPT_MAXREDIRS,
                           "CURLOPT_MAXREDIRS",
                           static_cast<long>(redirects.value()))) {
            return failOption(storage, "CURLOPT_MAXREDIRS");
        }
    }
    if (request.followLocation()
        && request.postRedirectPolicy() != QCNetworkPostRedirectPolicy::Default) {
        if (!setLongOption(handle,
                           CURLOPT_POSTREDIR,
                           "CURLOPT_POSTREDIR",
                           curlPostRedirectPolicy(request.postRedirectPolicy()))) {
            return failOption(storage, "CURLOPT_POSTREDIR");
        }
    }
    if (request.followLocation() && request.autoRefererEnabled()) {
        if (!setLongOption(handle,
                           CURLOPT_AUTOREFERER,
                           "CURLOPT_AUTOREFERER",
                           Internal::CurlOptions::kEnabled)) {
            return failOption(storage, "CURLOPT_AUTOREFERER");
        }
    }
    return true;
}

[[nodiscard]] bool configureTransportOptions(CURL *handle,
                                             const QCNetworkRequest &request,
                                             RequestOptionStorage *storage)
{
    if (!setLongOption(handle,
                       CURLOPT_HTTP_VERSION,
                       "CURLOPT_HTTP_VERSION",
                       curlHttpVersion(request.httpVersion()))) {
        return failOption(storage, "CURLOPT_HTTP_VERSION");
    }
    if (Internal::CurlOptions::setSslVerifyPeer(handle, request.sslConfig().verifyPeer())
        != CURLE_OK) {
        return failOption(storage, "CURLOPT_SSL_VERIFYPEER");
    }
    if (Internal::CurlOptions::setSslVerifyHost(handle, request.sslConfig().verifyHost())
        != CURLE_OK) {
        return failOption(storage, "CURLOPT_SSL_VERIFYHOST");
    }
    if (request.rangeStart() >= 0 && request.rangeEnd() > request.rangeStart()) {
        storage->range
            = QStringLiteral("%1-%2").arg(request.rangeStart()).arg(request.rangeEnd()).toUtf8();
        if (!setStringOption(handle, CURLOPT_RANGE, "CURLOPT_RANGE", storage->range)) {
            return failOption(storage, "CURLOPT_RANGE");
        }
    }
    return true;
}

[[nodiscard]] bool configureTimeoutOptions(CURL *handle,
                                           const QCNetworkRequest &request,
                                           RequestOptionStorage *storage)
{
    const auto timeout = request.timeoutConfig();
    if (timeout.connectTimeout().has_value()) {
        if (Internal::CurlOptions::setConnectTimeout(handle, timeout.connectTimeout().value())
            != CURLE_OK) {
            return failOption(storage, "CURLOPT_CONNECTTIMEOUT_MS");
        }
    }
    if (timeout.totalTimeout().has_value()) {
        if (!setLongOption(handle,
                           CURLOPT_TIMEOUT_MS,
                           "CURLOPT_TIMEOUT_MS",
                           static_cast<long>(timeout.totalTimeout()->count()))) {
            return failOption(storage, "CURLOPT_TIMEOUT_MS");
        }
    }
    return true;
}

bool configureBasicRequestOptions(CURL *handle,
                                  const QCNetworkRequest &request,
                                  RequestOptionStorage *storage)
{
    storage->url = request.url().toString().toUtf8();
    if (!setStringOption(handle, CURLOPT_URL, "CURLOPT_URL", storage->url)) {
        return false;
    }
    return configureRedirectOptions(handle, request, storage)
           && configureTransportOptions(handle, request, storage)
           && configureTimeoutOptions(handle, request, storage);
}

bool configureAdvancedPathOptions(CURL *handle,
                                  const QCNetworkRequest &request,
                                  RequestOptionStorage *storage)
{
#ifdef QCURL_ENABLE_ADVANCED_REQUEST_NETWORK_PATH_API
    if (const auto resolve = request.resolveOverride(); resolve.has_value()) {
        if (!appendStringList(&storage->resolveList,
                              resolve.value(),
                              &storage->failureMessage,
                              "CURLOPT_RESOLVE")) {
            return false;
        }
        if (storage->resolveList) {
            const CURLcode rc = CurlOptions::setWithTestHook(handle,
                                                             CURLOPT_RESOLVE,
                                                             "CURLOPT_RESOLVE",
                                                             storage->resolveList);
            if (rc != CURLE_OK) {
                storage->failureMessage = QStringLiteral("设置 CURLOPT_RESOLVE 失败（%1）")
                                              .arg(QString::fromUtf8(curl_easy_strerror(rc)));
                return false;
            }
        }
    }
    if (const auto connectTo = request.connectTo(); connectTo.has_value()) {
        if (!appendStringList(&storage->connectToList,
                              connectTo.value(),
                              &storage->failureMessage,
                              "CURLOPT_CONNECT_TO")) {
            return false;
        }
        if (storage->connectToList) {
            const CURLcode rc = CurlOptions::setWithTestHook(handle,
                                                             CURLOPT_CONNECT_TO,
                                                             "CURLOPT_CONNECT_TO",
                                                             storage->connectToList);
            if (rc != CURLE_OK) {
                storage->failureMessage = QStringLiteral("设置 CURLOPT_CONNECT_TO 失败（%1）")
                                              .arg(QString::fromUtf8(curl_easy_strerror(rc)));
                return false;
            }
        }
    }
#else
    Q_UNUSED(handle);
    Q_UNUSED(request);
    Q_UNUSED(storage);
#endif
    return true;
}

bool configureRequestOptions(CURL *handle,
                             const QCNetworkRequest &request,
                             RequestOptionStorage *storage)
{
    if (!configureBasicRequestOptions(handle, request, storage)
        || !configureProtocolOptions(handle, request, storage)
        || !configureAdvancedPathOptions(handle, request, storage)
        || !configureProxyOptions(handle, request, storage)
        || !configureTransferOptions(handle, request, storage)
        || !configureAuthOptions(handle, request, storage)) {
        return false;
    }

    storage->caInfo = request.sslConfig().caCertPath().toUtf8();
    if (!storage->caInfo.isEmpty()) {
        if (!setStringOption(handle, CURLOPT_CAINFO, "CURLOPT_CAINFO", storage->caInfo)) {
            return failOption(storage, "CURLOPT_CAINFO");
        }
    }

    return true;
}

} // namespace QCurl::Internal
