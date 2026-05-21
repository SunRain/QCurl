#include "QCBlockingNetworkClient.h"

#include "private/QCBlockingCurlAdapter_p.h"

#include <QCoreApplication>
#include <QIODevice>
#include <QSharedData>
#include <QThread>

#include <limits>
#include <optional>
#include <utility>

namespace QCurl {
namespace {

bool isApplicationThread()
{
    const auto *application = QCoreApplication::instance();
    return application != nullptr && QThread::currentThread() == application->thread();
}

QCBlockingNetworkResult invalidBlockingRequest(const QString &message)
{
    return QCBlockingNetworkResult::failure(NetworkError::InvalidRequest, message);
}

QCBlockingNetworkResult invalidInputDevice(const QString &message)
{
    return QCBlockingNetworkResult::failure(NetworkError::InputDeviceError, message);
}

bool validateBlockingDevice(QIODevice *body, QCBlockingNetworkResult *failure)
{
    if (!body) {
        *failure = invalidInputDevice(
            QStringLiteral("Blocking Extras raw body device must not be null"));
        return false;
    }
    if (body->thread() != QThread::currentThread()) {
        *failure = invalidInputDevice(
            QStringLiteral("Blocking Extras raw body device must belong to the calling thread"));
        return false;
    }
    if (!body->isOpen() || !body->isReadable()) {
        *failure = invalidInputDevice(
            QStringLiteral("Blocking Extras raw body device must be open and readable"));
        return false;
    }
    return true;
}

bool resolveBlockingDeviceSize(QIODevice *body,
                               std::optional<qint64> sizeBytes,
                               qint64 *resolvedSize,
                               bool *explicitSize,
                               QCBlockingNetworkResult *failure)
{
    *explicitSize = sizeBytes.has_value();
    if (*explicitSize) {
        if (sizeBytes.value() < 0) {
            *failure = invalidInputDevice(
                QStringLiteral("Blocking Extras raw body explicit size must be non-negative"));
            return false;
        }
        *resolvedSize = sizeBytes.value();
        return true;
    }

    if (body->isSequential()) {
        *failure = QCBlockingNetworkResult::failure(
            NetworkError::ReplayNotSupported,
            QStringLiteral("Blocking Extras raw body device requires explicit size when sequential"));
        return false;
    }

    *resolvedSize = body->size() - body->pos();
    if (*resolvedSize < 0) {
        *failure = invalidInputDevice(
            QStringLiteral("Blocking Extras raw body device size is smaller than position"));
        return false;
    }
    return true;
}

} // namespace

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
};

class QCBlockingNetworkClientData : public QSharedData
{
public:
    QCBlockingNetworkClient::Options options;
};

QCTransferProgress::QCTransferProgress(qint64 bytesReceived,
                                       qint64 bytesTotal,
                                       qint64 bytesSent,
                                       qint64 uploadTotal)
    : m_bytesReceived(bytesReceived)
    , m_bytesTotal(bytesTotal)
    , m_bytesSent(bytesSent)
    , m_uploadTotal(uploadTotal)
{
}

qint64 QCTransferProgress::bytesReceived() const noexcept
{
    return m_bytesReceived;
}

qint64 QCTransferProgress::bytesTotal() const noexcept
{
    return m_bytesTotal;
}

qint64 QCTransferProgress::bytesSent() const noexcept
{
    return m_bytesSent;
}

qint64 QCTransferProgress::uploadTotal() const noexcept
{
    return m_uploadTotal;
}

QCBlockingRequestOptions::QCBlockingRequestOptions()
    : d(new QCBlockingRequestOptionsData)
{
}

QCBlockingRequestOptions::QCBlockingRequestOptions(const QCBlockingRequestOptions &other) = default;

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

QCBlockingNetworkClient::QCBlockingNetworkClient()
    : d(new QCBlockingNetworkClientData)
{
}

QCBlockingNetworkClient::QCBlockingNetworkClient(Options options)
    : d(new QCBlockingNetworkClientData)
{
    d->options = std::move(options);
}

QCBlockingNetworkClient::QCBlockingNetworkClient(const QCBlockingNetworkClient &other) = default;

QCBlockingNetworkClient::QCBlockingNetworkClient(QCBlockingNetworkClient &&other) noexcept = default;

QCBlockingNetworkClient::~QCBlockingNetworkClient() = default;

QCBlockingNetworkClient &QCBlockingNetworkClient::operator=(
    const QCBlockingNetworkClient &other) = default;

QCBlockingNetworkClient &QCBlockingNetworkClient::operator=(
    QCBlockingNetworkClient &&other) noexcept = default;

QCBlockingNetworkClient::Options QCBlockingNetworkClient::options() const
{
    return d->options;
}

void QCBlockingNetworkClient::setOptions(const Options &options)
{
    d->options = options;
}

QCBlockingNetworkResult QCBlockingNetworkClient::sendGet(const QCNetworkRequest &request) const
{
    return perform(request, HttpMethod::Get, QByteArray());
}

