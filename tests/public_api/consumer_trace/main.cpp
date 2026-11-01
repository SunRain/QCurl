#include "trace_contract.h"

#include <QCNetworkAccessManager.h>
#include <QCNetworkMiddlewareExtras.h>
#include <QCoreApplication>
#include <QDebug>
#include <QFileInfo>
#include <QSet>

#include <curl/curl.h>
#include <dlfcn.h>
#include <link.h>

namespace {

bool loadedFrom(const void *address, const QString &expected)
{
    Dl_info info{};
    if (!dladdr(address, &info) || !info.dli_fname) {
        qCritical() << "无法核对实际加载库";
        return false;
    }
    const QString actual = QFileInfo(QString::fromLocal8Bit(info.dli_fname)).canonicalFilePath();
    qInfo() << "loaded=" << actual;
    if (actual != QFileInfo(expected).canonicalFilePath()) {
        qCritical() << "consumer 未加载 stage 中的库";
        return false;
    }
    return true;
}

int collectLoadedLibrary(dl_phdr_info *info, size_t, void *data)
{
    auto &paths = *static_cast<QSet<QString> *>(data);
    if (info->dlpi_name && *info->dlpi_name) {
        paths.insert(QFileInfo(QString::fromLocal8Bit(info->dlpi_name)).canonicalFilePath());
    }
    return 0;
}

} // namespace

bool verifyLoadedLibraries()
{
    QSet<QString> paths;
    dl_iterate_phdr(collectLoadedLibrary, &paths);
    const QString extras = QFileInfo(QString::fromUtf8(QCURL_EXPECTED_EXTRAS)).canonicalFilePath();
    qInfo() << "loaded extras=" << extras;
    if (extras.isEmpty() || !paths.contains(extras)) {
        qCritical() << "consumer 未加载 stage 中的 OtherExtras";
        return false;
    }
    return loadedFrom(&QCurl::QCNetworkAccessManager::staticMetaObject,
                      QString::fromUtf8(QCURL_EXPECTED_CORE));
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    if (app.arguments().size() < 3) {
        qCritical() << "需要 h1/h2、on/off 和可选 HTTP/2 URL";
        return 1;
    }
    const bool http2 = app.arguments().at(1) == QStringLiteral("h2");
    const bool ids   = app.arguments().at(2) == QStringLiteral("on");
    if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK) {
        return 2;
    }
    const auto *version = curl_version_info(CURLVERSION_NOW);
    qInfo() << "libcurl=" << version->version
            << "threadsafe=" << bool(version->features & CURL_VERSION_THREADSAFE)
            << "lib-ids=" << ids;
    if (curl_global_trace(ids ? "lib-ids" : "-lib-ids") != CURLE_OK) {
        curl_global_cleanup();
        return 3;
    }
    const QUrl base(app.arguments().value(3));
    bool passed = verifyLoadedLibraries();
    for (const auto policy : {QCurl::QCNetworkDiagnosticQueryPolicy::Redacted,
                              QCurl::QCNetworkDiagnosticQueryPolicy::Full}) {
        if (passed) {
            passed = runTraceRequests(http2, base, ids, policy);
        }
    }
    curl_global_cleanup();
    return passed ? 0 : 4;
}
