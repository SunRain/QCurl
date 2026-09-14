/**
 * @file
 * @brief 实现 Blocking Extras 的扩展请求配置辅助函数。
 */

#include "QCNetworkHttpHeaders.h"
#include "QCNetworkProxyConfig.h"
#include "QCNetworkRequest.h"
#include "QCNetworkRequestConfig.h"
#include "private/QCBlockingCurlRequestSetup_p.h"
#include "private/QCCurlOptionAdapter_p.h"

#include <QByteArrayView>

namespace QCurl::Internal {
namespace {

bool setStringOption(CURL *handle,
                     QCurl::Internal::CurlOptions::Option option,
                     const QByteArray &value)
{
    return CurlOptions::setWithTestHook(handle, option, value.constData()) == CURLE_OK;
}

bool setLongOption(CURL *handle, QCurl::Internal::CurlOptions::Option option, long value)
{
    return CurlOptions::setWithTestHook(handle, option, value) == CURLE_OK;
}

bool setOffTOption(CURL *handle, QCurl::Internal::CurlOptions::Option option, qint64 value)
{
    return CurlOptions::setWithTestHook(handle, option, static_cast<curl_off_t>(value)) == CURLE_OK;
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
            if (!setStringOption(handle, QCURL_CURL_OPTION(CURLOPT_PROXY), storage->proxyHost)) {
                return failOption(storage, QCURL_CURL_OPTION(CURLOPT_PROXY).name);
            }
        }
        if (proxy->port() > 0) {
            if (!setLongOption(handle, QCURL_CURL_OPTION(CURLOPT_PROXYPORT), proxy->port())) {
                return failOption(storage, QCURL_CURL_OPTION(CURLOPT_PROXYPORT).name);
            }
        }
        if (!setLongOption(handle,
                           QCURL_CURL_OPTION(CURLOPT_PROXYTYPE),
                           curlProxyType(proxy->type()))) {
            return failOption(storage, QCURL_CURL_OPTION(CURLOPT_PROXYTYPE).name);
        }
        storage->proxyUser     = proxy->userName().toUtf8();
        storage->proxyPassword = proxy->password().toUtf8();
        if (!storage->proxyUser.isEmpty()) {
            if (!setStringOption(handle,
                                 QCURL_CURL_OPTION(CURLOPT_PROXYUSERNAME),
                                 storage->proxyUser)) {
                return failOption(storage, QCURL_CURL_OPTION(CURLOPT_PROXYUSERNAME).name);
            }
        }
        if (!storage->proxyPassword.isEmpty()) {
            if (!setStringOption(handle,
                                 QCURL_CURL_OPTION(CURLOPT_PROXYPASSWORD),
                                 storage->proxyPassword)) {
                return failOption(storage, QCURL_CURL_OPTION(CURLOPT_PROXYPASSWORD).name);
            }
        }
    }
    return true;
}

bool configureTransferOptions(CURL *handle,
                              const QCNetworkRequest &request,
                              RequestOptionStorage *storage)
{
    const bool hasRefererHeader = isHeaderSet(request, QCurl::httpheaders::kReferer.toLower());
    if (!hasRefererHeader && !request.referer().isEmpty()) {
        storage->referer = request.referer().toUtf8();
        if (!setStringOption(handle, QCURL_CURL_OPTION(CURLOPT_REFERER), storage->referer)) {
            return failOption(storage, QCURL_CURL_OPTION(CURLOPT_REFERER).name);
        }
    }

    const bool hasAcceptEncodingHeader = isHeaderSet(request,
                                                     QCurl::httpheaders::kAcceptEncoding.toLower());
    if (!hasAcceptEncodingHeader && request.autoDecompressionEnabled()) {
        storage->acceptEncoding = request.acceptedEncodings().join(QLatin1Char(',')).toUtf8();
        if (!setStringOption(handle,
                             QCURL_CURL_OPTION(CURLOPT_ACCEPT_ENCODING),
                             storage->acceptEncoding)) {
            return failOption(storage, QCURL_CURL_OPTION(CURLOPT_ACCEPT_ENCODING).name);
        }
    }

    if (const auto bytesPerSec = request.maxDownloadBytesPerSec();
        bytesPerSec.has_value() && bytesPerSec.value() > 0) {
        if (!setOffTOption(handle,
                           QCURL_CURL_OPTION(CURLOPT_MAX_RECV_SPEED_LARGE),
                           bytesPerSec.value())) {
            return failOption(storage, QCURL_CURL_OPTION(CURLOPT_MAX_RECV_SPEED_LARGE).name);
        }
    }
    if (const auto bytesPerSec = request.maxUploadBytesPerSec();
        bytesPerSec.has_value() && bytesPerSec.value() > 0) {
        if (!setOffTOption(handle,
                           QCURL_CURL_OPTION(CURLOPT_MAX_SEND_SPEED_LARGE),
                           bytesPerSec.value())) {
            return failOption(storage, QCURL_CURL_OPTION(CURLOPT_MAX_SEND_SPEED_LARGE).name);
        }
    }
    return true;
}

bool configureAuthOptions(CURL *handle,
                          const QCNetworkRequest &request,
                          RequestOptionStorage *storage)
{
    bool hasSensitiveHeader           = false;
    const bool hasAuthorizationHeader = isHeaderSet(request,
                                                    QCurl::httpheaders::kAuthorization.toLower());
    hasSensitiveHeader = hasAuthorizationHeader
                         || isHeaderSet(request, QCurl::httpheaders::kCookie.toLower())
                         || isHeaderSet(request, QCurl::httpheaders::kProxyAuthorization.toLower());

    if (const auto auth = request.httpAuth(); auth.has_value() && !hasAuthorizationHeader) {
        hasSensitiveHeader        = true;
        storage->httpAuthUser     = auth->userName().toUtf8();
        storage->httpAuthPassword = auth->password().toUtf8();
        if (!setStringOption(handle, QCURL_CURL_OPTION(CURLOPT_USERNAME), storage->httpAuthUser)) {
            return failOption(storage, QCURL_CURL_OPTION(CURLOPT_USERNAME).name);
        }
        if (!setStringOption(handle,
                             QCURL_CURL_OPTION(CURLOPT_PASSWORD),
                             storage->httpAuthPassword)) {
            return failOption(storage, QCURL_CURL_OPTION(CURLOPT_PASSWORD).name);
        }
        if (!setLongOption(handle,
                           QCURL_CURL_OPTION(CURLOPT_HTTPAUTH),
                           static_cast<long>(curlHttpAuth(auth->method())))) {
            return failOption(storage, QCURL_CURL_OPTION(CURLOPT_HTTPAUTH).name);
        }
        if (auth->allowUnrestrictedAuth() && request.followLocation()) {
            if (!setLongOption(handle,
                               QCURL_CURL_OPTION(CURLOPT_UNRESTRICTED_AUTH),
                               Internal::CurlOptions::kEnabled)) {
                return failOption(storage, QCURL_CURL_OPTION(CURLOPT_UNRESTRICTED_AUTH).name);
            }
        }
    }

    if (request.followLocation() && request.allowUnrestrictedSensitiveHeadersOnRedirect()
        && hasSensitiveHeader) {
        if (!setLongOption(handle,
                           QCURL_CURL_OPTION(CURLOPT_UNRESTRICTED_AUTH),
                           Internal::CurlOptions::kEnabled)) {
            return failOption(storage, QCURL_CURL_OPTION(CURLOPT_UNRESTRICTED_AUTH).name);
        }
    }
    return true;
}

} // namespace QCurl::Internal
