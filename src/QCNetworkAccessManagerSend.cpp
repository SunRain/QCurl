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

QCNetworkReply *QCNetworkAccessManager::post(const QCNetworkRequest &request,
                                             const QByteArray &data)
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

QCNetworkReply *QCNetworkAccessManager::put(const QCNetworkRequest &request,
                                            const QByteArray &data)
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

bool QCNetworkAccessManagerPrivate::rejectOffOwnerThread(QString *error, const char *apiName) const
{
    if (QThread::currentThread() == q_func()->thread()) {
        return false;
    }

    if (error) {
        *error = QStringLiteral("%1 must run on manager owner thread")
                     .arg(QString::fromUtf8(apiName));
    }
    qWarning() << apiName << ": called from non-owner thread";
    Q_ASSERT_X(QThread::currentThread() == q_func()->thread(),
               apiName,
               "QCNetworkAccessManager scheduler API must run on owner thread");
    return true;
}

bool QCNetworkAccessManager::setSchedulerPolicy(const QCNetworkSchedulerPolicy &policy,
                                                QString *error)
{
    Q_D(QCNetworkAccessManager);
    if (d->rejectOffOwnerThread(error, "QCNetworkAccessManager::setSchedulerPolicy")) {
        return false;
    }
    if (!policy.validate(error)) {
        return false;
    }
    if (!d->scheduler->applyPolicy(policy, error)) {
        return false;
    }
    d->schedulerPolicy = policy;
    return true;
}

QCNetworkSchedulerPolicy QCNetworkAccessManager::schedulerPolicy() const
{
    Q_D(const QCNetworkAccessManager);
    if (d->rejectOffOwnerThread(nullptr, "QCNetworkAccessManager::schedulerPolicy")) {
        return QCNetworkSchedulerPolicy{};
    }
    return d->schedulerPolicy;
}

QCNetworkSchedulerStatistics QCNetworkAccessManager::schedulerStatistics() const
{
    Q_D(const QCNetworkAccessManager);
    if (d->rejectOffOwnerThread(nullptr, "QCNetworkAccessManager::schedulerStatistics")) {
        return QCNetworkSchedulerStatistics{};
    }

    const QCNetworkRequestScheduler::Statistics stats = d->scheduler->statistics();

    QCNetworkSchedulerStatistics result;
    result.setPendingRequests(stats.pendingRequests());
    result.setRunningRequests(stats.runningRequests());
    result.setCompletedRequests(stats.completedRequests());
    result.setCancelledRequests(stats.cancelledRequests());
    result.setTotalBytesReceived(stats.totalBytesReceived());
    result.setTotalBytesSent(stats.totalBytesSent());
    result.setAvgResponseTime(stats.avgResponseTime());
    return result;
}

QCNetworkLaneCancelResult QCNetworkAccessManager::cancelLaneRequests(const QCNetworkLaneKey &lane,
                                                                     SchedulerCancelScope scope)
{
    Q_D(QCNetworkAccessManager);
    if (d->rejectOffOwnerThread(nullptr, "QCNetworkAccessManager::cancelLaneRequests")) {
        return QCNetworkLaneCancelResult::failure(
            QCNetworkLaneCancelResult::Status::NonOwnerThread,
            QStringLiteral("QCNetworkAccessManager::cancelLaneRequests must run on owner thread"));
    }
    if (!d->schedulerEnabled) {
        return QCNetworkLaneCancelResult::failure(
            QCNetworkLaneCancelResult::Status::SchedulerDisabled,
            QStringLiteral("QCNetworkAccessManager: request scheduler is not enabled"));
    }
    if (!lane.isValid()) {
        return QCNetworkLaneCancelResult::failure(
            QCNetworkLaneCancelResult::Status::InvalidLane,
            QStringLiteral("QCNetworkAccessManager: invalid scheduler lane cannot be cancelled"));
    }
    if (!d->schedulerPolicy.isLaneRegistered(lane)) {
        return QCNetworkLaneCancelResult::failure(
            QCNetworkLaneCancelResult::Status::UnregisteredLane,
            QStringLiteral("QCNetworkAccessManager: scheduler lane is not registered: %1")
                .arg(lane.name()));
    }
    const auto schedulerScope
        = scope == SchedulerCancelScope::PendingAndRunning
        ? QCNetworkRequestScheduler::CancelLaneScope::PendingAndRunning
        : QCNetworkRequestScheduler::CancelLaneScope::PendingOnly;
    return QCNetworkLaneCancelResult::success(
        d->scheduler->cancelLaneRequests(lane.name(), schedulerScope));
}

#ifdef QCURL_ENABLE_TEST_HOOKS
QCNetworkRequestScheduler *QCNetworkAccessManager::schedulerForTesting() const
{
    Q_D(const QCNetworkAccessManager);
    if (d->rejectOffOwnerThread(nullptr, "QCNetworkAccessManager::schedulerForTesting")) {
        return nullptr;
    }
    return d->scheduler;
}

void QCNetworkAccessManager::registerSchedulerLaneForTesting(const QCNetworkLaneKey &lane)
{
    Q_D(QCNetworkAccessManager);
    if (!lane.isValid()
        || d->rejectOffOwnerThread(nullptr,
                                   "QCNetworkAccessManager::registerSchedulerLaneForTesting")) {
        return;
    }
    if (!d->schedulerPolicy.isLaneRegistered(lane)) {
        QString error;
        const bool registered = d->schedulerPolicy.setLaneConfig(
            lane, QCNetworkSchedulerPolicy::LaneConfig{}, &error);
        Q_ASSERT_X(registered,
                   "QCNetworkAccessManager::registerSchedulerLaneForTesting",
                   qPrintable(error));
    }
}
#endif

} // namespace QCurl
