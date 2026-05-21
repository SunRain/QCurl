#include "CurlFeatureProbe.h"

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

#include <QBuffer>
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
        &QCurl::QCNetworkAccessManager::sendPost));
    static_cast<void>(
        static_cast<QCurl::QCBlockingNetworkResult (QCurl::QCBlockingNetworkClient::*)(
            const QCurl::QCNetworkRequest &, QIODevice *, std::optional<qint64>) const>(
            &QCurl::QCBlockingNetworkClient::sendPost));
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
    const bool runtimeHasAltSvc  = (probe.runtimeFeatures() & CURL_VERSION_ALTSVC) != 0;
    const bool runtimeHasHsts    = (probe.runtimeFeatures() & CURL_VERSION_HSTS) != 0;
    const bool pinnedApi         = (ssl.pinnedPublicKey()
                                    == QStringLiteral(
                                        "sha256//AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA="));
    const bool pinnedRuntime     = supportsPinnedPublicKey();
    const bool acceptEncodingApi = request.autoDecompressionEnabled()
                                   && request.acceptedEncodings().contains(QStringLiteral("gzip"));

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

    QJsonObject libcurl;
    libcurl.insert(QStringLiteral("compiledVersionNum"), probe.compiledVersionNum());
    libcurl.insert(QStringLiteral("runtimeVersionNum"), probe.runtimeVersionNum());
    libcurl.insert(QStringLiteral("runtimeVersionString"), probe.runtimeVersionString());
    libcurl.insert(QStringLiteral("runtimeFeatures"), static_cast<qint64>(probe.runtimeFeatures()));
    libcurl.insert(QStringLiteral("runtimeSslBackend"), runtimeSslBackend());
    libcurl.insert(QStringLiteral("pinnedPublicKeyOption"), pinnedRuntime);

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
