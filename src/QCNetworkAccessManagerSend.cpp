#include "QCNetworkAccessManager.h"
#include "QCNetworkAccessManager_p.h"
#include "QCNetworkBody.h"
#include "QCNetworkHttpHeaders.h"
#include "QCNetworkMiddleware.h"
#include "QCNetworkReply.h"
#include "QCNetworkRequest.h"
#include "private/QCNetworkProtocolPolicy_p.h"
#include "private/QCRequestPipeline_p.h"
#include "private/QCThreading_p.h"

#include <QIODevice>
#include <QThread>

namespace {

bool hasContentTypeHeader(const QCurl::QCNetworkRequest &request)
{
    const QList<QByteArray> headerNames = request.rawHeaderList();
    for (const auto &name : headerNames) {
        if (name.trimmed().toLower() == QCurl::httpheaders::kContentType.toLower()) {
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
    prepared.setRawHeader(QCurl::httpheaders::kContentType, body.contentType());
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

    QString protocolError;
    if (!Internal::QCNetworkProtocolPolicy::validateCoreUrl(request.url(), &protocolError)) {
        return createInvalidRequestReply(request, method, protocolError, nullptr);
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

QCNetworkReply *QCNetworkAccessManager::head(const QCNetworkRequest &request)
{
    return d_func()->dispatchManagedSendRequest(request,
                                                HttpMethod::Head,
                                                Internal::makeEmptyRequestBody(),
                                                QByteArray(),
                                                "QCNetworkAccessManager::head");
}

QCNetworkReply *QCNetworkAccessManager::get(const QCNetworkRequest &request)
{
    return d_func()->dispatchManagedSendRequest(request,
                                                HttpMethod::Get,
                                                Internal::makeEmptyRequestBody(),
                                                QByteArray(),
                                                "QCNetworkAccessManager::get");
}

QCNetworkReply *QCNetworkAccessManager::post(const QCNetworkRequest &request, const QByteArray &data)
{
    return d_func()->dispatchManagedSendRequest(request,
                                                HttpMethod::Post,
                                                Internal::makeInlineRequestBody(data),
                                                data,
                                                "QCNetworkAccessManager::post");
}

QCNetworkReply *QCNetworkAccessManager::post(const QCNetworkRequest &request,
                                             const QCNetworkBody &body)
{
    return post(requestWithBodyContentType(request, body), body.data());
}

QCNetworkReply *QCNetworkAccessManager::post(const QCNetworkRequest &request,
                                             QIODevice *device,
                                             std::optional<qint64> sizeBytes)
{
    constexpr const char *apiName = "QCNetworkAccessManager::post";
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

QCNetworkReply *QCNetworkAccessManager::put(const QCNetworkRequest &request, const QByteArray &data)
{
    return d_func()->dispatchManagedSendRequest(request,
                                                HttpMethod::Put,
                                                Internal::makeInlineRequestBody(data),
                                                data,
                                                "QCNetworkAccessManager::put");
}

QCNetworkReply *QCNetworkAccessManager::put(const QCNetworkRequest &request,
                                            const QCNetworkBody &body)
{
    return put(requestWithBodyContentType(request, body), body.data());
}

QCNetworkReply *QCNetworkAccessManager::put(const QCNetworkRequest &request,
                                            QIODevice *device,
                                            std::optional<qint64> sizeBytes)
{
    constexpr const char *apiName = "QCNetworkAccessManager::put";
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

QCNetworkReply *QCNetworkAccessManager::patch(const QCNetworkRequest &request,
                                              const QByteArray &data)
{
    return d_func()->dispatchManagedSendRequest(request,
                                                HttpMethod::Patch,
                                                Internal::makeInlineRequestBody(data),
                                                data,
                                                "QCNetworkAccessManager::patch");
}

QCNetworkReply *QCNetworkAccessManager::patch(const QCNetworkRequest &request,
                                              const QCNetworkBody &body)
{
    return patch(requestWithBodyContentType(request, body), body.data());
}

QCNetworkReply *QCNetworkAccessManager::sendCustomRequest(const QCNetworkRequest &request,
                                                          QByteArrayView method)
{
    if (!Internal::QCNetworkProtocolPolicy::isValidHttpMethodToken(method)) {
        return d_func()->createInvalidRequestReply(request,
                                                   HttpMethod::Custom,
                                                   invalidCustomMethodMessage(method),
                                                   this);
    }

    const QByteArray methodToken = method.toByteArray();
    return d_func()->dispatchManagedSendRequest(request,
                                                HttpMethod::Custom,
                                                Internal::makeCustomRequestBody(methodToken),
                                                QByteArray(),
                                                "QCNetworkAccessManager::sendCustomRequest");
}

QCNetworkReply *QCNetworkAccessManager::sendCustomRequest(const QCNetworkRequest &request,
                                                          QByteArrayView method,
                                                          const QByteArray &data)
{
    if (!Internal::QCNetworkProtocolPolicy::isValidHttpMethodToken(method)) {
        return d_func()->createInvalidRequestReply(request,
                                                   HttpMethod::Custom,
                                                   invalidCustomMethodMessage(method),
                                                   this);
    }

    const QByteArray methodToken = method.toByteArray();
    return d_func()->dispatchManagedSendRequest(request,
                                                HttpMethod::Custom,
                                                Internal::makeCustomInlineRequestBody(methodToken,
                                                                                      data),
                                                data,
                                                "QCNetworkAccessManager::sendCustomRequest");
}

} // namespace QCurl
