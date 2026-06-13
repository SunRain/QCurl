/**
 * @file
 * @brief 实现 Blocking Extras 的 libcurl 请求配置辅助函数。
 */

#include "QCNetworkHttpVersion.h"
#include "QCNetworkRequest.h"
#include "QCNetworkRequestConfig.h"
#include "QCNetworkSslConfig.h"
#include "QCNetworkTimeoutConfig.h"
#include "private/QCBlockingCurlRequestSetup_p.h"
#include "private/QCCurlOptionAdapter_p.h"

#include <QDebug>
#include <QStringList>

#include <utility>

namespace QCurl::Internal {
namespace {

bool setStringOption(CURL *handle, CURLoption option, const QByteArray &value)
{
    return curl_easy_setopt(handle, option, value.constData()) == CURLE_OK;
}

bool setLongOption(CURL *handle, CURLoption option, long value)
{
    return curl_easy_setopt(handle, option, value) == CURLE_OK;
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

bool failUnsupportedSecurityOption(const QCNetworkRequest &request,
                                   RequestOptionStorage *storage,
                                   QString message)
{
    if (request.unsupportedSecurityOptionPolicy() == QCUnsupportedSecurityOptionPolicy::Warn) {
        qWarning() << message;
        return false;
    }
    storage->failureMessage        = std::move(message);
    storage->unsupportedCapability = true;
    return true;
}

bool failOption(RequestOptionStorage *storage, const char *optionName)
{
    storage->failureMessage = QStringLiteral("Blocking Extras failed to set %1")
                                  .arg(QString::fromUtf8(optionName));
    return false;
}

bool setProtocolListOption(CURL *handle,
                           const QCNetworkRequest &request,
                           RequestOptionStorage *storage,
                           CURLoption option,
                           const char *optionName,
                           const QStringList &protocols,
                           QByteArray *bytes)
{
#ifdef QCURL_ENABLE_TEST_HOOKS
    const QList<QByteArray> forcedOptions = qgetenv("QCURL_TEST_FORCE_CAPABILITY_ERROR").split(',');
    for (const QByteArray &forced : forcedOptions) {
        const QByteArray trimmed = forced.trimmed();
        if (trimmed == "1" || trimmed == "all" || trimmed == optionName) {
            return !failUnsupportedSecurityOption(
                request,
                storage,
                QStringLiteral("Blocking Extras does not support %1 on this libcurl")
                    .arg(QString::fromUtf8(optionName)));
        }
    }
#endif

    *bytes            = protocols.join(QLatin1Char(',')).toUtf8();
    const CURLcode rc = curl_easy_setopt(handle, option, bytes->constData());
    if (rc == CURLE_OK) {
        return true;
    }

    if (rc == CURLE_UNKNOWN_OPTION || rc == CURLE_NOT_BUILT_IN) {
        return !failUnsupportedSecurityOption(
            request,
            storage,
            QStringLiteral("Blocking Extras does not support %1 on this libcurl: %2")
                .arg(QString::fromUtf8(optionName))
                .arg(QString::fromUtf8(curl_easy_strerror(rc))));
    }

    storage->failureMessage = QStringLiteral("Blocking Extras failed to set %1: %2")
                                  .arg(QString::fromUtf8(optionName))
                                  .arg(QString::fromUtf8(curl_easy_strerror(rc)));
    return false;
}

#ifdef QCURL_ENABLE_ADVANCED_REQUEST_NETWORK_PATH_API
curl_slist *appendStringList(curl_slist *list, const QStringList &values)
{
    for (const auto &value : values) {
        const QByteArray bytes = value.toUtf8();
        list                   = curl_slist_append(list, bytes.constData());
    }
    return list;
}
#endif

} // namespace

bool appendRequestHeaders(CURL *handle, const QCNetworkRequest &request, curl_slist **headers)
{
    const QList<QByteArray> names = request.rawHeaderList();
    for (const QByteArray &name : names) {
        const QByteArray line = name + QByteArrayLiteral(": ") + request.rawHeader(name);
        curl_slist *next      = curl_slist_append(*headers, line.constData());
        if (!next) {
            return false;
        }
        *headers = next;
    }

    return !*headers || curl_easy_setopt(handle, CURLOPT_HTTPHEADER, *headers) == CURLE_OK;
}

bool configureBasicRequestOptions(CURL *handle,
                                  const QCNetworkRequest &request,
                                  RequestOptionStorage *storage)
{
    storage->url = request.url().toString().toUtf8();
    if (!setStringOption(handle, CURLOPT_URL, storage->url)) {
        return false;
    }

    if (Internal::CurlOptions::setEnabled(handle, CURLOPT_FOLLOWLOCATION, request.followLocation())
        != CURLE_OK) {
        return failOption(storage, "CURLOPT_FOLLOWLOCATION");
    }
    if (const auto redirects = request.maxRedirects(); redirects.has_value()) {
        if (!setLongOption(handle, CURLOPT_MAXREDIRS, static_cast<long>(redirects.value()))) {
            return failOption(storage, "CURLOPT_MAXREDIRS");
        }
    }
    if (request.followLocation()
        && request.postRedirectPolicy() != QCNetworkPostRedirectPolicy::Default) {
        if (!setLongOption(handle,
                           CURLOPT_POSTREDIR,
                           curlPostRedirectPolicy(request.postRedirectPolicy()))) {
            return failOption(storage, "CURLOPT_POSTREDIR");
        }
    }
    if (request.followLocation() && request.autoRefererEnabled()) {
        if (!setLongOption(handle, CURLOPT_AUTOREFERER, Internal::CurlOptions::kEnabled)) {
            return failOption(storage, "CURLOPT_AUTOREFERER");
        }
    }
    if (!setLongOption(handle, CURLOPT_HTTP_VERSION, curlHttpVersion(request.httpVersion()))) {
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
        if (!setStringOption(handle, CURLOPT_RANGE, storage->range)) {
            return failOption(storage, "CURLOPT_RANGE");
        }
    }

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
                           static_cast<long>(timeout.totalTimeout()->count()))) {
            return failOption(storage, "CURLOPT_TIMEOUT_MS");
        }
    }
    return true;
}

