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
#include <QElapsedTimer>
#include <QIODevice>
#include <QMetaObject>
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

} // namespace

namespace QCurl {

QCNetworkReply *QCNetworkAccessManagerPrivate::dispatchManagedSendRequest(
    const QCNetworkRequest &request,
    HttpMethod method,
    bool async,
    const Internal::RequestBody &requestBodySource,
    const QByteArray &body,
    const char *apiName)
{
    return dispatchSendRequest(request,
                               method,
                               async,
                               requestBodySource,
                               body,
                               apiName,
                               [this, request, method, async, requestBodySource, body]() {
                                   const auto middlewaresSnapshot = q_func()->middlewares();
                                   const QCNetworkRequest modifiedRequest
                                       = prepareManagedRequest(request, middlewaresSnapshot);
                                   return createManagedReply(modifiedRequest,
                                                             method,
                                                             async,
                                                             requestBodySource,
                                                             body,
                                                             middlewaresSnapshot);
                               });
}

QCNetworkReply *QCNetworkAccessManagerPrivate::dispatchSendRequest(
    const QCNetworkRequest &request,
    HttpMethod method,
    bool async,
    const Internal::RequestBody &requestBodySource,
    const QByteArray &body,
    const char *apiName,
    const ReplyFactory &impl)
{
    if (QThread::currentThread() != q_func()->thread()) {
        if (!Internal::hasEventDispatcher(q_func()->thread())) {
            return createNoEventLoopErrorReply(request,
                                               method,
                                               requestBodySource,
                                               body,
                                               nullptr,
                                               apiName);
        }

        QCNetworkReply *result = nullptr;
        QElapsedTimer timer;
        timer.start();
        QMetaObject::invokeMethod(
            q_func(), [impl, &result]() { result = impl(); }, Qt::BlockingQueuedConnection);
        if (timer.elapsed() > 1000) {
            qWarning() << apiName << ": cross-thread blocking call took" << timer.elapsed()
                       << "ms (potential deadlock risk if owner thread is blocked)";
        }
        return result;
    }

    if (async && !Internal::hasEventDispatcher(q_func()->thread())) {
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
                                                true,
                                                Internal::makeEmptyRequestBody(),
                                                QByteArray(),
                                                "QCNetworkAccessManager::sendHead");
}

QCNetworkReply *QCNetworkAccessManager::sendGet(const QCNetworkRequest &request)
{
    return d_func()->dispatchManagedSendRequest(request,
                                                HttpMethod::Get,
                                                true,
                                                Internal::makeEmptyRequestBody(),
                                                QByteArray(),
                                                "QCNetworkAccessManager::sendGet");
}

QCNetworkReply *QCNetworkAccessManager::sendPost(const QCNetworkRequest &request,
                                                 const QByteArray &data)
{
    return d_func()->dispatchManagedSendRequest(request,
                                                HttpMethod::Post,
                                                true,
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
                                                   true,
                                                   rawBodyOwnerThreadErrorMessage(apiName),
                                                   nullptr);
    }

    return d_func()->dispatchManagedSendRequest(request,
                                                HttpMethod::Post,
                                                true,
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
                                                true,
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
                                                   true,
                                                   rawBodyOwnerThreadErrorMessage(apiName),
                                                   nullptr);
    }

    return d_func()->dispatchManagedSendRequest(request,
                                                HttpMethod::Put,
                                                true,
                                                Internal::makeDeviceRequestBody(device,
                                                                                sizeBytes,
                                                                                false),
                                                QByteArray(),
                                                apiName);
}

QCNetworkReply *QCNetworkAccessManager::sendDelete(const QCNetworkRequest &request)
{
    return sendDelete(request, QByteArray());
}

QCNetworkReply *QCNetworkAccessManager::sendDelete(const QCNetworkRequest &request,
                                                   const QByteArray &data)
{
    return d_func()->dispatchManagedSendRequest(request,
                                                HttpMethod::Delete,
                                                true,
                                                Internal::makeInlineRequestBody(data),
                                                data,
                                                "QCNetworkAccessManager::sendDelete");
}

QCNetworkReply *QCNetworkAccessManager::sendPatch(const QCNetworkRequest &request,
                                                  const QByteArray &data)
{
    return d_func()->dispatchManagedSendRequest(request,
                                                HttpMethod::Patch,
                                                true,
                                                Internal::makeInlineRequestBody(data),
                                                data,
                                                "QCNetworkAccessManager::sendPatch");
}

QCNetworkReply *QCNetworkAccessManager::sendPatch(const QCNetworkRequest &request,
                                                  const QCNetworkBody &body)
{
    return sendPatch(requestWithBodyContentType(request, body), body.data());
}

