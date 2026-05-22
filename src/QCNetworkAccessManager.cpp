#include "QCNetworkAccessManager.h"

#include "QCNetworkAccessManager_p.h"
#include "QCNetworkMiddleware.h"
#include "QCNetworkReply.h"
#include "QCNetworkReply_p.h"
#include "QCNetworkRequest.h"
#include "QCNetworkRequestScheduler.h"
#include "private/CurlGlobalConstructor_p.h"

#include <QPointer>
#include <QVariant>

namespace QCurl {

namespace {

constexpr const char kMiddlewareResponseInvokedProperty[] = "_qcurl_middleware_response_invoked";

void runReplyCreatedMiddlewares(QCNetworkReply *reply,
                                const QList<QCNetworkMiddleware *> &middlewares)
{
    if (!reply || middlewares.isEmpty()) {
        return;
    }

    for (auto *middleware : middlewares) {
        if (middleware) {
            middleware->onReplyCreated(reply);
        }
    }
}

void runResponseMiddlewaresOnce(QCNetworkReply *reply,
                                QCNetworkAccessManager *manager,
                                const QList<QCNetworkMiddleware *> &middlewares)
{
    if (!reply || !manager || middlewares.isEmpty()) {
        return;
    }

    if (reply->property(kMiddlewareResponseInvokedProperty).toBool()) {
        return;
    }
    // finished 可能被已完成 reply 触发多次探测，这里用属性做幂等保护。
    reply->setProperty(kMiddlewareResponseInvokedProperty, true);

    const QList<QCNetworkMiddleware *> activeMiddlewares = manager->middlewares();
    for (auto *middleware : middlewares) {
        if (!activeMiddlewares.contains(middleware)) {
            continue;
        }
        if (middleware) {
            middleware->onResponseReceived(reply);
        }
    }
}

void wireResponseMiddlewares(QCNetworkAccessManager *manager,
                             QCNetworkReply *reply,
                             const QList<QCNetworkMiddleware *> &middlewares)
{
    if (!manager || !reply || middlewares.isEmpty()) {
        return;
    }

    QPointer<QCNetworkReply> safeReply(reply);
    QPointer<QCNetworkAccessManager> safeManager(manager);
    QObject::connect(reply, &QCNetworkReply::finished, manager, [safeReply, safeManager, middlewares]() {
        if (!safeManager) {
            return;
        }
        if (!safeReply) {
            return;
        }
        runResponseMiddlewaresOnce(safeReply.data(), safeManager.data(), middlewares);
    });

    // 对已完成 reply 立即补跑一次，避免“先 finished、后接线”导致漏掉响应中间件。
    if (reply->isFinished()) {
        runResponseMiddlewaresOnce(reply, manager, middlewares);
    }
}

} // namespace

QCNetworkRequest QCNetworkAccessManagerPrivate::prepareManagedRequest(
    const QCNetworkRequest &request, const QList<QCNetworkMiddleware *> &middlewares) const
{
    if (middlewares.isEmpty()) {
        return request;
    }

    QCNetworkRequest modifiedRequest = request;
    for (auto *middleware : middlewares) {
        if (middleware) {
            middleware->onRequestPreSend(modifiedRequest);
        }
    }
    return modifiedRequest;
}

QCNetworkAccessManager::QCNetworkAccessManager(QObject *parent)
    : QObject(parent)
    , d_ptr(new QCNetworkAccessManagerPrivate(this))
{
    CurlGlobalConstructor::instance();
}

QCNetworkAccessManager::~QCNetworkAccessManager()
{
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
    applyReplyDefaults(reply);
    return reply;
}

QCNetworkReply *QCNetworkAccessManagerPrivate::createManagedReply(
    const QCNetworkRequest &request,
    HttpMethod method,
    const Internal::RequestBody &requestBodySource,
    const QByteArray &body,
    const QList<QCNetworkMiddleware *> &middlewares)
{
    auto *reply = createPreparedManagedReply(request, method, requestBodySource, body, middlewares);
    startPreparedReply(reply, request);
    return reply;
}

QCNetworkReply *QCNetworkAccessManagerPrivate::createPreparedManagedReply(
    const QCNetworkRequest &request,
    HttpMethod method,
    const Internal::RequestBody &requestBodySource,
    const QByteArray &body,
    const QList<QCNetworkMiddleware *> &middlewares)
{
    auto *reply = createReply(request, method, requestBodySource, body, q_func());
    prepareManagedReply(reply, middlewares);
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
    const QCNetworkRequest &request,
    HttpMethod method,
    const QString &message,
    QObject *parent)
{
    auto *reply = createReply(request, method, Internal::makeEmptyRequestBody(), QByteArray(), parent);
    if (!reply) {
        return nullptr;
    }

    reply->abortWithError(NetworkError::InvalidRequest, message);
    return reply;
}

void QCNetworkAccessManagerPrivate::applyReplyDefaults(QCNetworkReply *reply) const
{
    if (!reply) {
        return;
    }

    if (cookieModeFlag != QCNetworkAccessManager::NotOpen && !cookieFilePath.isEmpty()) {
        reply->d_func()->cookieFilePath = cookieFilePath;
        reply->d_func()->cookieMode     = cookieModeFlag;
    }
}

void QCNetworkAccessManagerPrivate::prepareManagedReply(
    QCNetworkReply *reply, const QList<QCNetworkMiddleware *> &middlewares) const
{
    Q_Q(const QCNetworkAccessManager);
    wireResponseMiddlewares(const_cast<QCNetworkAccessManager *>(q), reply, middlewares);
    runReplyCreatedMiddlewares(reply, middlewares);
}

void QCNetworkAccessManagerPrivate::startPreparedReply(QCNetworkReply *reply,
                                                       const QCNetworkRequest &request) const
{
    if (!reply) {
        return;
    }

    if (schedulerEnabled) {
        // lane/priority 会在 scheduler 入队时快照，后续信号与 lane 级取消都以该快照为准。
        const auto *manager = q_func();
        if (auto *requestScheduler = manager->scheduler()) {
            requestScheduler->scheduleReply(reply, request.lane(), request.priority());
        } else {
            reply->abortWithError(NetworkError::InvalidRequest,
                                  QStringLiteral(
                                      "QCNetworkAccessManager: owner 线程无法提供请求调度器"));
        }
        return;
    }

    reply->execute();
}

} // namespace QCurl
