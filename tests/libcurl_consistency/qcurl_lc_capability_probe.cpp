#include "CurlFeatureProbe.h"

#include <QBuffer>
#include <QCBlockingNetworkClient.h>
#include <QCBlockingNetworkResult.h>
#include <QCNetworkAccessManager.h>
#include <QCNetworkHttpVersion.h>
#include <QCNetworkMultipartBody.h>
#include <QCNetworkProxyConfig.h>
#include <QCNetworkReply.h>
#include <QCNetworkRequest.h>
#include <QCNetworkResumableDownloadJob.h>
#include <QCNetworkSslConfig.h>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStringList>

#include <cstdio>
#include <curl/curl.h>
#include <optional>

namespace {

QString runtimeSslBackend()
{
    const curl_version_info_data *info = curl_version_info(CURLVERSION_NOW);
    if (!info || !info->ssl_version) {
        return {};
    }
    return QString::fromUtf8(info->ssl_version);
}

bool supportsPinnedPublicKey()
{
    CURL *easy = curl_easy_init();
    if (!easy) {
        return false;
    }

    const CURLcode rc = curl_easy_setopt(easy,
                                         CURLOPT_PINNEDPUBLICKEY,
                                         "sha256//AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA=");
    curl_easy_cleanup(easy);
    return rc != CURLE_UNKNOWN_OPTION && rc != CURLE_NOT_BUILT_IN;
}

/**
 * @brief 探测 origin TLS 最低版本与 TLS 1.2/1.3 密码套件选项。
 * @return 三类选项都能被当前 libcurl 接受时返回 true。
 */
bool supportsOriginTlsPolicy()
{
    CURL *easy = curl_easy_init();
    if (!easy) {
        return false;
    }
    const CURLcode minRc    = curl_easy_setopt(easy, CURLOPT_SSLVERSION, CURL_SSLVERSION_TLSv1_2);
    const CURLcode cipherRc = curl_easy_setopt(easy,
                                               CURLOPT_SSL_CIPHER_LIST,
                                               "ECDHE-RSA-AES256-GCM-SHA384");
    const CURLcode tls13Rc = curl_easy_setopt(easy, CURLOPT_TLS13_CIPHERS, "TLS_AES_256_GCM_SHA384");
    curl_easy_cleanup(easy);
    return minRc == CURLE_OK && cipherRc == CURLE_OK && tls13Rc == CURLE_OK;
}

/**
 * @brief 探测 HTTPS proxy 的验证、CA、最低 TLS 与密码套件选项。
 * @return 当前 libcurl 能配置完整 proxy TLS 合同时返回 true。
 */
bool supportsProxyTlsPolicy()
{
    CURL *easy = curl_easy_init();
    if (!easy) {
        return false;
    }
    const CURLcode typeRc   = curl_easy_setopt(easy, CURLOPT_PROXYTYPE, CURLPROXY_HTTPS);
    const CURLcode verifyRc = curl_easy_setopt(easy, CURLOPT_PROXY_SSL_VERIFYPEER, 1L);
    const CURLcode hostRc   = curl_easy_setopt(easy, CURLOPT_PROXY_SSL_VERIFYHOST, 2L);
    const CURLcode minRc = curl_easy_setopt(easy, CURLOPT_PROXY_SSLVERSION, CURL_SSLVERSION_TLSv1_2);
    const CURLcode cipherRc = curl_easy_setopt(easy,
                                               CURLOPT_PROXY_SSL_CIPHER_LIST,
                                               "ECDHE-RSA-AES256-GCM-SHA384");
    const CURLcode tls13Rc  = curl_easy_setopt(easy,
                                               CURLOPT_PROXY_TLS13_CIPHERS,
                                               "TLS_AES_256_GCM_SHA384");
    curl_easy_cleanup(easy);
    return typeRc == CURLE_OK && verifyRc == CURLE_OK && hostRc == CURLE_OK && minRc == CURLE_OK
           && cipherRc == CURLE_OK && tls13Rc == CURLE_OK;
}

QJsonObject capabilityStatus(bool available,
                             const QString &availableText,
                             const QString &missingText,
                             const QString &attribution)
{
    return QJsonObject{
        {QStringLiteral("available"), available},
        {QStringLiteral("attribution"), attribution},
        {QStringLiteral("reason"), available ? availableText : missingText},
    };
}

QJsonObject buildManifest()
{
    QCurl::CurlFeatureProbe &probe = QCurl::CurlFeatureProbe::instance();

    QCurl::QCNetworkRequest request;
    request.setAutoDecompressionEnabled(true);
    request.setAcceptedEncodings({QStringLiteral("gzip"), QStringLiteral("br")});
    request.setRawHeader(QByteArrayLiteral("X-QCurl-Probe"), QByteArrayLiteral("1"));
    request.setHttpVersion(QCurl::QCNetworkHttpVersion::Http3);
    const bool rawRequestHeaderApi = request.rawHeader(QByteArrayLiteral("X-QCurl-Probe"))
                                     == QByteArrayLiteral("1");
    const bool http3Api            = request.httpVersion() == QCurl::QCNetworkHttpVersion::Http3;

    QCurl::QCNetworkProxyConfig socks5;
    socks5.setType(QCurl::QCNetworkProxyConfig::ProxyType::Socks5Hostname);
    socks5.setHostName(QStringLiteral("127.0.0.1"));
    socks5.setPort(1);
    const bool socks5HostnameApi = socks5.type()
                                       == QCurl::QCNetworkProxyConfig::ProxyType::Socks5Hostname
                                   && socks5.isValid();

    static_cast<void>(static_cast<QCurl::QCNetworkReply *(
                          QCurl::QCNetworkAccessManager::*)(const QCurl::QCNetworkRequest &,
                                                            QIODevice *,
                                                            std::optional<qint64>)>(
        &QCurl::QCNetworkAccessManager::post));
    static_cast<void>(
        static_cast<QCurl::QCBlockingNetworkResult (
            QCurl::QCBlockingNetworkClient::*)(const QCurl::QCNetworkRequest &,
                                               QIODevice *,
                                               std::optional<qint64>,
                                               const QCurl::QCBlockingRequestOptions &) const>(
            &QCurl::QCBlockingNetworkClient::post));
    QBuffer bodyProbe;
    bodyProbe.open(QIODevice::ReadOnly);
    auto multipartProbe
        = QCurl::QCNetworkMultipartBody::fromSingleFileDevice(&bodyProbe,
                                                              QStringLiteral("file"),
                                                              QStringLiteral("probe.bin"),
                                                              QStringLiteral(
                                                                  "application/octet-stream"),
                                                              qint64(0));
    QCurl::QCNetworkResumableDownloadJob *resumableJobTypeProbe = nullptr;
    Q_UNUSED(resumableJobTypeProbe);

    QCurl::QCNetworkSslConfig ssl;
    ssl.setPinnedPublicKey(QStringLiteral("sha256//AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA="));

    const bool rawBodyDeviceApi               = true;
    const bool blockingExtrasRawBodyDeviceApi = true;
    const bool unknownSizePostApi             = true;
    const bool singleFileMultipartBodyApi     = multipartProbe.has_value();
    const bool resumableDownloadJobApi        = true;
    const bool runtimeHasHttp2                = (probe.runtimeFeatures() & CURL_VERSION_HTTP2) != 0;
    const bool runtimeHasHttp3                = (probe.runtimeFeatures() & CURL_VERSION_HTTP3) != 0;
    const bool runtimeHasIpv6                 = (probe.runtimeFeatures() & CURL_VERSION_IPV6) != 0;
    const bool runtimeHasAltSvc  = (probe.runtimeFeatures() & CURL_VERSION_ALTSVC) != 0;
    const bool runtimeHasHsts    = (probe.runtimeFeatures() & CURL_VERSION_HSTS) != 0;
    const bool pinnedApi         = (ssl.pinnedPublicKey()
                                    == QStringLiteral(
                                        "sha256//AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA="));
    const bool pinnedRuntime     = supportsPinnedPublicKey();
    const bool acceptEncodingApi = request.autoDecompressionEnabled()
                                   && request.acceptedEncodings().contains(QStringLiteral("gzip"));
    request.setIpResolve(QCurl::QCNetworkIpResolve::Ipv4);
    const bool ipResolveApi = request.ipResolve() == QCurl::QCNetworkIpResolve::Ipv4;

    QCurl::QCNetworkSslConfig tlsPolicy;
    tlsPolicy.setClientCertPath(QStringLiteral("client.pem"));
    tlsPolicy.setClientKeyPath(QStringLiteral("client.key"));
    tlsPolicy.setMinTlsVersion(QCurl::QCNetworkTlsVersion::Tls1_2);
    tlsPolicy.setCipherList(QStringLiteral("ECDHE-RSA-AES256-GCM-SHA384"));
    tlsPolicy.setTls13Ciphers(QStringLiteral("TLS_AES_256_GCM_SHA384"));
    const bool tlsPolicyApi = !tlsPolicy.clientCertPath().isEmpty()
                              && tlsPolicy.minTlsVersion() == QCurl::QCNetworkTlsVersion::Tls1_2
                              && !tlsPolicy.cipherList().isEmpty()
                              && !tlsPolicy.tls13Ciphers().isEmpty();

    QCurl::QCNetworkProxyConfig::ProxyTlsConfig proxyTlsPolicy;
    proxyTlsPolicy.setMinTlsVersion(QCurl::QCNetworkTlsVersion::Tls1_2);
    proxyTlsPolicy.setCipherList(QStringLiteral("ECDHE-RSA-AES256-GCM-SHA384"));
    proxyTlsPolicy.setTls13Ciphers(QStringLiteral("TLS_AES_256_GCM_SHA384"));
    const bool proxyTlsPolicyApi      = proxyTlsPolicy.minTlsVersion()
                                            == QCurl::QCNetworkTlsVersion::Tls1_2
                                        && !proxyTlsPolicy.cipherList().isEmpty()
                                        && !proxyTlsPolicy.tls13Ciphers().isEmpty();
    const bool originTlsPolicyRuntime = supportsOriginTlsPolicy();
    const bool proxyTlsPolicyRuntime  = supportsProxyTlsPolicy();

    QJsonObject qcurl;
    qcurl.insert(QStringLiteral("acceptEncodingApi"), acceptEncodingApi);
    qcurl.insert(QStringLiteral("rawBodyDeviceApi"), rawBodyDeviceApi);
    qcurl.insert(QStringLiteral("blockingExtrasRawBodyDeviceApi"), blockingExtrasRawBodyDeviceApi);
    qcurl.insert(QStringLiteral("unknownSizeRawBodyPostApi"), unknownSizePostApi);
    qcurl.insert(QStringLiteral("singleFileMultipartBodyApi"), singleFileMultipartBodyApi);
    qcurl.insert(QStringLiteral("pinnedPublicKeyApi"), pinnedApi);
    qcurl.insert(QStringLiteral("rawRequestHeaderApi"), rawRequestHeaderApi);
    qcurl.insert(QStringLiteral("socks5HostnameApi"), socks5HostnameApi);
    qcurl.insert(QStringLiteral("http3Api"), http3Api);
    qcurl.insert(QStringLiteral("resumableDownloadJobApi"), resumableDownloadJobApi);
    qcurl.insert(QStringLiteral("ipResolveApi"), ipResolveApi);
    qcurl.insert(QStringLiteral("tlsPolicyApi"), tlsPolicyApi);
    qcurl.insert(QStringLiteral("proxyTlsPolicyApi"), proxyTlsPolicyApi);

    QJsonObject libcurl;
    libcurl.insert(QStringLiteral("compiledVersionNum"), probe.compiledVersionNum());
    libcurl.insert(QStringLiteral("runtimeVersionNum"), probe.runtimeVersionNum());
    libcurl.insert(QStringLiteral("runtimeVersionString"), probe.runtimeVersionString());
    libcurl.insert(QStringLiteral("runtimeFeatures"), static_cast<qint64>(probe.runtimeFeatures()));
    libcurl.insert(QStringLiteral("runtimeSslBackend"), runtimeSslBackend());
    libcurl.insert(QStringLiteral("pinnedPublicKeyOption"), pinnedRuntime);
    libcurl.insert(QStringLiteral("originTlsPolicyOptions"), originTlsPolicyRuntime);
    libcurl.insert(QStringLiteral("proxyTlsPolicyOptions"), proxyTlsPolicyRuntime);

    QJsonObject matrix;
    matrix.insert(QStringLiteral("http2"),
                  capabilityStatus(runtimeHasHttp2,
                                   QStringLiteral("runtime libcurl advertises CURL_VERSION_HTTP2"),
                                   QStringLiteral(
                                       "runtime libcurl does not advertise CURL_VERSION_HTTP2"),
                                   QStringLiteral("Fail")));
    matrix.insert(
        QStringLiteral("http3"),
        capabilityStatus(runtimeHasHttp3,
                         QStringLiteral("runtime libcurl advertises CURL_VERSION_HTTP3"),
                         QStringLiteral(
                             "HTTP/3 remains ext-only or unavailable without CURL_VERSION_HTTP3"),
                         QStringLiteral("Preview")));
    matrix
        .insert(QStringLiteral("websocket"),
                capabilityStatus(QCurl::hasWebSocketSupport(),
                                 QStringLiteral("QCURL_WEBSOCKET_SUPPORT is enabled at build time"),
                                 QStringLiteral(
                                     "WebSocket support is disabled or unavailable at build time"),
                                 QStringLiteral("Preview")));
    matrix.insert(QStringLiteral("hsts"),
                  capabilityStatus(runtimeHasHsts,
                                   QStringLiteral("runtime libcurl advertises CURL_VERSION_HSTS"),
                                   QStringLiteral(
                                       "HSTS cache is unavailable without CURL_VERSION_HSTS"),
                                   QStringLiteral("Warn")));
    matrix.insert(QStringLiteral("altSvc"),
                  capabilityStatus(runtimeHasAltSvc,
                                   QStringLiteral("runtime libcurl advertises CURL_VERSION_ALTSVC"),
                                   QStringLiteral(
                                       "Alt-Svc cache is unavailable without CURL_VERSION_ALTSVC"),
                                   QStringLiteral("Warn")));
    matrix.insert(QStringLiteral("tlsPinnedPublicKey"),
                  capabilityStatus(
                      pinnedApi && pinnedRuntime,
                      QStringLiteral(
                          "QCurl API and runtime libcurl pinned public key option are available"),
                      QStringLiteral(
                          "TLS pinned public key is unavailable or not built into runtime libcurl"),
                      QStringLiteral("Fail")));
    matrix.insert(QStringLiteral("proxyAndSocks"),
                  capabilityStatus(socks5HostnameApi,
                                   QStringLiteral("QCurl proxy API exposes SOCKS5Hostname"),
                                   QStringLiteral("QCurl proxy API does not expose SOCKS5Hostname"),
                                   QStringLiteral("Fail")));
    matrix.insert(QStringLiteral("rawObservability"),
                  capabilityStatus(rawRequestHeaderApi,
                                   QStringLiteral("QCurl raw request header API is available"),
                                   QStringLiteral("QCurl raw request header API is unavailable"),
                                   QStringLiteral("Fail")));
    matrix.insert(QStringLiteral("ipResolveIpv6"),
                  capabilityStatus(runtimeHasIpv6,
                                   QStringLiteral("runtime libcurl advertises CURL_VERSION_IPV6"),
                                   QStringLiteral("runtime libcurl does not advertise IPv6"),
                                   QStringLiteral("Fail")));
    matrix.insert(QStringLiteral("originTlsPolicy"),
                  capabilityStatus(tlsPolicyApi && originTlsPolicyRuntime,
                                   QStringLiteral(
                                       "QCurl and libcurl origin TLS policies available"),
                                   QStringLiteral(
                                       "origin TLS policy API or runtime option unavailable"),
                                   QStringLiteral("Fail")));
    matrix.insert(QStringLiteral("proxyTlsPolicy"),
                  capabilityStatus(proxyTlsPolicyApi && proxyTlsPolicyRuntime,
                                   QStringLiteral(
                                       "QCurl and libcurl HTTPS proxy TLS policies available"),
                                   QStringLiteral(
                                       "HTTPS proxy TLS API or runtime option unavailable"),
                                   QStringLiteral("Fail")));

    QJsonObject tests;
    tests.insert(QStringLiteral("test_p1_accept_encoding.py"),
                 QJsonObject{
                     {QStringLiteral("enabled"), acceptEncodingApi},
                     {QStringLiteral("reason"),
                      acceptEncodingApi
                          ? QStringLiteral("QCNetworkRequest accept-encoding APIs available")
                          : QStringLiteral("QCNetworkRequest accept-encoding APIs unavailable")},
                 });
    tests.insert(QStringLiteral("test_p1_upload_seek_constraints.py"),
                 QJsonObject{
                     {QStringLiteral("enabled"), rawBodyDeviceApi},
                     {QStringLiteral("reason"),
                      rawBodyDeviceApi
                          ? QStringLiteral("manager-level raw-body device API available")
                          : QStringLiteral("manager-level raw-body device API unavailable")},
                 });
    tests.insert(QStringLiteral("test_p2_stream_upload_chunked_post.py"),
                 QJsonObject{
                     {QStringLiteral("enabled"), rawBodyDeviceApi && unknownSizePostApi},
                     {QStringLiteral("reason"),
                      (rawBodyDeviceApi && unknownSizePostApi)
                          ? QStringLiteral("unknown-size POST raw-body API available")
                          : QStringLiteral("unknown-size POST raw-body API unavailable")},
                 });
    tests.insert(QStringLiteral("test_p2_tls_pinned_public_key.py"),
                 QJsonObject{
                     {QStringLiteral("enabled"), pinnedApi && pinnedRuntime},
                     {QStringLiteral("reason"),
                      (pinnedApi && pinnedRuntime)
                          ? QStringLiteral("CURLOPT_PINNEDPUBLICKEY available at runtime")
                          : QStringLiteral("CURLOPT_PINNEDPUBLICKEY unavailable at runtime")},
                 });
    tests.insert(QStringLiteral("test_p1_request_headers.py"),
                 QJsonObject{
                     {QStringLiteral("enabled"), rawRequestHeaderApi},
                     {QStringLiteral("reason"),
                      rawRequestHeaderApi
                          ? QStringLiteral("QCNetworkRequest raw header API available")
                          : QStringLiteral("QCNetworkRequest raw header API unavailable")},
                 });
    tests.insert(QStringLiteral("test_p1_socks_success.py"),
                 QJsonObject{
                     {QStringLiteral("enabled"), socks5HostnameApi},
                     {QStringLiteral("reason"),
                      socks5HostnameApi
                          ? QStringLiteral("SOCKS5/SOCKS5Hostname proxy APIs available")
                          : QStringLiteral("SOCKS5/SOCKS5Hostname proxy APIs unavailable")},
                 });
    tests.insert(QStringLiteral("test_p1_redirect_302_303_308.py"),
                 QJsonObject{
                     {QStringLiteral("enabled"), rawBodyDeviceApi},
                     {QStringLiteral("reason"),
                      rawBodyDeviceApi
                          ? QStringLiteral("seekable/non-seekable raw-body API available")
                          : QStringLiteral("seekable/non-seekable raw-body API unavailable")},
                 });
    tests.insert(QStringLiteral("test_p2_range_boundaries.py"),
                 QJsonObject{
                     {QStringLiteral("enabled"), resumableDownloadJobApi},
                     {QStringLiteral("reason"),
                      resumableDownloadJobApi
                          ? QStringLiteral("QCNetworkResumableDownloadJob API available")
                          : QStringLiteral("QCNetworkResumableDownloadJob API unavailable")},
                 });
    tests.insert(QStringLiteral("test_ext_http3_version_policy.py"),
                 QJsonObject{
                     {QStringLiteral("enabled"), http3Api},
                     {QStringLiteral("reason"),
                      http3Api ? QStringLiteral("HTTP/3 request policy API available")
                               : QStringLiteral("HTTP/3 request policy API unavailable")},
                 });
    tests.insert(QStringLiteral("test_ext_http3_success_h3.py"),
                 QJsonObject{
                     {QStringLiteral("enabled"), http3Api && runtimeHasHttp3},
                     {QStringLiteral("reason"),
                      (http3Api && runtimeHasHttp3)
                          ? QStringLiteral("HTTP/3 API and runtime support available")
                          : QStringLiteral("HTTP/3 runtime unavailable; default with-ext gate "
                                           "excludes H3 success file")},
                 });
    tests.insert(QStringLiteral("test_p2_tls_policy.py"),
                 QJsonObject{
                     {QStringLiteral("enabled"), tlsPolicyApi && originTlsPolicyRuntime},
                     {QStringLiteral("reason"),
                      (tlsPolicyApi && originTlsPolicyRuntime)
                          ? QStringLiteral("origin mTLS and TLS policy APIs/options available")
                          : QStringLiteral("origin TLS policy capability unavailable")},
                 });
    tests.insert(QStringLiteral("test_p2_ip_resolve_ipv4.py"),
                 QJsonObject{
                     {QStringLiteral("enabled"), ipResolveApi},
                     {QStringLiteral("reason"),
                      ipResolveApi ? QStringLiteral("request IP resolve API available")
                                   : QStringLiteral("request IP resolve API unavailable")},
                 });
    tests.insert(QStringLiteral("test_p2_ip_resolve_ipv6.py"),
                 QJsonObject{
                     {QStringLiteral("enabled"), ipResolveApi && runtimeHasIpv6},
                     {QStringLiteral("reason"),
                      (ipResolveApi && runtimeHasIpv6)
                          ? QStringLiteral("request IP resolve API and IPv6 runtime available")
                          : QStringLiteral("IPv6 request capability unavailable")},
                 });
    tests.insert(QStringLiteral("test_p2_https_proxy_tls.py"),
                 QJsonObject{
                     {QStringLiteral("enabled"), proxyTlsPolicyApi && proxyTlsPolicyRuntime},
                     {QStringLiteral("reason"),
                      (proxyTlsPolicyApi && proxyTlsPolicyRuntime)
                          ? QStringLiteral("HTTPS proxy TLS API and runtime options available")
                          : QStringLiteral("HTTPS proxy TLS capability unavailable")},
                 });

    QJsonObject root;
    root.insert(QStringLiteral("schema"), QStringLiteral("qcurl-lc/capabilities@v1"));
    root.insert(QStringLiteral("generatedAt"),
                QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
    root.insert(QStringLiteral("qcurl"), qcurl);
    root.insert(QStringLiteral("libcurl"), libcurl);
    root.insert(QStringLiteral("capabilityMatrix"), matrix);
    root.insert(QStringLiteral("tests"), tests);
    return root;
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    QString outputPath;
    const QStringList args = QCoreApplication::arguments();
    for (int i = 1; i < args.size(); ++i) {
        if (args.at(i) == QStringLiteral("--output") && i + 1 < args.size()) {
            outputPath = args.at(i + 1);
            ++i;
        } else if (args.at(i).startsWith(QStringLiteral("--output="))) {
            outputPath = args.at(i).mid(QStringLiteral("--output=").size());
        }
    }

    const QJsonDocument doc(buildManifest());
    const QByteArray payload = doc.toJson(QJsonDocument::Indented);
    if (outputPath.isEmpty()) {
        std::fwrite(payload.constData(), 1, static_cast<size_t>(payload.size()), stdout);
        return 0;
    }

    QFile file(outputPath);
    const QFileInfo info(file);
    QDir().mkpath(info.dir().absolutePath());
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        qCritical("failed to open capability manifest output");
        return 2;
    }
    file.write(payload);
    file.close();
    return 0;
}
