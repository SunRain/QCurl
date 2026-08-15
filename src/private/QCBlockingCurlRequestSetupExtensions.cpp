/**
 * @file
 * @brief 实现 Blocking Extras 的扩展请求配置辅助函数。
 */

#include "QCNetworkProxyConfig.h"
#include "QCNetworkRequest.h"
#include "QCNetworkRequestConfig.h"
#include "private/QCBlockingCurlRequestSetup_p.h"
#include "private/QCCurlOptionAdapter_p.h"

#include <QByteArrayView>

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

bool setOffTOption(CURL *handle, CURLoption option, const char *optionName, qint64 value)
{
    return CurlOptions::setWithTestHook(handle, option, optionName, static_cast<curl_off_t>(value))
           == CURLE_OK;
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

unsigned long curlHttpAuth(QCNetworkHttpAuthMethod method)
{
    switch (method) {
        case QCNetworkHttpAuthMethod::Basic:
            return CURLAUTH_BASIC;
        case QCNetworkHttpAuthMethod::Any:
            return CURLAUTH_ANY;
        case QCNetworkHttpAuthMethod::AnySafe:
            return CURLAUTH_ANYSAFE;
    }

    return CURLAUTH_BASIC;
}

bool isHeaderSet(const QCNetworkRequest &request, QByteArrayView headerName)
{
    const QByteArray normalizedHeaderName = headerName.toByteArray();
    const QList<QByteArray> names         = request.rawHeaderList();
    for (const QByteArray &name : names) {
        if (name.trimmed().compare(normalizedHeaderName, Qt::CaseInsensitive) == 0) {
            return true;
        }
    }
    return false;
}

bool failOption(RequestOptionStorage *storage, const char *optionName)
{
    storage->failureMessage = QStringLiteral("Blocking Extras failed to set %1")
                                    .arg(QString::fromUtf8(optionName));
    return false;
}

} // namespace

bool configureProxyOptions(CURL *handle,
                           const QCNetworkRequest &request,
                           RequestOptionStorage *storage)
{
    if (const auto proxy = request.proxyConfig(); proxy.has_value()) {
        storage->proxyHost = proxy->hostName().toUtf8();
        if (!storage->proxyHost.isEmpty()) {
            if (!setStringOption(handle, CURLOPT_PROXY, "CURLOPT_PROXY", storage->proxyHost)) {
                return failOption(storage, "CURLOPT_PROXY");
            }
        }
        if (proxy->port() > 0) {
            if (!setLongOption(handle, CURLOPT_PROXYPORT, "CURLOPT_PROXYPORT", proxy->port())) {
                return failOption(storage, "CURLOPT_PROXYPORT");
            }
        }
        if (!setLongOption(handle,
                           CURLOPT_PROXYTYPE,
                           "CURLOPT_PROXYTYPE",
                           curlProxyType(proxy->type()))) {
            return failOption(storage, "CURLOPT_PROXYTYPE");
        }
        storage->proxyUser     = proxy->userName().toUtf8();
        storage->proxyPassword = proxy->password().toUtf8();
        if (!storage->proxyUser.isEmpty()) {
            if (!setStringOption(handle,
                                 CURLOPT_PROXYUSERNAME,
                                 "CURLOPT_PROXYUSERNAME",
                                 storage->proxyUser)) {
                return failOption(storage, "CURLOPT_PROXYUSERNAME");
            }
        }
        if (!storage->proxyPassword.isEmpty()) {
            if (!setStringOption(handle,
                                 CURLOPT_PROXYPASSWORD,
                                 "CURLOPT_PROXYPASSWORD",
                                 storage->proxyPassword)) {
                return failOption(storage, "CURLOPT_PROXYPASSWORD");
            }
        }
    }
    return true;
}

