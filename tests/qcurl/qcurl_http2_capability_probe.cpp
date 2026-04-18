#include "QCGlobal.h"

#include <QCoreApplication>

#include <cstdio>

#include <curl/curl.h>

namespace {

QString buildFailureReason()
{
    if (!QCurl::hasHttp2Support()) {
        return QStringLiteral("QCurl built without HTTP/2 support (QCURL_HAS_HTTP2=0)");
    }

    const curl_version_info_data *info = curl_version_info(CURLVERSION_NOW);
    if (!info) {
        return QStringLiteral("curl_version_info(CURLVERSION_NOW) returned null");
    }

    if ((info->features & CURL_VERSION_HTTP2) == 0) {
        return QStringLiteral(
            "libcurl runtime does not advertise HTTP/2 support (missing CURL_VERSION_HTTP2)");
    }

    return {};
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    const QString failureReason = buildFailureReason();
    if (!failureReason.isEmpty()) {
        std::fprintf(stderr, "%s\n", failureReason.toUtf8().constData());
        return 2;
    }

    return 0;
}
