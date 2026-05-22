#include <QCBlockingNetworkClient.h>
#include <QCBlockingNetworkResult.h>
#include <QCNetworkRequest.h>

#include <QCoreApplication>
#include <QBuffer>
#include <QFile>
#include <QUrl>

#include <optional>

namespace {

struct ProgressProbe
{
    int calls = 0;
};

bool recordProgress(const QCurl::QCTransferProgress &, void *userData)
{
    auto *probe = static_cast<ProgressProbe *>(userData);
    if (!probe) {
        return false;
    }

    ++probe->calls;
    return true;
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    QCurl::QCBlockingNetworkResult success = QCurl::QCBlockingNetworkResult::success(
        200,
        QByteArrayLiteral("ok"),
        {{QByteArrayLiteral("content-type"), QByteArrayLiteral("text/plain")}});
    if (!success.isSuccess() || success.statusCode() != 200
        || success.body() != QByteArrayLiteral("ok") || success.headers().size() != 1
        || success.rawHeaders().value(QByteArrayLiteral("content-type"))
            != QByteArrayLiteral("text/plain")
        || success.rawHeaderList().size() != 1
        || success.rawHeaderList().constFirst().first != QByteArrayLiteral("content-type")
        || success.bytesReceived() != 2) {
        return 1;
    }

    success.setDiagnosticCurlCode(7);
    if (success.diagnosticCurlCode() != 7) {
        return 7;
    }

    QCurl::QCBlockingRequestOptions requestOptions;
    requestOptions.setMaxInMemoryBodyBytes(4096);
    if (requestOptions.maxInMemoryBodyBytes() != 4096) {
        return 8;
    }
    ProgressProbe probe;
    requestOptions.setProgressCallback(recordProgress, &probe);
    if (requestOptions.progressCallback() == nullptr
        || requestOptions.progressCallbackUserData() != &probe) {
        return 12;
    }

    QCurl::QCTransferProgress progress(10, 100, 4, 16);
    if (progress.bytesReceived() != 10 || progress.bytesTotal() != 100
        || progress.bytesSent() != 4 || progress.uploadTotal() != 16) {
        return 9;
    }

    QCurl::QCBlockingNetworkClient::Options options;
    options.setApplicationThreadPolicy(
        QCurl::QCBlockingNetworkClient::ApplicationThreadPolicy::AllowForCliOrTests);

    QCurl::QCBlockingNetworkClient client(options);
    QCurl::QCNetworkRequest request(QUrl(QStringLiteral("https://example.invalid")));
    const auto result = client.get(request, requestOptions);
    const auto deleteResult = client.deleteResource(request, requestOptions);
    const auto postResult =
        client.post(request, QByteArrayLiteral("payload"), requestOptions);
    const auto putResult =
        client.put(request, QByteArrayLiteral("payload"), requestOptions);
    const auto patchResult =
        client.patch(request, QByteArrayLiteral("payload"), requestOptions);
    const auto customDeleteResult = client.sendCustomRequest(
        request,
        QByteArrayLiteral("DELETE"),
        QByteArrayLiteral("payload"),
        requestOptions);

    QBuffer body;
    body.setData(QByteArrayLiteral("body"));
    if (!body.open(QIODevice::ReadOnly)) {
        return 5;
    }
    static_cast<void>(static_cast<QCurl::QCBlockingNetworkResult (QCurl::QCBlockingNetworkClient::*)(
        const QCurl::QCNetworkRequest &,
        QIODevice *,
        std::optional<qint64>,
        const QCurl::QCBlockingRequestOptions &) const>(
        &QCurl::QCBlockingNetworkClient::post));
    static_cast<void>(static_cast<QCurl::QCBlockingNetworkResult (QCurl::QCBlockingNetworkClient::*)(
        const QCurl::QCNetworkRequest &,
        QIODevice *,
        std::optional<qint64>,
        const QCurl::QCBlockingRequestOptions &) const>(
        &QCurl::QCBlockingNetworkClient::put));
    const auto uploadResult = client.post(request, &body, qint64(4));

    QBuffer output;
    if (!output.open(QIODevice::WriteOnly)) {
        return 10;
    }
    const auto downloadResult = client.downloadToDevice(request, &output, requestOptions);

    if (result.isSuccess() || result.error() != QCurl::NetworkError::InvalidRequest
        || result.errorMessage().isEmpty() || result.errorMessage().contains(QStringLiteral("not wired"))) {
        return 2;
    }
    if (deleteResult.isSuccess() || postResult.isSuccess() || putResult.isSuccess()
        || patchResult.isSuccess() || customDeleteResult.isSuccess()) {
        return 13;
    }
    if (uploadResult.isSuccess() || uploadResult.errorMessage().isEmpty()) {
        return 6;
    }
    if (downloadResult.isSuccess() || downloadResult.errorMessage().isEmpty()) {
        return 11;
    }

    client.setOptions(QCurl::QCBlockingNetworkClient::Options{});
    if (client.options().applicationThreadPolicy()
        != QCurl::QCBlockingNetworkClient::ApplicationThreadPolicy::Reject) {
        return 3;
    }

    const auto rejected = client.get(request);
    if (rejected.isSuccess()
        || !rejected.errorMessage().contains(QStringLiteral("application-thread opt-in"))) {
        return 4;
    }

    return 0;
}
