#include "QCNetworkAccessManager.h"

#include "QCNetworkAccessManager_p.h"
#include "QCNetworkCachePolicy.h"
#include "QCNetworkMiddleware.h"
#include "QCNetworkReply.h"
#include "QCNetworkReply_p.h"
#include "QCNetworkRequest.h"
#include "private/QCNetworkCacheIntegration_p.h"
#include "private/QCNetworkReplyProperties_p.h"
#include "private/QCNetworkRequestScheduler_p.h"
#include "private/QCThreading_p.h"

#include <QPointer>
#include <QVariant>

namespace QCurl {

namespace {

constexpr const char kMiddlewareResponseInvokedProperty[] = "_qcurl_middleware_response_invoked";

void queueReplyFailure(QCNetworkReply *reply, NetworkError error, const QString &message)
{
    QMetaObject::invokeMethod(
        reply,
        [reply, error, message]() { reply->abortWithError(error, message); },
        Qt::QueuedConnection);
}

void runReplyCreatedMiddlewares(QCNetworkReply *reply,
                                QCNetworkAccessManager *manager,
                                const QCNetworkAccessManagerPrivate::MiddlewareSnapshot &middlewares,
                                QObject *context)
{
    const QPointer<QCNetworkReply> safeReply(reply);
    const QPointer<QCNetworkAccessManager> safeManager(manager);
    const QPointer<QObject> safeContext(context);
    for (const auto &entry : middlewares) {
        if (!safeReply || !safeManager || !safeContext) {
            return;
        }
        if (auto *middleware = entry->middleware) {
            middleware->onReplyCreated(reply);
        }
    }
}

void runResponseMiddlewaresOnce(QCNetworkReply *reply,
                                QCNetworkAccessManager *manager,
                                const QCNetworkAccessManagerPrivate::MiddlewareSnapshot &middlewares,
                                QObject *context)
{
    if (!reply || !manager || middlewares.isEmpty()) {
        return;
    }
    if (reply->property(Internal::replyproperties::kDestroying).toBool()) {
        return;
    }

    if (reply->property(kMiddlewareResponseInvokedProperty).toBool()) {
        return;
    }
    // finished 可能被已完成 reply 触发多次探测，这里用属性做幂等保护。
    reply->setProperty(kMiddlewareResponseInvokedProperty, true);

    const QPointer<QCNetworkReply> safeReply(reply);
    const QPointer<QCNetworkAccessManager> safeManager(manager);
    const QPointer<QObject> safeContext(context);
    for (const auto &entry : middlewares) {
        if (!safeReply || !safeManager || !safeContext) {
            return;
        }
        if (auto *middleware = entry->middleware) {
            middleware->onResponseReceived(reply);
        }
    }
}

void wireResponseMiddlewares(QCNetworkAccessManager *manager,
                             QCNetworkReply *reply,
                             const QCNetworkAccessManagerPrivate::MiddlewareSnapshot &middlewares,
                             QObject *context)
{
    if (!manager || !reply || middlewares.isEmpty()) {
        return;
    }

    QPointer<QCNetworkReply> safeReply(reply);
    QPointer<QCNetworkAccessManager> safeManager(manager);
    const QPointer<QObject> safeContext(context);
    QObject::connect(reply,
                     &QCNetworkReply::finished,
                     manager,
                     [safeReply, safeManager, safeContext, middlewares]() {
                         if (!safeManager || !safeReply || !safeContext) {
                             return;
                         }
                         runResponseMiddlewaresOnce(safeReply.data(),
                                                    safeManager.data(),
                                                    middlewares,
                                                    safeContext.data());
                     });

    // 对已完成 reply 立即补跑一次，避免“先 finished、后接线”导致漏掉响应中间件。
    if (reply->isFinished()) {
        runResponseMiddlewaresOnce(reply, manager, middlewares, context);
    }
}

} // namespace

std::optional<QCNetworkRequest> QCNetworkAccessManagerPrivate::prepareManagedRequest(
    const QCNetworkRequest &request, const MiddlewareSnapshot &middlewares, QObject *context) const
{
    const QPointer<const QCNetworkAccessManager> safeManager(q_func());
    const QPointer<QObject> safeContext(context);
    QCNetworkRequest modifiedRequest = request;
    for (const auto &entry : middlewares) {
        if (!safeManager || !safeContext) {
            return std::nullopt;
        }
        if (auto *middleware = entry->middleware) {
            middleware->onRequestPreSend(modifiedRequest);
        }
    }
    if (!safeManager || !safeContext) {
        return std::nullopt;
    }
    return modifiedRequest;
}

QCNetworkAccessManager::QCNetworkAccessManager(QObject *parent)
    : QObject(parent)
    , d_ptr(new QCNetworkAccessManagerPrivate(this))
{
    Q_D(QCNetworkAccessManager);
    d->scheduler = new QCNetworkRequestScheduler(this);
    d->connectSchedulerSignals();
}

QCNetworkAccessManager::~QCNetworkAccessManager()
{
    Q_D(QCNetworkAccessManager);
    QObject::disconnect(d->scheduler, nullptr, this, nullptr);
    clearMiddlewares();
}

QCNetworkReply *QCNetworkAccessManagerPrivate::createReply(
    const QCNetworkRequest &request,
    HttpMethod method,
    const Internal::RequestBody &requestBodySource,
    const QByteArray &body,
    QObject *parent)
{
    auto *reply = new QCNetworkReply(QCNetworkReply::FactoryKey{},
                                     request,
                                     method,
                                     requestBodySource,
                                     body,
                                     parent);
    if (QThread::currentThread() == q_func()->thread()) {
        applyReplyDefaults(reply);
    }
    return reply;
}

QCNetworkReply *QCNetworkAccessManagerPrivate::createManagedReply(
    const QCNetworkRequest &request,
    HttpMethod method,
    const Internal::RequestBody &requestBodySource,
    const QByteArray &body,
    const MiddlewareSnapshot &middlewares)
{
    const QPointer<const QCNetworkAccessManager> safeManager(q_func());
    auto *reply = createPreparedManagedReply(request,
                                             method,
                                             requestBodySource,
                                             body,
                                             middlewares,
                                             q_func());
    if (!safeManager || !reply) {
        return nullptr;
    }
    QPointer<QCNetworkReply> safeReply(reply);
    startPreparedReply(reply, request);
    return safeManager ? safeReply.data() : nullptr;
}

QCNetworkReply *QCNetworkAccessManagerPrivate::createPreparedManagedReply(
    const QCNetworkRequest &request,
    HttpMethod method,
    const Internal::RequestBody &requestBodySource,
    const QByteArray &body,
    const MiddlewareSnapshot &middlewares,
    QObject *context)
{
    const QPointer<const QCNetworkAccessManager> safeManager(q_func());
    const QPointer<QObject> safeContext(context);
    auto *reply = createCacheAwareReply(request, method, requestBodySource, body);
    const QPointer<QCNetworkReply> safeReply(reply);
    prepareManagedReply(reply, middlewares, context);
    if (!safeManager || !safeContext) {
        if (safeReply) {
            safeReply->deleteLater();
        }
        return nullptr;
    }
    return safeReply.data();
}

QCNetworkReply *QCNetworkAccessManagerPrivate::createCacheAwareReply(
    const QCNetworkRequest &request,
    HttpMethod method,
    const Internal::RequestBody &requestBodySource,
    const QByteArray &body)
{
    const bool managerUsesCookies = shareHandleConfig.shareCookies() || !cookieFilePath.isEmpty();
    const QCNetworkCacheRequestKey cacheKey = Internal::buildCacheRequestKey(request,
                                                                             method,
                                                                             managerUsesCookies);
    QCNetworkRequest effectiveRequest       = request;
    QCNetworkCacheLookupResult staleEntry;
    bool cacheRevalidation = false;
    if (cache && request.cachePolicy() == QCNetworkCachePolicy::PreferCache
        && (method == HttpMethod::Get || method == HttpMethod::Head)) {
        staleEntry = cache->lookup(cacheKey, QCNetworkCacheReadMode::AllowStale);
        if (staleEntry.status() == QCNetworkCacheLookupStatus::StaleHit
            && Internal::cacheMetadataHasValidator(staleEntry.metadata())) {
            effectiveRequest = Internal::requestWithCacheValidators(request, staleEntry.metadata());
            cacheRevalidation = true;
        }
    }

    auto *reply = createReply(effectiveRequest, method, requestBodySource, body, q_func());
    if (reply) {
        auto *replyPrivate                       = reply->d_func();
        replyPrivate->cacheRequestKey            = cacheKey;
        replyPrivate->cacheRequestKeyInitialized = true;
        replyPrivate->staleCacheEntry            = staleEntry;
        replyPrivate->cacheRevalidation          = cacheRevalidation;
    }
    return reply;
}

QCNetworkReply *QCNetworkAccessManagerPrivate::createNoEventLoopErrorReply(
    const QCNetworkRequest &request,
    HttpMethod method,
    const Internal::RequestBody &requestBodySource,
    const QByteArray &body,
    QObject *parent,
    const char *apiName)
{
    auto *reply = createReply(request, method, requestBodySource, body, parent);
    if (!reply) {
        return nullptr;
    }

    reply->abortWithError(NetworkError::InvalidRequest,
                          QStringLiteral("%1: owner 线程缺少 Qt 事件循环，无法执行异步请求")
                              .arg(QString::fromUtf8(apiName)));
    return reply;
}

QCNetworkReply *QCNetworkAccessManagerPrivate::createInvalidRequestReply(
    const QCNetworkRequest &request, HttpMethod method, const QString &message, QObject *parent)
{
    Q_UNUSED(parent);
    const bool ownerThread = QThread::currentThread() == q_func()->thread();
    auto *reply            = createReply(request,
                                         method,
                                         Internal::makeEmptyRequestBody(),
                                         QByteArray(),
                                         ownerThread ? q_func() : nullptr);
    if (!reply) {
        return nullptr;
    }

    if (ownerThread && Internal::hasEventDispatcher(q_func()->thread())) {
        queueReplyFailure(reply, NetworkError::InvalidRequest, message);
    } else {
        reply->abortWithError(NetworkError::InvalidRequest, message);
    }
    return reply;
}

void QCNetworkAccessManagerPrivate::applyReplyDefaults(QCNetworkReply *reply) const
{
    if (!reply) {
        return;
    }

    auto *replyPrivate              = reply->d_func();
    replyPrivate->logger            = logger;
    replyPrivate->debugTraceEnabled = debugTraceEnabled;

    if (cookieModeFlag != QCNetworkAccessManager::NotOpen && !cookieFilePath.isEmpty()) {
        replyPrivate->cookieFilePath = cookieFilePath;
        replyPrivate->cookieMode     = cookieModeFlag;
    }
}

void QCNetworkAccessManagerPrivate::prepareManagedReply(QCNetworkReply *reply,
                                                        const MiddlewareSnapshot &middlewares,
                                                        QObject *context) const
{
    Q_Q(const QCNetworkAccessManager);
    const QPointer<QCNetworkAccessManager> safeManager(const_cast<QCNetworkAccessManager *>(q));
    const QPointer<QCNetworkReply> safeReply(reply);
    const QPointer<QObject> safeContext(context);
    wireResponseMiddlewares(safeManager.data(), reply, middlewares, context);
    if (safeManager && safeReply && safeContext) {
        runReplyCreatedMiddlewares(reply, safeManager.data(), middlewares, context);
    }
}

void QCNetworkAccessManagerPrivate::startPreparedReply(QCNetworkReply *reply,
                                                       const QCNetworkRequest &request) const
{
    if (!reply) {
        return;
    }

    const QPointer<QCNetworkReply> safeReply(reply);
    const QPointer<const QCNetworkAccessManager> safeManager(q_func());
    if (schedulerEnabled) {
        // lane/priority 会在 scheduler 入队时快照，后续信号与 lane 级取消都以该快照为准。
        if (!request.lane().isValid()) {
            queueReplyFailure(reply,
                              NetworkError::InvalidRequest,
                              QStringLiteral("QCNetworkAccessManager: scheduler lane is invalid"));
            return;
        }
        if (!scheduler->policy().isLaneRegistered(request.lane())) {
            queueReplyFailure(reply,
                              NetworkError::InvalidRequest,
                              QStringLiteral(
                                  "QCNetworkAccessManager: scheduler lane is not registered: %1")
                                  .arg(request.lane().name()));
            return;
        }

        if (scheduler) {
            const SchedulerCommandResult result = scheduler->scheduleReply(reply,
                                                                           request.lane(),
                                                                           request.priority());
            if (!safeManager || !safeReply) {
                return;
            }
            if (result != SchedulerCommandResult::Applied) {
                queueReplyFailure(
                    reply,
                    NetworkError::InvalidRequest,
                    QStringLiteral(
                        "QCNetworkAccessManager: scheduler rejected request command (%1)")
                        .arg(static_cast<int>(result)));
            }
        } else {
            queueReplyFailure(reply,
                              NetworkError::InvalidRequest,
                              QStringLiteral(
                                  "QCNetworkAccessManager: owner 线程无法提供请求调度器"));
        }
        return;
    }

    QMetaObject::invokeMethod(reply, &QCNetworkReply::execute, Qt::QueuedConnection);
}

} // namespace QCurl
