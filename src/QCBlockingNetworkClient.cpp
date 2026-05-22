#include "QCBlockingNetworkClient.h"

#include "private/QCBlockingCurlAdapter_p.h"

#include <QCoreApplication>
#include <QIODevice>
#include <QSharedData>
#include <QThread>

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

bool isHttpTokenSeparator(char ch)
{
    switch (ch) {
        case '(':
        case ')':
        case '<':
        case '>':
        case '@':
        case ',':
        case ';':
        case ':':
        case '\\':
        case '"':
        case '/':
        case '[':
        case ']':
        case '?':
        case '=':
        case '{':
        case '}':
            return true;
        default:
            return false;
    }
}

bool isValidHttpToken(QByteArrayView method)
{
    if (method.isEmpty()) {
        return false;
    }

    for (char ch : method) {
        const auto byte = static_cast<unsigned char>(ch);
        if (byte <= 0x20 || byte >= 0x7f || isHttpTokenSeparator(ch)) {
            return false;
        }
    }

    return true;
}

QByteArray normalizedHttpMethodToken(QByteArrayView method)
{
    return method.toByteArray().toUpper();
}

} // namespace

class QCBlockingNetworkClientData : public QSharedData
{
public:
    QCBlockingNetworkClient::Options options;
};

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

QCBlockingNetworkResult QCBlockingNetworkClient::get(
    const QCNetworkRequest &request,
    const QCBlockingRequestOptions &requestOptions) const
{
    return perform(request, HttpMethod::Get, QByteArray(), requestOptions);
}

QCBlockingNetworkResult QCBlockingNetworkClient::head(
    const QCNetworkRequest &request,
    const QCBlockingRequestOptions &requestOptions) const
{
    return perform(request, HttpMethod::Head, QByteArray(), requestOptions);
}

QCBlockingNetworkResult QCBlockingNetworkClient::deleteResource(
    const QCNetworkRequest &request,
    const QCBlockingRequestOptions &requestOptions) const
{
    return perform(request, HttpMethod::Delete, QByteArray(), requestOptions);
}

QCBlockingNetworkResult QCBlockingNetworkClient::post(
    const QCNetworkRequest &request,
    const QByteArray &body,
    const QCBlockingRequestOptions &requestOptions) const
{
    return perform(request, HttpMethod::Post, body, requestOptions);
}

QCBlockingNetworkResult QCBlockingNetworkClient::put(
    const QCNetworkRequest &request,
    const QByteArray &body,
    const QCBlockingRequestOptions &requestOptions) const
{
    return perform(request, HttpMethod::Put, body, requestOptions);
}

QCBlockingNetworkResult QCBlockingNetworkClient::patch(
    const QCNetworkRequest &request,
    const QByteArray &body,
    const QCBlockingRequestOptions &requestOptions) const
{
    return perform(request, HttpMethod::Patch, body, requestOptions);
}

QCBlockingNetworkResult QCBlockingNetworkClient::post(
    const QCNetworkRequest &request,
    QIODevice *body,
    std::optional<qint64> sizeBytes,
    const QCBlockingRequestOptions &requestOptions) const
{
    return perform(request, HttpMethod::Post, body, sizeBytes, requestOptions);
}

QCBlockingNetworkResult QCBlockingNetworkClient::put(
    const QCNetworkRequest &request,
    QIODevice *body,
    std::optional<qint64> sizeBytes,
    const QCBlockingRequestOptions &requestOptions) const
{
    return perform(request, HttpMethod::Put, body, sizeBytes, requestOptions);
}

QCBlockingNetworkResult QCBlockingNetworkClient::sendCustomRequest(
    const QCNetworkRequest &request,
    QByteArrayView method,
    const QCBlockingRequestOptions &requestOptions) const
{
    return performCustom(request, method, QByteArray(), requestOptions);
}

QCBlockingNetworkResult QCBlockingNetworkClient::sendCustomRequest(
    const QCNetworkRequest &request,
    QByteArrayView method,
    const QByteArray &body,
    const QCBlockingRequestOptions &requestOptions) const
{
    return performCustom(request, method, body, requestOptions);
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

    return Internal::performBlockingDownloadToDevice(request,
                                                     HttpMethod::Get,
                                                     Internal::makeBlockingBytesBody(QByteArray()),
                                                     output,
                                                     requestOptions);
}

QCBlockingNetworkResult QCBlockingNetworkClient::perform(const QCNetworkRequest &request,
                                                         HttpMethod method,
                                                         const QByteArray &body,
                                                         const QCBlockingRequestOptions
                                                             &requestOptions) const
{
    if (applicationThreadRejected()) {
        return QCBlockingNetworkResult::failure(
            NetworkError::InvalidRequest,
            QStringLiteral("Blocking Extras requests require explicit application-thread opt-in"));
    }

    return Internal::performBlockingRequest(
        request,
        method,
        Internal::makeBlockingBytesBody(body),
        requestOptions.cookieSnapshot(),
        requestOptions);
}

QCBlockingNetworkResult QCBlockingNetworkClient::perform(
    const QCNetworkRequest &request,
    HttpMethod method,
    QIODevice *body,
    std::optional<qint64> sizeBytes,
    const QCBlockingRequestOptions &requestOptions) const
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

    return Internal::performBlockingRequest(request,
                                            method,
                                            Internal::makeBlockingDeviceBody(body,
                                                                            resolvedSize,
                                                                            explicitSize),
                                            requestOptions.cookieSnapshot(),
                                            requestOptions);
}

QCBlockingNetworkResult QCBlockingNetworkClient::performCustom(
    const QCNetworkRequest &request,
    QByteArrayView method,
    const QByteArray &body,
    const QCBlockingRequestOptions &requestOptions) const
{
    if (applicationThreadRejected()) {
        return invalidBlockingRequest(
            QStringLiteral("Blocking Extras requests require explicit application-thread opt-in"));
    }
    if (!isValidHttpToken(method)) {
        return invalidBlockingRequest(
            QStringLiteral("Blocking Extras custom HTTP method token is invalid"));
    }

    return Internal::performBlockingCustomRequest(
        request,
        normalizedHttpMethodToken(method),
        Internal::makeBlockingBytesBody(body),
        requestOptions.cookieSnapshot(),
        requestOptions);
}

bool QCBlockingNetworkClient::applicationThreadRejected() const
{
    return d->options.applicationThreadPolicy() == ApplicationThreadPolicy::Reject
        && isApplicationThread();
}

} // namespace QCurl
