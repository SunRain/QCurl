#include "QCBlockingNetworkClient.h"

#include <QSharedData>

#include <limits>

namespace QCurl {

class QCBlockingNetworkClientOptionsData : public QSharedData
{
public:
    QCBlockingNetworkClient::ApplicationThreadPolicy applicationThreadPolicy =
        QCBlockingNetworkClient::ApplicationThreadPolicy::Reject;
};

class QCBlockingRequestOptionsData : public QSharedData
{
public:
    static constexpr qint64 DefaultMaxInMemoryBodyBytes = 16 * 1024 * 1024;

    qint64 maxInMemoryBodyBytes = DefaultMaxInMemoryBodyBytes;
    QCBlockingProgressCallback progressCallback = nullptr;
    void *progressCallbackUserData = nullptr;
    QCCookieSnapshot cookieSnapshot;
};

class QCTransferProgressData : public QSharedData
{
public:
    qint64 bytesReceived = 0;
    qint64 bytesTotal = -1;
    qint64 bytesSent = 0;
    qint64 uploadTotal = -1;
};

QCTransferProgress::QCTransferProgress()
    : d(new QCTransferProgressData)
{
}

QCTransferProgress::QCTransferProgress(qint64 bytesReceived,
                                       qint64 bytesTotal,
                                       qint64 bytesSent,
                                       qint64 uploadTotal)
    : d(new QCTransferProgressData)
{
    d->bytesReceived = bytesReceived;
    d->bytesTotal = bytesTotal;
    d->bytesSent = bytesSent;
    d->uploadTotal = uploadTotal;
}

QCTransferProgress::QCTransferProgress(const QCTransferProgress &other) = default;

QCTransferProgress::QCTransferProgress(QCTransferProgress &&other) noexcept = default;

QCTransferProgress::~QCTransferProgress() = default;

QCTransferProgress &QCTransferProgress::operator=(const QCTransferProgress &other) = default;

QCTransferProgress &QCTransferProgress::operator=(QCTransferProgress &&other) noexcept = default;

qint64 QCTransferProgress::bytesReceived() const noexcept
{
    return d->bytesReceived;
}

qint64 QCTransferProgress::bytesTotal() const noexcept
{
    return d->bytesTotal;
}

qint64 QCTransferProgress::bytesSent() const noexcept
{
    return d->bytesSent;
}

qint64 QCTransferProgress::uploadTotal() const noexcept
{
    return d->uploadTotal;
}

QCBlockingRequestOptions::QCBlockingRequestOptions()
    : d(new QCBlockingRequestOptionsData)
{
}

QCBlockingRequestOptions::QCBlockingRequestOptions(const QCBlockingRequestOptions &other) =
    default;

QCBlockingRequestOptions::QCBlockingRequestOptions(QCBlockingRequestOptions &&other) noexcept =
    default;

QCBlockingRequestOptions::~QCBlockingRequestOptions() = default;

QCBlockingRequestOptions &QCBlockingRequestOptions::operator=(
    const QCBlockingRequestOptions &other) = default;

QCBlockingRequestOptions &QCBlockingRequestOptions::operator=(
    QCBlockingRequestOptions &&other) noexcept = default;

qint64 QCBlockingRequestOptions::maxInMemoryBodyBytes() const noexcept
{
    return d->maxInMemoryBodyBytes;
}

void QCBlockingRequestOptions::setMaxInMemoryBodyBytes(qint64 bytes) noexcept
{
    if (bytes < 0) {
        d->maxInMemoryBodyBytes = std::numeric_limits<qint64>::max();
        return;
    }
    d->maxInMemoryBodyBytes = bytes;
}

QCBlockingProgressCallback QCBlockingRequestOptions::progressCallback() const noexcept
{
    return d->progressCallback;
}

void *QCBlockingRequestOptions::progressCallbackUserData() const noexcept
{
    return d->progressCallbackUserData;
}

void QCBlockingRequestOptions::setProgressCallback(QCBlockingProgressCallback callback,
                                                   void *userData) noexcept
{
    d->progressCallback = callback;
    d->progressCallbackUserData = callback ? userData : nullptr;
}

QCCookieSnapshot QCBlockingRequestOptions::cookieSnapshot() const
{
    return d->cookieSnapshot;
}

void QCBlockingRequestOptions::setCookieSnapshot(const QCCookieSnapshot &cookies)
{
    d->cookieSnapshot = cookies;
}

QCBlockingNetworkClient::Options::Options()
    : d(new QCBlockingNetworkClientOptionsData)
{
}

QCBlockingNetworkClient::Options::Options(const Options &other) = default;

QCBlockingNetworkClient::Options::Options(Options &&other) noexcept = default;

QCBlockingNetworkClient::Options::~Options() = default;

QCBlockingNetworkClient::Options &QCBlockingNetworkClient::Options::operator=(
    const Options &other) = default;

QCBlockingNetworkClient::Options &QCBlockingNetworkClient::Options::operator=(
    Options &&other) noexcept = default;

QCBlockingNetworkClient::ApplicationThreadPolicy
QCBlockingNetworkClient::Options::applicationThreadPolicy() const noexcept
{
    return d->applicationThreadPolicy;
}

void QCBlockingNetworkClient::Options::setApplicationThreadPolicy(
    ApplicationThreadPolicy policy) noexcept
{
    d->applicationThreadPolicy = policy;
}

} // namespace QCurl
