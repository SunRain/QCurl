#include "private/QCBlockingCurlAdapter_p.h"

#include "CurlFeatureProbe.h"
#include "QCNetworkError.h"
#include "QCNetworkHttpVersion.h"
#include "QCNetworkProxyConfig.h"
#include "QCNetworkSslConfig.h"
#include "QCNetworkTimeoutConfig.h"
#include "private/CurlGlobalConstructor_p.h"

#include <QNetworkCookie>
#include <QScopeGuard>
#include <QStringList>

#include <curl/curl.h>

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

curl_slist *appendStringList(curl_slist *list, const QStringList &values)
{
    for (const auto &value : values) {
        const QByteArray bytes = value.toUtf8();
        list = curl_slist_append(list, bytes.constData());
    }
    return list;
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

long curlProxyType(QCNetworkProxyConfig::ProxyType type)
{
    switch (type) {
        case QCNetworkProxyConfig::ProxyType::None:
        case QCNetworkProxyConfig::ProxyType::Http:
            return CURLPROXY_HTTP;
        case QCNetworkProxyConfig::ProxyType::Https:
#ifdef CURLPROXY_HTTPS
            return CURLPROXY_HTTPS;
#else
            return CURLPROXY_HTTP;
#endif
        case QCNetworkProxyConfig::ProxyType::Socks4:
            return CURLPROXY_SOCKS4;
        case QCNetworkProxyConfig::ProxyType::Socks4A:
            return CURLPROXY_SOCKS4A;
        case QCNetworkProxyConfig::ProxyType::Socks5:
            return CURLPROXY_SOCKS5;
        case QCNetworkProxyConfig::ProxyType::Socks5Hostname:
            return CURLPROXY_SOCKS5_HOSTNAME;
    }

    return CURLPROXY_HTTP;
}

size_t writeBodyCallback(char *ptr, size_t size, size_t nmemb, void *userdata)
{
    auto *body = static_cast<QByteArray *>(userdata);
    if (!body) {
        return 0;
    }

    const size_t totalSize = size * nmemb;
    body->append(ptr, static_cast<qsizetype>(totalSize));
    return totalSize;
}

size_t writeHeaderCallback(char *ptr, size_t size, size_t nmemb, void *userdata)
{
    auto *headers = static_cast<QCBlockingNetworkResult::HeaderList *>(userdata);
    if (!headers) {
        return 0;
    }

    const size_t totalSize = size * nmemb;
    QByteArray line(ptr, static_cast<qsizetype>(totalSize));
    line = line.trimmed();
    const int colon = line.indexOf(':');
    if (colon > 0) {
        headers->append({line.left(colon).trimmed(), line.mid(colon + 1).trimmed()});
    }
    return totalSize;
}

bool appendRequestHeaders(CURL *handle, const QCNetworkRequest &request, curl_slist **headers)
{
    const QList<QByteArray> names = request.rawHeaderList();
    for (const QByteArray &name : names) {
        const QByteArray line = name + QByteArrayLiteral(": ") + request.rawHeader(name);
        curl_slist *next = curl_slist_append(*headers, line.constData());
        if (!next) {
            return false;
        }
        *headers = next;
    }

    return !*headers || curl_easy_setopt(handle, CURLOPT_HTTPHEADER, *headers) == CURLE_OK;
}

QByteArray cookieHeaderValue(const QCCookieSnapshot &snapshot)
{
    QList<QByteArray> pairs;
    for (const QNetworkCookie &cookie : snapshot.cookies()) {
        if (!cookie.name().isEmpty()) {
            pairs.append(cookie.name() + QByteArrayLiteral("=") + cookie.value());
        }
    }
    return pairs.join("; ");
}

QCCookieDelta extractCookieDelta(const QCBlockingNetworkResult::HeaderList &headers)
{
    QList<QNetworkCookie> cookies;
    for (const auto &header : headers) {
        if (header.first.compare(QByteArrayLiteral("Set-Cookie"), Qt::CaseInsensitive) != 0) {
            continue;
        }
        cookies.append(QNetworkCookie::parseCookies(header.second));
    }
    return QCCookieDelta(cookies);
}

struct RequestOptionStorage
{
    QByteArray url;
    QByteArray proxyHost;
    QByteArray proxyUser;
    QByteArray proxyPassword;
    QByteArray caInfo;
    curl_slist *resolveList = nullptr;
    curl_slist *connectToList = nullptr;
};

bool configureRequestOptions(CURL *handle,
                             const QCNetworkRequest &request,
                             RequestOptionStorage *storage)
{
    storage->url = request.url().toString().toUtf8();
    if (!setStringOption(handle, CURLOPT_URL, storage->url)) {
        return false;
    }

    setLongOption(handle, CURLOPT_FOLLOWLOCATION, request.followLocation() ? 1L : 0L);
    if (const auto redirects = request.maxRedirects(); redirects.has_value()) {
        setLongOption(handle, CURLOPT_MAXREDIRS, static_cast<long>(redirects.value()));
    }
    setLongOption(handle, CURLOPT_HTTP_VERSION, curlHttpVersion(request.httpVersion()));
    setLongOption(handle, CURLOPT_SSL_VERIFYPEER, request.sslConfig().verifyPeer() ? 1L : 0L);
    setLongOption(handle, CURLOPT_SSL_VERIFYHOST, request.sslConfig().verifyHost() ? 2L : 0L);

    const auto timeout = request.timeoutConfig();
    if (timeout.connectTimeout().has_value()) {
        setLongOption(handle,
                      CURLOPT_CONNECTTIMEOUT_MS,
                      static_cast<long>(timeout.connectTimeout()->count()));
    }
    if (timeout.totalTimeout().has_value()) {
        setLongOption(handle, CURLOPT_TIMEOUT_MS, static_cast<long>(timeout.totalTimeout()->count()));
    }

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

    if (const auto proxy = request.proxyConfig(); proxy.has_value()) {
        storage->proxyHost = proxy->hostName().toUtf8();
        if (!storage->proxyHost.isEmpty()) {
            setStringOption(handle, CURLOPT_PROXY, storage->proxyHost);
        }
        if (proxy->port() > 0) {
            setLongOption(handle, CURLOPT_PROXYPORT, proxy->port());
        }
        setLongOption(handle, CURLOPT_PROXYTYPE, curlProxyType(proxy->type()));
        storage->proxyUser = proxy->userName().toUtf8();
        storage->proxyPassword = proxy->password().toUtf8();
        if (!storage->proxyUser.isEmpty()) {
            setStringOption(handle, CURLOPT_PROXYUSERNAME, storage->proxyUser);
        }
        if (!storage->proxyPassword.isEmpty()) {
            setStringOption(handle, CURLOPT_PROXYPASSWORD, storage->proxyPassword);
        }
    }

    storage->caInfo = request.sslConfig().caCertPath().toUtf8();
    if (!storage->caInfo.isEmpty()) {
        setStringOption(handle, CURLOPT_CAINFO, storage->caInfo);
    }

    return true;
}

void configureUploadMethod(CURL *handle,
                           const char *methodName,
                           QCBlockingRequestBodyReadState *readState)
{
    curl_easy_setopt(handle, CURLOPT_UPLOAD, 1L);
    curl_easy_setopt(handle, CURLOPT_CUSTOMREQUEST, methodName);
    curl_easy_setopt(handle, CURLOPT_READFUNCTION, readBlockingRequestBodyCallback);
    curl_easy_setopt(handle, CURLOPT_READDATA, readState);
    curl_easy_setopt(handle, CURLOPT_INFILESIZE_LARGE, curlBodySize(readState->body));
}

void configureMethod(CURL *handle,
                     HttpMethod method,
                     QCBlockingRequestBodyReadState *readState)
{
    if (method == HttpMethod::Post) {
        if (isStreamingBody(readState->body)) {
            configureUploadMethod(handle, "POST", readState);
        } else {
            curl_easy_setopt(handle, CURLOPT_POST, 1L);
            curl_easy_setopt(handle, CURLOPT_POSTFIELDS, readState->body.bytes->constData());
            curl_easy_setopt(handle, CURLOPT_POSTFIELDSIZE_LARGE, curlBodySize(readState->body));
        }
    } else if (method == HttpMethod::Put) {
        configureUploadMethod(handle, "PUT", readState);
    } else {
        curl_easy_setopt(handle, CURLOPT_HTTPGET, 1L);
    }
}

} // namespace