QCBlockingNetworkResult QCBlockingNetworkClient::get(
    const QCNetworkRequest &request,
    const QCBlockingRequestOptions &requestOptions) const
{
    return perform(request, HttpMethod::Get, QByteArray(), QCCookieSnapshot(), requestOptions);
}

QCBlockingNetworkResult QCBlockingNetworkClient::head(
    const QCNetworkRequest &request,
    const QCBlockingRequestOptions &requestOptions) const
{
    return perform(request, HttpMethod::Head, QByteArray(), QCCookieSnapshot(), requestOptions);
}

QCBlockingNetworkResult QCBlockingNetworkClient::send(
    const QCNetworkRequest &request,
    HttpMethod method,
    const QByteArray &body,
    const QCBlockingRequestOptions &requestOptions) const
{
    return perform(request, method, body, QCCookieSnapshot(), requestOptions);
}

QCBlockingNetworkResult QCBlockingNetworkClient::downloadToDevice(
    const QCNetworkRequest &request,
    QIODevice *output,
    const QCBlockingRequestOptions &requestOptions) const
{
    if (applicationThreadRejected()) {
        return invalidBlockingRequest(
            QStringLiteral("Blocking Extras requests require explicit application-thread opt-in"));
    }
    if (!output) {
        return QCBlockingNetworkResult::failure(
            NetworkError::OutputDeviceError,
            QStringLiteral("Blocking Extras output device must not be null"));
    }
    if (output->thread() != QThread::currentThread()) {
        return QCBlockingNetworkResult::failure(
            NetworkError::OutputDeviceError,
            QStringLiteral("Blocking Extras output device must belong to the calling thread"));
    }
    if (!output->isOpen() || !output->isWritable()) {
        return QCBlockingNetworkResult::failure(
            NetworkError::OutputDeviceError,
            QStringLiteral("Blocking Extras output device must be open and writable"));
    }

    return Internal::performBlockingDownloadToDevice(request, output, requestOptions);
}

QCBlockingNetworkResult QCBlockingNetworkClient::sendGet(const QCNetworkRequest &request,
                                                         const QCCookieSnapshot &cookies) const
{
    return perform(request, HttpMethod::Get, QByteArray(), cookies);
}

QCBlockingNetworkResult QCBlockingNetworkClient::sendPost(const QCNetworkRequest &request,
                                                          const QByteArray &body) const
{
    return perform(request, HttpMethod::Post, body);
}

QCBlockingNetworkResult QCBlockingNetworkClient::sendPost(const QCNetworkRequest &request,
                                                          const QByteArray &body,
                                                          const QCCookieSnapshot &cookies) const
{
    return perform(request, HttpMethod::Post, body, cookies);
}

QCBlockingNetworkResult QCBlockingNetworkClient::sendPost(
    const QCNetworkRequest &request,
    QIODevice *body,
    std::optional<qint64> sizeBytes) const
{
    return perform(request, HttpMethod::Post, body, sizeBytes);
}

QCBlockingNetworkResult QCBlockingNetworkClient::sendPut(const QCNetworkRequest &request,
                                                         const QByteArray &body) const
{
    return perform(request, HttpMethod::Put, body);
}

QCBlockingNetworkResult QCBlockingNetworkClient::sendPut(
    const QCNetworkRequest &request,
    QIODevice *body,
    std::optional<qint64> sizeBytes) const
{
    return perform(request, HttpMethod::Put, body, sizeBytes);
}

QCBlockingNetworkResult QCBlockingNetworkClient::perform(const QCNetworkRequest &request,
                                                         HttpMethod method,
                                                         const QByteArray &body,
                                                         const QCCookieSnapshot &cookies,
                                                         const QCBlockingRequestOptions
                                                             &requestOptions) const
{
    if (applicationThreadRejected()) {
        return QCBlockingNetworkResult::failure(
            NetworkError::InvalidRequest,
            QStringLiteral("Blocking Extras requests require explicit application-thread opt-in"));
    }

    return Internal::performBlockingRequest(
        request, method, Internal::makeBlockingBytesBody(body), cookies, requestOptions);
}

QCBlockingNetworkResult QCBlockingNetworkClient::perform(
    const QCNetworkRequest &request,
    HttpMethod method,
    QIODevice *body,
    std::optional<qint64> sizeBytes) const
{
    if (applicationThreadRejected()) {
        return invalidBlockingRequest(
            QStringLiteral("Blocking Extras requests require explicit application-thread opt-in"));
    }

    QCBlockingNetworkResult failure;
    if (!validateBlockingDevice(body, &failure)) {
        return failure;
    }

    qint64 resolvedSize = -1;
    bool explicitSize = false;
    if (!resolveBlockingDeviceSize(body, sizeBytes, &resolvedSize, &explicitSize, &failure)) {
        return failure;
    }

    return Internal::performBlockingRequest(
        request, method, Internal::makeBlockingDeviceBody(body, resolvedSize, explicitSize));
}

bool QCBlockingNetworkClient::applicationThreadRejected() const
{
    return d->options.applicationThreadPolicy() == ApplicationThreadPolicy::Reject
        && isApplicationThread();
}

} // namespace QCurl