bool configureTransferOptions(CURL *handle,
                              const QCNetworkRequest &request,
                              RequestOptionStorage *storage)
{
    const bool hasRefererHeader = isHeaderSet(request, QByteArrayLiteral("referer"));
    if (!hasRefererHeader && !request.referer().isEmpty()) {
        storage->referer = request.referer().toUtf8();
        if (!setStringOption(handle, CURLOPT_REFERER, "CURLOPT_REFERER", storage->referer)) {
            return failOption(storage, "CURLOPT_REFERER");
        }
    }

    const bool hasAcceptEncodingHeader = isHeaderSet(request, QByteArrayLiteral("accept-encoding"));
    if (!hasAcceptEncodingHeader && request.autoDecompressionEnabled()) {
        storage->acceptEncoding = request.acceptedEncodings().join(QLatin1Char(',')).toUtf8();
        if (!setStringOption(handle,
                             CURLOPT_ACCEPT_ENCODING,
                             "CURLOPT_ACCEPT_ENCODING",
                             storage->acceptEncoding)) {
            return failOption(storage, "CURLOPT_ACCEPT_ENCODING");
        }
    }

    if (const auto bytesPerSec = request.maxDownloadBytesPerSec();
        bytesPerSec.has_value() && bytesPerSec.value() > 0) {
        if (!setOffTOption(handle,
                           CURLOPT_MAX_RECV_SPEED_LARGE,
                           "CURLOPT_MAX_RECV_SPEED_LARGE",
                           bytesPerSec.value())) {
            return failOption(storage, "CURLOPT_MAX_RECV_SPEED_LARGE");
        }
    }
    if (const auto bytesPerSec = request.maxUploadBytesPerSec();
        bytesPerSec.has_value() && bytesPerSec.value() > 0) {
        if (!setOffTOption(handle,
                           CURLOPT_MAX_SEND_SPEED_LARGE,
                           "CURLOPT_MAX_SEND_SPEED_LARGE",
                           bytesPerSec.value())) {
            return failOption(storage, "CURLOPT_MAX_SEND_SPEED_LARGE");
        }
    }
    return true;
}

bool configureAuthOptions(CURL *handle,
                          const QCNetworkRequest &request,
                          RequestOptionStorage *storage)
{
    bool hasSensitiveHeader           = false;
    const bool hasAuthorizationHeader = isHeaderSet(request, QByteArrayLiteral("authorization"));
    hasSensitiveHeader = hasAuthorizationHeader || isHeaderSet(request, QByteArrayLiteral("cookie"))
                         || isHeaderSet(request, QByteArrayLiteral("proxy-authorization"));

    if (const auto auth = request.httpAuth(); auth.has_value() && !hasAuthorizationHeader) {
        hasSensitiveHeader        = true;
        storage->httpAuthUser     = auth->userName().toUtf8();
        storage->httpAuthPassword = auth->password().toUtf8();
        if (!setStringOption(handle, CURLOPT_USERNAME, "CURLOPT_USERNAME", storage->httpAuthUser)) {
            return failOption(storage, "CURLOPT_USERNAME");
        }
        if (!setStringOption(handle,
                             CURLOPT_PASSWORD,
                             "CURLOPT_PASSWORD",
                             storage->httpAuthPassword)) {
            return failOption(storage, "CURLOPT_PASSWORD");
        }
        if (!setLongOption(handle,
                           CURLOPT_HTTPAUTH,
                           "CURLOPT_HTTPAUTH",
                           static_cast<long>(curlHttpAuth(auth->method())))) {
            return failOption(storage, "CURLOPT_HTTPAUTH");
        }
        if (auth->allowUnrestrictedAuth() && request.followLocation()) {
            if (!setLongOption(handle,
                               CURLOPT_UNRESTRICTED_AUTH,
                               "CURLOPT_UNRESTRICTED_AUTH",
                               Internal::CurlOptions::kEnabled)) {
                return failOption(storage, "CURLOPT_UNRESTRICTED_AUTH");
            }
        }
    }

    if (request.followLocation() && request.allowUnrestrictedSensitiveHeadersOnRedirect()
        && hasSensitiveHeader) {
        if (!setLongOption(handle,
                           CURLOPT_UNRESTRICTED_AUTH,
                           "CURLOPT_UNRESTRICTED_AUTH",
                           Internal::CurlOptions::kEnabled)) {
            return failOption(storage, "CURLOPT_UNRESTRICTED_AUTH");
        }
    }
    return true;
}

} // namespace QCurl::Internal
