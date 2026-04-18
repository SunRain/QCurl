#include "CurlFeatureProbe.h"

#include <QCNetworkRequest.h>
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

    const CURLcode rc
        = curl_easy_setopt(easy,
                           CURLOPT_PINNEDPUBLICKEY,
                           "sha256//AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA=");
    curl_easy_cleanup(easy);
    return rc != CURLE_UNKNOWN_OPTION && rc != CURLE_NOT_BUILT_IN;
}

QJsonObject buildManifest()
{
    QCurl::CurlFeatureProbe &probe = QCurl::CurlFeatureProbe::instance();

    QCurl::QCNetworkRequest request;
    request.setAutoDecompressionEnabled(true);
    request.setAcceptedEncodings({QStringLiteral("gzip"), QStringLiteral("br")});

    QBuffer buffer;
    buffer.open(QIODevice::ReadWrite);
    request.setUploadDevice(&buffer);
    request.setAllowChunkedUploadForPost(true);

    QCurl::QCNetworkSslConfig ssl;
    ssl.setPinnedPublicKey(QStringLiteral("sha256//AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA="));

    const bool uploadDeviceApi = (request.uploadDevice() == &buffer);
    const bool chunkedApi = request.allowChunkedUploadForPost();
    const bool pinnedApi = (ssl.pinnedPublicKey()
                            == QStringLiteral("sha256//AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA="));
    const bool acceptEncodingApi = request.autoDecompressionEnabled()
                                   && request.acceptedEncodings().contains(QStringLiteral("gzip"));

    QJsonObject qcurl;
    qcurl.insert(QStringLiteral("acceptEncodingApi"), acceptEncodingApi);
    qcurl.insert(QStringLiteral("uploadDeviceApi"), uploadDeviceApi);
    qcurl.insert(QStringLiteral("chunkedUnknownSizePostApi"), chunkedApi);
    qcurl.insert(QStringLiteral("pinnedPublicKeyApi"), pinnedApi);

    QJsonObject libcurl;
    libcurl.insert(QStringLiteral("compiledVersionNum"), probe.compiledVersionNum());
    libcurl.insert(QStringLiteral("runtimeVersionNum"), probe.runtimeVersionNum());
    libcurl.insert(QStringLiteral("runtimeVersionString"), probe.runtimeVersionString());
    libcurl.insert(QStringLiteral("runtimeFeatures"), static_cast<qint64>(probe.runtimeFeatures()));
    libcurl.insert(QStringLiteral("runtimeSslBackend"), runtimeSslBackend());
    libcurl.insert(QStringLiteral("pinnedPublicKeyOption"), supportsPinnedPublicKey());

    QJsonObject tests;
    tests.insert(
        QStringLiteral("test_p1_accept_encoding.py"),
        QJsonObject{
            {QStringLiteral("enabled"), acceptEncodingApi},
            {QStringLiteral("reason"),
             acceptEncodingApi ? QStringLiteral("QCNetworkRequest accept-encoding APIs available")
                               : QStringLiteral("QCNetworkRequest accept-encoding APIs unavailable")},
        });
    tests.insert(
        QStringLiteral("test_p1_upload_seek_constraints.py"),
        QJsonObject{
            {QStringLiteral("enabled"), uploadDeviceApi},
            {QStringLiteral("reason"),
             uploadDeviceApi ? QStringLiteral("QCNetworkRequest uploadDevice API available")
                             : QStringLiteral("QCNetworkRequest uploadDevice API unavailable")},
        });
    tests.insert(
        QStringLiteral("test_p2_stream_upload_chunked_post.py"),
        QJsonObject{
            {QStringLiteral("enabled"), uploadDeviceApi && chunkedApi},
            {QStringLiteral("reason"),
             (uploadDeviceApi && chunkedApi)
                 ? QStringLiteral("unknown-size POST chunked upload API available")
                 : QStringLiteral("unknown-size POST chunked upload API unavailable")},
        });
    tests.insert(
        QStringLiteral("test_p2_tls_pinned_public_key.py"),
        QJsonObject{
            {QStringLiteral("enabled"), pinnedApi && supportsPinnedPublicKey()},
            {QStringLiteral("reason"),
             (pinnedApi && supportsPinnedPublicKey())
                 ? QStringLiteral("CURLOPT_PINNEDPUBLICKEY available at runtime")
                 : QStringLiteral("CURLOPT_PINNEDPUBLICKEY unavailable at runtime")},
        });

    QJsonObject root;
    root.insert(QStringLiteral("schema"), QStringLiteral("qcurl-lc/capabilities@v1"));
    root.insert(QStringLiteral("generatedAt"), QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
    root.insert(QStringLiteral("qcurl"), qcurl);
    root.insert(QStringLiteral("libcurl"), libcurl);
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
