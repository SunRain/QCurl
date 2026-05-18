#include <QCBlockingNetworkClient.h>
#include <QCBlockingNetworkResult.h>
#include <QCNetworkRequest.h>

#include <QCoreApplication>
#include <QBuffer>
#include <QUrl>

#include <optional>

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    QCurl::QCBlockingNetworkResult success = QCurl::QCBlockingNetworkResult::success(
        200,
        QByteArrayLiteral("ok"),
        {{QByteArrayLiteral("content-type"), QByteArrayLiteral("text/plain")}});
    if (!success.isSuccess() || success.statusCode() != 200
        || success.body() != QByteArrayLiteral("ok") || success.headers().size() != 1) {
        return 1;
    }

    QCurl::QCBlockingNetworkClient::Options options;
    options.setApplicationThreadPolicy(
        QCurl::QCBlockingNetworkClient::ApplicationThreadPolicy::AllowForCliOrTests);

    QCurl::QCBlockingNetworkClient client(options);
    QCurl::QCNetworkRequest request(QUrl(QStringLiteral("https://example.invalid")));
    const auto result = client.sendGet(request);

    QBuffer body;
    body.setData(QByteArrayLiteral("body"));
    if (!body.open(QIODevice::ReadOnly)) {
        return 5;
    }
    static_cast<void>(static_cast<QCurl::QCBlockingNetworkResult (QCurl::QCBlockingNetworkClient::*)(
        const QCurl::QCNetworkRequest &, QIODevice *, std::optional<qint64>) const>(
        &QCurl::QCBlockingNetworkClient::sendPost));
    static_cast<void>(static_cast<QCurl::QCBlockingNetworkResult (QCurl::QCBlockingNetworkClient::*)(
        const QCurl::QCNetworkRequest &, QIODevice *, std::optional<qint64>) const>(
        &QCurl::QCBlockingNetworkClient::sendPut));
    const auto uploadResult = client.sendPost(request, &body, qint64(4));

    if (result.isSuccess() || result.error() != QCurl::NetworkError::InvalidRequest
        || result.errorMessage().isEmpty() || result.errorMessage().contains(QStringLiteral("not wired"))) {
        return 2;
    }
    if (uploadResult.isSuccess() || uploadResult.error() != QCurl::NetworkError::InvalidRequest
        || uploadResult.errorMessage().isEmpty()) {
        return 6;
    }

    client.setOptions(QCurl::QCBlockingNetworkClient::Options{});
    if (client.options().applicationThreadPolicy()
        != QCurl::QCBlockingNetworkClient::ApplicationThreadPolicy::Reject) {
        return 3;
    }

    const auto rejected = client.sendGet(request);
    if (rejected.isSuccess()
        || !rejected.errorMessage().contains(QStringLiteral("application-thread opt-in"))) {
        return 4;
    }

    return 0;
}
