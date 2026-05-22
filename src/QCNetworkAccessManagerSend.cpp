#include "QCNetworkAccessManager.h"
#include "QCNetworkAccessManager_p.h"
#include "QCNetworkBody.h"
#include "QCNetworkMiddleware.h"
#include "QCNetworkReply.h"
#include "QCNetworkRequest.h"
#include "QCNetworkRequestScheduler.h"
#include "private/QCRequestPipeline_p.h"
#include "private/QCThreading_p.h"

#include <QDebug>
#include <QIODevice>
#include <QThread>

namespace {

bool hasContentTypeHeader(const QCurl::QCNetworkRequest &request)
{
    const QList<QByteArray> headerNames = request.rawHeaderList();
    for (const auto &name : headerNames) {
        if (name.trimmed().toLower() == QByteArrayLiteral("content-type")) {
            return true;
        }
    }
    return false;
}

QCurl::QCNetworkRequest requestWithBodyContentType(const QCurl::QCNetworkRequest &request,
                                                   const QCurl::QCNetworkBody &body)
{
    if (body.contentType().isEmpty() || hasContentTypeHeader(request)) {
        return request;
    }

    QCurl::QCNetworkRequest prepared(request);
    prepared.setRawHeader(QByteArrayLiteral("Content-Type"), body.contentType());
    return prepared;
}

QString rawBodyOwnerThreadErrorMessage(const char *apiName)
{
    return QStringLiteral("%1: manager-level raw-body QIODevice overload 必须在 owner 线程调用")
        .arg(QString::fromUtf8(apiName));
}

QString sendOwnerThreadErrorMessage(const char *apiName)
{
    return QStringLiteral(
               "%1: Core 异步发送 API 必须在 manager owner 线程调用；跨线程调用请显式排队到 "
               "owner 线程，或使用 Blocking Extras")
        .arg(QString::fromUtf8(apiName));
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

QString invalidCustomMethodMessage(QByteArrayView method)
{
    return QStringLiteral("QCNetworkAccessManager::sendCustomRequest: HTTP method token 无效：%1")
        .arg(QString::fromUtf8(method.toByteArray()));
}

} // namespace

namespace QCurl {

QCNetworkReply *QCNetworkAccessManagerPrivate::dispatchManagedSendRequest(
    const QCNetworkRequest &request,
    HttpMethod method,
    const Internal::RequestBody &requestBodySource,
    const QByteArray &body,
    const char *apiName)
{
    return dispatchSendRequest(request,
                               method,
                               requestBodySource,
                               body,
                               apiName,
                               [this, request, method, requestBodySource, body]() {
                                   const auto middlewaresSnapshot = q_func()->middlewares();
                                   const QCNetworkRequest modifiedRequest
                                       = prepareManagedRequest(request, middlewaresSnapshot);
                                   return createManagedReply(modifiedRequest,
                                                             method,
                                                             requestBodySource,
                                                             body,
                                                             middlewaresSnapshot);
                               });
}

QCNetworkReply *QCNetworkAccessManagerPrivate::dispatchSendRequest(
    const QCNetworkRequest &request,
    HttpMethod method,
    const Internal::RequestBody &requestBodySource,
    const QByteArray &body,
    const char *apiName,
    const ReplyFactory &impl)
{
    if (QThread::currentThread() != q_func()->thread()) {
        return createInvalidRequestReply(request,
                                         method,
                                         sendOwnerThreadErrorMessage(apiName),
                                         nullptr);
    }

    if (!Internal::hasEventDispatcher(q_func()->thread())) {
        return createNoEventLoopErrorReply(request,
                                           method,
                                           requestBodySource,
                                           body,
                                           q_func(),
                                           apiName);
    }

    return impl();
}

QCNetworkReply *QCNetworkAccessManager::sendHead(const QCNetworkRequest &request)
{
    return d_func()->dispatchManagedSendRequest(request,
                                                HttpMethod::Head,
                                                Internal::makeEmptyRequestBody(),
                                                QByteArray(),
                                                "QCNetworkAccessManager::sendHead");
}

QCNetworkReply *QCNetworkAccessManager::sendGet(const QCNetworkRequest &request)
{
    return d_func()->dispatchManagedSendRequest(request,
                                                HttpMethod::Get,
                                                Internal::makeEmptyRequestBody(),
                                                QByteArray(),
                                                "QCNetworkAccessManager::sendGet");
}

QCNetworkReply *QCNetworkAccessManager::sendPost(const QCNetworkRequest &request,
                                                 const QByteArray &data)
{
    return d_func()->dispatchManagedSendRequest(request,
                                                HttpMethod::Post,
                                                Internal::makeInlineRequestBody(data),
                                                data,
                                                "QCNetworkAccessManager::sendPost");
}

QCNetworkReply *QCNetworkAccessManager::sendPost(const QCNetworkRequest &request,
                                                 const QCNetworkBody &body)
{
    return sendPost(requestWithBodyContentType(request, body), body.data());
}

QCNetworkReply *QCNetworkAccessManager::sendPost(const QCNetworkRequest &request,
                                                 QIODevice *device,
                                                 std::optional<qint64> sizeBytes)
{
    constexpr const char *apiName = "QCNetworkAccessManager::sendPost";
    if (QThread::currentThread() != thread()) {
        return d_func()->createInvalidRequestReply(request,
                                                   HttpMethod::Post,
                                                   rawBodyOwnerThreadErrorMessage(apiName),
                                                   nullptr);
    }

    return d_func()->dispatchManagedSendRequest(request,
                                                HttpMethod::Post,
                                                Internal::makeDeviceRequestBody(device,
                                                                                sizeBytes,
                                                                                true),
                                                QByteArray(),
                                                apiName);
}

QCNetworkReply *QCNetworkAccessManager::sendPut(const QCNetworkRequest &request,
                                                const QByteArray &data)
{
    return d_func()->dispatchManagedSendRequest(request,
                                                HttpMethod::Put,
                                                Internal::makeInlineRequestBody(data),
                                                data,
                                                "QCNetworkAccessManager::sendPut");
}

QCNetworkReply *QCNetworkAccessManager::sendPut(const QCNetworkRequest &request,
                                                const QCNetworkBody &body)
{
    return sendPut(requestWithBodyContentType(request, body), body.data());
}

QCNetworkReply *QCNetworkAccessManager::sendPut(const QCNetworkRequest &request,
                                                QIODevice *device,
                                                std::optional<qint64> sizeBytes)
{
    constexpr const char *apiName = "QCNetworkAccessManager::sendPut";
    if (QThread::currentThread() != thread()) {
        return d_func()->createInvalidRequestReply(request,
                                                   HttpMethod::Put,
                                                   rawBodyOwnerThreadErrorMessage(apiName),
                                                   nullptr);
    }

    return d_func()->dispatchManagedSendRequest(request,
                                                HttpMethod::Put,
                                                Internal::makeDeviceRequestBody(device,
                                                                                sizeBytes,
                                                                                false),
                                                QByteArray(),
                                                apiName);
}

QCNetworkReply *QCNetworkAccessManager::deleteResource(const QCNetworkRequest &request)
{
    return d_func()->dispatchManagedSendRequest(request,
                                                HttpMethod::Delete,
                                                Internal::makeEmptyRequestBody(),
                                                QByteArray(),
                                                "QCNetworkAccessManager::deleteResource");
}

QCNetworkReply *QCNetworkAccessManager::sendPatch(const QCNetworkRequest &request,
                                                  const QByteArray &data)
{
    return d_func()->dispatchManagedSendRequest(request,
                                                HttpMethod::Patch,
                                                Internal::makeInlineRequestBody(data),
                                                data,
                                                "QCNetworkAccessManager::sendPatch");
}

QCNetworkReply *QCNetworkAccessManager::sendPatch(const QCNetworkRequest &request,
                                                  const QCNetworkBody &body)
{
    return sendPatch(requestWithBodyContentType(request, body), body.data());
}

QCNetworkReply *QCNetworkAccessManager::sendCustomRequest(const QCNetworkRequest &request,
                                                          QByteArrayView method)
{
    if (!isValidHttpToken(method)) {
        return d_func()->createInvalidRequestReply(request,
                                                   HttpMethod::Custom,
                                                   invalidCustomMethodMessage(method),
                                                   this);
    }

    const QByteArray normalizedMethod = normalizedHttpMethodToken(method);
    return d_func()->dispatchManagedSendRequest(
        request,
        HttpMethod::Custom,
        Internal::makeCustomRequestBody(normalizedMethod),
        QByteArray(),
        "QCNetworkAccessManager::sendCustomRequest");
}

QCNetworkReply *QCNetworkAccessManager::sendCustomRequest(const QCNetworkRequest &request,
                                                          QByteArrayView method,
                                                          const QByteArray &data)
{
    if (!isValidHttpToken(method)) {
        return d_func()->createInvalidRequestReply(request,
                                                   HttpMethod::Custom,
                                                   invalidCustomMethodMessage(method),
                                                   this);
    }

    const QByteArray normalizedMethod = normalizedHttpMethodToken(method);
    return d_func()->dispatchManagedSendRequest(
        request,
        HttpMethod::Custom,
        Internal::makeCustomInlineRequestBody(normalizedMethod, data),
        data,
        "QCNetworkAccessManager::sendCustomRequest");
}

void QCNetworkAccessManager::enableRequestScheduler(bool enabled)
{
    Q_D(QCNetworkAccessManager);
    d->schedulerEnabled = enabled;
}

bool QCNetworkAccessManager::isSchedulerEnabled() const
{
    Q_D(const QCNetworkAccessManager);
    return d->schedulerEnabled;
}

QCNetworkRequestScheduler *QCNetworkAccessManager::scheduler() const
{
    if (QThread::currentThread() != thread()) {
        qWarning() << "QCNetworkAccessManager::scheduler: called from non-owner thread";
        Q_ASSERT_X(QThread::currentThread() == thread(),
                   "QCNetworkAccessManager::scheduler",
                   "scheduler() must be called on the manager owner thread");
        return nullptr;
    }

    return QCNetworkRequestScheduler::instance();
}

} // namespace QCurl