QCBlockingNetworkResult performBlockingRequest(const QCNetworkRequest &request,
                                               HttpMethod method,
                                               const QByteArray &body)
{
    return performBlockingRequest(request, method, makeBlockingBytesBody(body));
}

QCBlockingNetworkResult performBlockingRequest(const QCNetworkRequest &request,
                                               HttpMethod method,
                                               QCBlockingRequestBody body)
{
    return performBlockingRequest(request, method, std::move(body), QCCookieSnapshot());
}

QCBlockingNetworkResult performBlockingRequest(const QCNetworkRequest &request,
                                               HttpMethod method,
                                               QCBlockingRequestBody body,
                                               const QCCookieSnapshot &cookies)
{
    const auto availability = CurlFeatureProbe::instance().minimumRuntimeAvailability();
    if (!availability.supported) {
        return QCBlockingNetworkResult::failure(NetworkError::InvalidRequest, availability.reason);
    }

    CurlGlobalConstructor::instance();
    CURL *handle = curl_easy_init();
    if (!handle) {
        return QCBlockingNetworkResult::failure(NetworkError::InvalidRequest,
                                               QStringLiteral("Blocking Extras curl init failed"));
    }

    RequestOptionStorage storage;
    curl_slist *requestHeaders = nullptr;
    QByteArray responseBody;
    QCBlockingNetworkResult::HeaderList responseHeaders;
    QCBlockingRequestBodyReadState readState{std::move(body), 0, QString()};

    const auto cleanup = qScopeGuard([&]() {
        if (requestHeaders) {
            curl_slist_free_all(requestHeaders);
        }
        if (storage.resolveList) {
            curl_slist_free_all(storage.resolveList);
        }
        if (storage.connectToList) {
            curl_slist_free_all(storage.connectToList);
        }
        curl_easy_cleanup(handle);
    });

    if (!configureRequestOptions(handle, request, &storage)
        || !appendRequestHeaders(handle, request, &requestHeaders)) {
        return QCBlockingNetworkResult::failure(
            NetworkError::InvalidRequest,
            QStringLiteral("Blocking Extras request option configuration failed"));
    }

    const QByteArray cookieHeader = cookieHeaderValue(cookies);
    if (!cookieHeader.isEmpty()) {
        curl_easy_setopt(handle, CURLOPT_COOKIE, cookieHeader.constData());
    }
    curl_easy_setopt(handle, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(handle, CURLOPT_WRITEFUNCTION, writeBodyCallback);
    curl_easy_setopt(handle, CURLOPT_WRITEDATA, &responseBody);
    curl_easy_setopt(handle, CURLOPT_HEADERFUNCTION, writeHeaderCallback);
    curl_easy_setopt(handle, CURLOPT_HEADERDATA, &responseHeaders);
    configureMethod(handle, method, &readState);

    const CURLcode code = curl_easy_perform(handle);
    long httpStatus = 0;
    curl_easy_getinfo(handle, CURLINFO_RESPONSE_CODE, &httpStatus);

    if (code != CURLE_OK) {
        if (!readState.failureMessage.isEmpty()) {
            return QCBlockingNetworkResult::failure(NetworkError::InvalidRequest,
                                                   readState.failureMessage,
                                                   static_cast<int>(httpStatus));
        }
        return QCBlockingNetworkResult::failure(
            fromCurlCode(static_cast<int>(code)),
            QString::fromUtf8(curl_easy_strerror(code)),
            static_cast<int>(httpStatus));
    }
    if (httpStatus >= 400) {
        return QCBlockingNetworkResult::failure(fromHttpCode(httpStatus),
                                               QStringLiteral("HTTP error %1").arg(httpStatus),
                                               static_cast<int>(httpStatus));
    }

    return QCBlockingNetworkResult::success(static_cast<int>(httpStatus),
                                           responseBody,
                                           responseHeaders,
                                           extractCookieDelta(responseHeaders));
}

} // namespace QCurl::Internal