QCNetworkReply *QCNetworkAccessManager::sendGetSync(const QCNetworkRequest &request)
{
    return d_func()->dispatchManagedSendRequest(request,
                                                HttpMethod::Get,
                                                false,
                                                Internal::makeEmptyRequestBody(),
                                                QByteArray(),
                                                "QCNetworkAccessManager::sendGetSync");
}

QCNetworkReply *QCNetworkAccessManager::sendPostSync(const QCNetworkRequest &request,
                                                     const QByteArray &data)
{
    return d_func()->dispatchManagedSendRequest(request,
                                                HttpMethod::Post,
                                                false,
                                                Internal::makeInlineRequestBody(data),
                                                data,
                                                "QCNetworkAccessManager::sendPostSync");
}

QCNetworkReply *QCNetworkAccessManager::sendPostSync(const QCNetworkRequest &request,
                                                     QIODevice *device,
                                                     std::optional<qint64> sizeBytes)
{
    constexpr const char *apiName = "QCNetworkAccessManager::sendPostSync";
    if (QThread::currentThread() != thread()) {
        return d_func()->createInvalidRequestReply(request,
                                                   HttpMethod::Post,
                                                   false,
                                                   rawBodyOwnerThreadErrorMessage(apiName),
                                                   nullptr);
    }

    return d_func()->dispatchManagedSendRequest(request,
                                                HttpMethod::Post,
                                                false,
                                                Internal::makeDeviceRequestBody(device,
                                                                                sizeBytes,
                                                                                true),
                                                QByteArray(),
                                                apiName);
}

QCNetworkReply *QCNetworkAccessManager::sendPutSync(const QCNetworkRequest &request,
                                                    const QByteArray &data)
{
    return d_func()->dispatchManagedSendRequest(request,
                                                HttpMethod::Put,
                                                false,
                                                Internal::makeInlineRequestBody(data),
                                                data,
                                                "QCNetworkAccessManager::sendPutSync");
}

QCNetworkReply *QCNetworkAccessManager::sendPutSync(const QCNetworkRequest &request,
                                                    QIODevice *device,
                                                    std::optional<qint64> sizeBytes)
{
    constexpr const char *apiName = "QCNetworkAccessManager::sendPutSync";
    if (QThread::currentThread() != thread()) {
        return d_func()->createInvalidRequestReply(request,
                                                   HttpMethod::Put,
                                                   false,
                                                   rawBodyOwnerThreadErrorMessage(apiName),
                                                   nullptr);
    }

    return d_func()->dispatchManagedSendRequest(request,
                                                HttpMethod::Put,
                                                false,
                                                Internal::makeDeviceRequestBody(device,
                                                                                sizeBytes,
                                                                                false),
                                                QByteArray(),
                                                apiName);
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
        qWarning() << "QCNetworkAccessManager::scheduler: called from non-owner thread; use "
                      "schedulerOnOwnerThread() to fetch the manager owner-thread scheduler";
        Q_ASSERT_X(QThread::currentThread() == thread(),
                   "QCNetworkAccessManager::scheduler",
                   "scheduler() must be called on the manager owner thread");
        return nullptr;
    }

    return QCNetworkRequestScheduler::instance();
}

QCNetworkRequestScheduler *QCNetworkAccessManager::schedulerOnOwnerThread() const
{
    if (!Internal::hasEventDispatcher(thread())) {
        qWarning() << "QCNetworkAccessManager::schedulerOnOwnerThread: manager owner thread has "
                      "no Qt event dispatcher; blocking call is rejected";
        return nullptr;
    }

    if (QThread::currentThread() == thread()) {
        return QCNetworkRequestScheduler::instance();
    }

    auto *mutableThis                 = const_cast<QCNetworkAccessManager *>(this);
    QCNetworkRequestScheduler *result = nullptr;
    QElapsedTimer timer;
    timer.start();
    const bool invoked = QMetaObject::invokeMethod(
        mutableThis,
        [&result]() { result = QCNetworkRequestScheduler::instance(); },
        Qt::BlockingQueuedConnection);
    if (!invoked) {
        qWarning() << "QCNetworkAccessManager::schedulerOnOwnerThread: failed to marshal call "
                      "back to the manager owner thread";
        return nullptr;
    }
    if (timer.elapsed() > 1000) {
        qWarning() << "QCNetworkAccessManager::schedulerOnOwnerThread: cross-thread blocking call "
                      "took"
                   << timer.elapsed() << "ms (potential deadlock risk if owner thread is blocked)";
    }
    return result;
}

} // namespace QCurl