bool configureProtocolOptions(CURL *handle,
                              const QCNetworkRequest &request,
                              RequestOptionStorage *storage)
{
    if (const auto allowed = request.allowedProtocols(); allowed.has_value()) {
        if (!setProtocolListOption(handle,
                                   request,
                                   storage,
                                   CURLOPT_PROTOCOLS_STR,
                                   "CURLOPT_PROTOCOLS_STR",
                                   allowed.value(),
                                   &storage->allowedProtocols)) {
            return false;
        }
    }
    if (const auto redirAllowed = request.allowedRedirectProtocols(); redirAllowed.has_value()) {
        if (!setProtocolListOption(handle,
                                   request,
                                   storage,
                                   CURLOPT_REDIR_PROTOCOLS_STR,
                                   "CURLOPT_REDIR_PROTOCOLS_STR",
                                   redirAllowed.value(),
                                   &storage->allowedRedirectProtocols)) {
            return false;
        }
    }
    return true;
}

bool configureAdvancedPathOptions(CURL *handle,
                                  const QCNetworkRequest &request,
                                  RequestOptionStorage *storage)
{
#ifdef QCURL_ENABLE_ADVANCED_REQUEST_NETWORK_PATH_API
    if (const auto resolve = request.resolveOverride(); resolve.has_value()) {
        storage->resolveList = appendStringList(storage->resolveList, resolve.value());
        if (storage->resolveList
            && curl_easy_setopt(handle, CURLOPT_RESOLVE, storage->resolveList) != CURLE_OK) {
            return false;
        }
    }
    if (const auto connectTo = request.connectTo(); connectTo.has_value()) {
        storage->connectToList = appendStringList(storage->connectToList, connectTo.value());
        if (storage->connectToList
            && curl_easy_setopt(handle, CURLOPT_CONNECT_TO, storage->connectToList) != CURLE_OK) {
            return false;
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
        if (!setStringOption(handle, CURLOPT_CAINFO, storage->caInfo)) {
            return failOption(storage, "CURLOPT_CAINFO");
        }
    }

    return true;
}

} // namespace QCurl::Internal
