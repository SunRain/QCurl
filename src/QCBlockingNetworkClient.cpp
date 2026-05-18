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

bool validateBlockingDevice(QIODevice *body, QString *errorMessage)
{
    if (!body) {
        *errorMessage = QStringLiteral("Blocking Extras raw body device must not be null");
        return false;
    }
    if (body->thread() != QThread::currentThread()) {
        *errorMessage =
            QStringLiteral("Blocking Extras raw body device must belong to the calling thread");
        return false;
    }
    if (!body->isOpen() || !body->isReadable()) {
        *errorMessage = QStringLiteral("Blocking Extras raw body device must be open and readable");
        return false;
    }
    return true;
}

bool resolveBlockingDeviceSize(QIODevice *body,
                               std::optional<qint64> sizeBytes,
                               qint64 *resolvedSize,
                               bool *explicitSize,
                               QString *errorMessage)
{
    *explicitSize = sizeBytes.has_value();
    if (*explicitSize) {
        if (sizeBytes.value() < 0) {
            *errorMessage =
                QStringLiteral("Blocking Extras raw body explicit size must be non-negative");
            return false;
        }
        *resolvedSize = sizeBytes.value();
        return true;
    }

    if (body->isSequential()) {
        *errorMessage =
            QStringLiteral("Blocking Extras raw body device requires explicit size when sequential");
        return false;
    }

    *resolvedSize = body->size() - body->pos();
    if (*resolvedSize < 0) {
        *errorMessage =
            QStringLiteral("Blocking Extras raw body device size is smaller than position");
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

class QCBlockingNetworkClientData : public QSharedData
{
public:
    QCBlockingNetworkClient::Options options;
};

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
                                                         const QCCookieSnapshot &cookies) const
{
    if (applicationThreadRejected()) {
        return QCBlockingNetworkResult::failure(
            NetworkError::InvalidRequest,
            QStringLiteral("Blocking Extras requests require explicit application-thread opt-in"));
    }

    return Internal::performBlockingRequest(
        request, method, Internal::makeBlockingBytesBody(body), cookies);
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

    QString errorMessage;
    if (!validateBlockingDevice(body, &errorMessage)) {
        return invalidBlockingRequest(errorMessage);
    }

    qint64 resolvedSize = -1;
    bool explicitSize = false;
    if (!resolveBlockingDeviceSize(body, sizeBytes, &resolvedSize, &explicitSize, &errorMessage)) {
        return invalidBlockingRequest(errorMessage);
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
