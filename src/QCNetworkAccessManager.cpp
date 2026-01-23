#include "QCNetworkAccessManager.h"

#include "CurlGlobalConstructor.h"
#include "QCCurlMultiManager.h"
#include "QCMultipartFormData.h"
#include "QCNetworkAccessManager_p.h"
#include "QCNetworkCache.h"
#include "QCNetworkDiskCache.h"
#include "QCNetworkLogger.h"
#include "QCNetworkMiddleware.h"
#include "QCNetworkMockHandler.h"
#include "QCNetworkReply.h"
#include "QCNetworkReply_p.h"
#include "QCNetworkRequestBuilder.h"
#include "QCNetworkRequestScheduler.h"
#include "QCUtility.h"

#include <QAbstractEventDispatcher>
#include <QDebug>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QIODevice>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QSet>
#include <QSharedPointer>
#include <QSocketNotifier>
#include <QStringList>
#include <QThread>
#include <QTimer>
#include <QUrl>

#include <curl/multi.h>

namespace {

static bool hasEventDispatcher(QThread *thread)
{
    return (thread != nullptr) && (QAbstractEventDispatcher::instance(thread) != nullptr);
}

static QCurl::QCNetworkReply *makeNoEventLoopErrorReply(const QCurl::QCNetworkRequest &request,
                                                        QCurl::HttpMethod method,
                                                        const QByteArray &body,
                                                        QObject *parent,
                                                        const char *apiName)
{
    // 不依赖事件循环，直接返回可诊断的 Error reply（可能会同步发射 finished）
    auto *reply = new QCurl::QCNetworkReply(request,
                                            method,
                                            QCurl::ExecutionMode::Async,
                                            body,
                                            parent);
    reply->abortWithError(QCurl::NetworkError::InvalidRequest,
                          QStringLiteral("%1: owner 线程缺少 Qt 事件循环，无法执行异步请求")
                              .arg(QString::fromUtf8(apiName)));
    return reply;
}

} // namespace

namespace QCurl {

namespace {

constexpr const char kMiddlewareResponseInvokedProperty[] = "_qcurl_middleware_response_invoked";

QCNetworkRequest applyRequestPreSendMiddlewares(const QCNetworkRequest &request,
                                                const QList<QCNetworkMiddleware *> &middlewares)
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
                                const QList<QCNetworkMiddleware *> &middlewares)
{
    if (!reply || middlewares.isEmpty()) {
        return;
    }

    if (reply->property(kMiddlewareResponseInvokedProperty).toBool()) {
        return;
    }
    reply->setProperty(kMiddlewareResponseInvokedProperty, true);

    for (auto *middleware : middlewares) {
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
    QObject::connect(reply, &QCNetworkReply::finished, manager, [safeReply, middlewares]() {
        if (!safeReply) {
            return;
        }
        runResponseMiddlewaresOnce(safeReply.data(), middlewares);
    });

    if (reply->isFinished()) {
        runResponseMiddlewaresOnce(reply, middlewares);
    }
}

} // namespace

QCNetworkAccessManager::QCNetworkAccessManager(QObject *parent)
    : QObject(parent)
    , d_ptr(new QCNetworkAccessManagerPrivate(this))
    , m_cookieModeFlag(NotOpen)
    , m_schedulerEnabled(false)
    , m_cache(nullptr)
{
    CurlGlobalConstructor::instance();
}

QCNetworkAccessManager::~QCNetworkAccessManager()
{
    delete d_ptr;
}

QString QCNetworkAccessManager::cookieFilePath() const
{
    return m_cookieFilePath;
}

QCNetworkAccessManager::CookieFileModeFlag QCNetworkAccessManager::cookieFileMode() const
{
    return m_cookieModeFlag;
}

void QCNetworkAccessManager::setCookieFilePath(const QString &cookieFilePath,
                                               CookieFileModeFlag flag)
{
    m_cookieFilePath = cookieFilePath;
    m_cookieModeFlag = flag;
}

bool QCNetworkAccessManager::importCookies(const QList<QNetworkCookie> &cookies,
                                           const QUrl &originUrl,
                                           QString *error)
{
    if (QThread::currentThread() != thread()) {
        if (!hasEventDispatcher(thread())) {
            if (error) {
                *error = QStringLiteral("QCNetworkAccessManager::importCookies: manager "
                                        "所在线程无事件循环，无法跨线程提交");
            }
            return false;
        }

        bool ok = false;
        QString localError;
        QMetaObject::invokeMethod(
            this,
            [this, cookies, originUrl, &ok, &localError]() {
                ok = importCookies(cookies, originUrl, &localError);
            },
            Qt::BlockingQueuedConnection);

        if (error) {
            *error = localError;
        }
        return ok;
    }

    auto *multi = QCCurlMultiManager::instance();
    if (!multi) {
        if (error) {
            *error = QStringLiteral("QCCurlMultiManager 不可用");
        }
        return false;
    }
    return multi->importCookiesForManager(this, cookies, originUrl, error);
}

QList<QNetworkCookie> QCNetworkAccessManager::exportCookies(const QUrl &filterUrl,
                                                            QString *error) const
{
    if (QThread::currentThread() != thread()) {
        if (!hasEventDispatcher(thread())) {
            if (error) {
                *error = QStringLiteral("QCNetworkAccessManager::exportCookies: manager "
                                        "所在线程无事件循环，无法跨线程提交");
            }
            return {};
        }

        QList<QNetworkCookie> cookies;
        QString localError;
        auto *mutableThis = const_cast<QCNetworkAccessManager *>(this);
        QMetaObject::invokeMethod(
            mutableThis,
            [this, filterUrl, &cookies, &localError]() {
                cookies = exportCookies(filterUrl, &localError);
            },
            Qt::BlockingQueuedConnection);

        if (error) {
            *error = localError;
        }
        return cookies;
    }

    auto *multi = QCCurlMultiManager::instance();
    if (!multi) {
        if (error) {
            *error = QStringLiteral("QCCurlMultiManager 不可用");
        }
        return {};
    }
    return multi->exportCookiesForManager(this, filterUrl, error);
}

bool QCNetworkAccessManager::clearAllCookies(QString *error)
{
    if (QThread::currentThread() != thread()) {
        if (!hasEventDispatcher(thread())) {
            if (error) {
                *error = QStringLiteral("QCNetworkAccessManager::clearAllCookies: manager "
                                        "所在线程无事件循环，无法跨线程提交");
            }
            return false;
        }

        bool ok = false;
        QString localError;
        QMetaObject::invokeMethod(
            this,
            [this, &ok, &localError]() { ok = clearAllCookies(&localError); },
            Qt::BlockingQueuedConnection);

        if (error) {
            *error = localError;
        }
        return ok;
    }

    auto *multi = QCCurlMultiManager::instance();
    if (!multi) {
        if (error) {
            *error = QStringLiteral("QCCurlMultiManager 不可用");
        }
        return false;
    }
    return multi->clearAllCookiesForManager(this, error);
}

void QCNetworkAccessManager::setShareHandleConfig(const ShareHandleConfig &config)
{
    m_shareHandleConfig = config;
}

QCNetworkAccessManager::ShareHandleConfig QCNetworkAccessManager::shareHandleConfig() const noexcept
{
    return m_shareHandleConfig;
}

void QCNetworkAccessManager::setHstsAltSvcCacheConfig(const HstsAltSvcCacheConfig &config)
{
    m_hstsAltSvcCacheConfig = config;
}

QCNetworkAccessManager::HstsAltSvcCacheConfig QCNetworkAccessManager::hstsAltSvcCacheConfig()
    const noexcept
{
    return m_hstsAltSvcCacheConfig;
}

// ==================
// 核心 API 实现
// ==================

QCNetworkReply *QCNetworkAccessManager::sendHead(const QCNetworkRequest &request)
{
    const auto impl = [this, request]() -> QCNetworkReply * {
        Q_D(QCNetworkAccessManager);
        const auto middlewaresSnapshot         = d->middlewares;
        const QCNetworkRequest modifiedRequest = applyRequestPreSendMiddlewares(request,
                                                                                middlewaresSnapshot);

        auto *reply = new QCNetworkReply(modifiedRequest,
                                         HttpMethod::Head,
                                         ExecutionMode::Async,
                                         QByteArray(),
                                         this);

        // Cookie 配置传递
        if (m_cookieModeFlag != NotOpen && !m_cookieFilePath.isEmpty()) {
            reply->d_func()->cookieFilePath = m_cookieFilePath;
            reply->d_func()->cookieMode     = m_cookieModeFlag;
        }

        wireResponseMiddlewares(this, reply, middlewaresSnapshot);
        runReplyCreatedMiddlewares(reply, middlewaresSnapshot);
        reply->execute(); // 自动启动请求
        return reply;
    };

    if (QThread::currentThread() != thread()) {
        if (!hasEventDispatcher(thread())) {
            return makeNoEventLoopErrorReply(request,
                                             HttpMethod::Head,
                                             QByteArray(),
                                             nullptr,
                                             "QCNetworkAccessManager::sendHead");
        }

        QCNetworkReply *result = nullptr;
        QElapsedTimer timer;
        timer.start();
        QMetaObject::invokeMethod(
            this, [impl, &result]() { result = impl(); }, Qt::BlockingQueuedConnection);
        if (timer.elapsed() > 1000) {
            qWarning() << "QCNetworkAccessManager::sendHead: cross-thread blocking call took"
                       << timer.elapsed()
                       << "ms (potential deadlock risk if owner thread is blocked)";
        }
        return result;
    }

    if (!hasEventDispatcher(thread())) {
        return makeNoEventLoopErrorReply(request,
                                         HttpMethod::Head,
                                         QByteArray(),
                                         this,
                                         "QCNetworkAccessManager::sendHead");
    }

    return impl();
}

QCNetworkReply *QCNetworkAccessManager::sendGet(const QCNetworkRequest &request)
{
    const auto impl = [this, request]() -> QCNetworkReply * {
        Q_D(QCNetworkAccessManager);
        const auto middlewaresSnapshot         = d->middlewares;
        const QCNetworkRequest modifiedRequest = applyRequestPreSendMiddlewares(request,
                                                                                middlewaresSnapshot);

        auto *reply = new QCNetworkReply(modifiedRequest,
                                         HttpMethod::Get,
                                         ExecutionMode::Async,
                                         QByteArray(),
                                         this);

        // Cookie 配置传递
        if (m_cookieModeFlag != NotOpen && !m_cookieFilePath.isEmpty()) {
            reply->d_func()->cookieFilePath = m_cookieFilePath;
            reply->d_func()->cookieMode     = m_cookieModeFlag;
        }

        wireResponseMiddlewares(this, reply, middlewaresSnapshot);
        runReplyCreatedMiddlewares(reply, middlewaresSnapshot);
        reply->execute(); // 自动启动请求
        return reply;
    };

    if (QThread::currentThread() != thread()) {
        if (!hasEventDispatcher(thread())) {
            return makeNoEventLoopErrorReply(request,
                                             HttpMethod::Get,
                                             QByteArray(),
                                             nullptr,
                                             "QCNetworkAccessManager::sendGet");
        }

        QCNetworkReply *result = nullptr;
        QElapsedTimer timer;
        timer.start();
        QMetaObject::invokeMethod(
            this, [impl, &result]() { result = impl(); }, Qt::BlockingQueuedConnection);
        if (timer.elapsed() > 1000) {
            qWarning() << "QCNetworkAccessManager::sendGet: cross-thread blocking call took"
                       << timer.elapsed()
                       << "ms (potential deadlock risk if owner thread is blocked)";
        }
        return result;
    }

    if (!hasEventDispatcher(thread())) {
        return makeNoEventLoopErrorReply(request,
                                         HttpMethod::Get,
                                         QByteArray(),
                                         this,
                                         "QCNetworkAccessManager::sendGet");
    }

    return impl();
}

QCNetworkReply *QCNetworkAccessManager::sendPost(const QCNetworkRequest &request,
                                                 const QByteArray &data)
{
    const auto impl = [this, request, data]() -> QCNetworkReply * {
        Q_D(QCNetworkAccessManager);
        const auto middlewaresSnapshot         = d->middlewares;
        const QCNetworkRequest modifiedRequest = applyRequestPreSendMiddlewares(request,
                                                                                middlewaresSnapshot);

        auto *reply = new QCNetworkReply(modifiedRequest,
                                         HttpMethod::Post,
                                         ExecutionMode::Async,
                                         data,
                                         this);

        // Cookie 配置传递
        if (m_cookieModeFlag != NotOpen && !m_cookieFilePath.isEmpty()) {
            reply->d_func()->cookieFilePath = m_cookieFilePath;
            reply->d_func()->cookieMode     = m_cookieModeFlag;
        }

        wireResponseMiddlewares(this, reply, middlewaresSnapshot);
        runReplyCreatedMiddlewares(reply, middlewaresSnapshot);
        reply->execute(); // 自动启动请求
        return reply;
    };

    if (QThread::currentThread() != thread()) {
        if (!hasEventDispatcher(thread())) {
            return makeNoEventLoopErrorReply(request,
                                             HttpMethod::Post,
                                             data,
                                             nullptr,
                                             "QCNetworkAccessManager::sendPost");
        }

        QCNetworkReply *result = nullptr;
        QElapsedTimer timer;
        timer.start();
        QMetaObject::invokeMethod(
            this, [impl, &result]() { result = impl(); }, Qt::BlockingQueuedConnection);
        if (timer.elapsed() > 1000) {
            qWarning() << "QCNetworkAccessManager::sendPost: cross-thread blocking call took"
                       << timer.elapsed()
                       << "ms (potential deadlock risk if owner thread is blocked)";
        }
        return result;
    }

    if (!hasEventDispatcher(thread())) {
        return makeNoEventLoopErrorReply(request,
                                         HttpMethod::Post,
                                         data,
                                         this,
                                         "QCNetworkAccessManager::sendPost");
    }

    return impl();
}

QCNetworkReply *QCNetworkAccessManager::sendPut(const QCNetworkRequest &request,
                                                const QByteArray &data)
{
    const auto impl = [this, request, data]() -> QCNetworkReply * {
        Q_D(QCNetworkAccessManager);
        const auto middlewaresSnapshot         = d->middlewares;
        const QCNetworkRequest modifiedRequest = applyRequestPreSendMiddlewares(request,
                                                                                middlewaresSnapshot);

        auto *reply = new QCNetworkReply(modifiedRequest,
                                         HttpMethod::Put,
                                         ExecutionMode::Async,
                                         data,
                                         this);

        // Cookie 配置传递
        if (m_cookieModeFlag != NotOpen && !m_cookieFilePath.isEmpty()) {
            reply->d_func()->cookieFilePath = m_cookieFilePath;
            reply->d_func()->cookieMode     = m_cookieModeFlag;
        }

        wireResponseMiddlewares(this, reply, middlewaresSnapshot);
        runReplyCreatedMiddlewares(reply, middlewaresSnapshot);
        reply->execute(); // 自动启动请求
        return reply;
    };

    if (QThread::currentThread() != thread()) {
        if (!hasEventDispatcher(thread())) {
            return makeNoEventLoopErrorReply(request,
                                             HttpMethod::Put,
                                             data,
                                             nullptr,
                                             "QCNetworkAccessManager::sendPut");
        }

        QCNetworkReply *result = nullptr;
        QElapsedTimer timer;
        timer.start();
        QMetaObject::invokeMethod(
            this, [impl, &result]() { result = impl(); }, Qt::BlockingQueuedConnection);
        if (timer.elapsed() > 1000) {
            qWarning() << "QCNetworkAccessManager::sendPut: cross-thread blocking call took"
                       << timer.elapsed()
                       << "ms (potential deadlock risk if owner thread is blocked)";
        }
        return result;
    }

    if (!hasEventDispatcher(thread())) {
        return makeNoEventLoopErrorReply(request,
                                         HttpMethod::Put,
                                         data,
                                         this,
                                         "QCNetworkAccessManager::sendPut");
    }

    return impl();
}

QCNetworkReply *QCNetworkAccessManager::sendDelete(const QCNetworkRequest &request)
{
    return sendDelete(request, QByteArray());
}

QCNetworkReply *QCNetworkAccessManager::sendDelete(const QCNetworkRequest &request,
                                                   const QByteArray &data)
{
    const auto impl = [this, request, data]() -> QCNetworkReply * {
        Q_D(QCNetworkAccessManager);
        const auto middlewaresSnapshot         = d->middlewares;
        const QCNetworkRequest modifiedRequest = applyRequestPreSendMiddlewares(request,
                                                                                middlewaresSnapshot);

        auto *reply = new QCNetworkReply(modifiedRequest,
                                         HttpMethod::Delete,
                                         ExecutionMode::Async,
                                         data,
                                         this);

        // Cookie 配置传递
        if (m_cookieModeFlag != NotOpen && !m_cookieFilePath.isEmpty()) {
            reply->d_func()->cookieFilePath = m_cookieFilePath;
            reply->d_func()->cookieMode     = m_cookieModeFlag;
        }

        wireResponseMiddlewares(this, reply, middlewaresSnapshot);
        runReplyCreatedMiddlewares(reply, middlewaresSnapshot);
        reply->execute(); // 自动启动请求
        return reply;
    };

    if (QThread::currentThread() != thread()) {
        if (!hasEventDispatcher(thread())) {
            return makeNoEventLoopErrorReply(request,
                                             HttpMethod::Delete,
                                             data,
                                             nullptr,
                                             "QCNetworkAccessManager::sendDelete");
        }

        QCNetworkReply *result = nullptr;
        QElapsedTimer timer;
        timer.start();
        QMetaObject::invokeMethod(
            this, [impl, &result]() { result = impl(); }, Qt::BlockingQueuedConnection);
        if (timer.elapsed() > 1000) {
            qWarning() << "QCNetworkAccessManager::sendDelete: cross-thread blocking call took"
                       << timer.elapsed()
                       << "ms (potential deadlock risk if owner thread is blocked)";
        }
        return result;
    }

    if (!hasEventDispatcher(thread())) {
        return makeNoEventLoopErrorReply(request,
                                         HttpMethod::Delete,
                                         data,
                                         this,
                                         "QCNetworkAccessManager::sendDelete");
    }

    return impl();
}

QCNetworkReply *QCNetworkAccessManager::sendPatch(const QCNetworkRequest &request,
                                                  const QByteArray &data)
{
    const auto impl = [this, request, data]() -> QCNetworkReply * {
        Q_D(QCNetworkAccessManager);
        const auto middlewaresSnapshot         = d->middlewares;
        const QCNetworkRequest modifiedRequest = applyRequestPreSendMiddlewares(request,
                                                                                middlewaresSnapshot);

        auto *reply = new QCNetworkReply(modifiedRequest,
                                         HttpMethod::Patch,
                                         ExecutionMode::Async,
                                         data,
                                         this);

        // Cookie 配置传递
        if (m_cookieModeFlag != NotOpen && !m_cookieFilePath.isEmpty()) {
            reply->d_func()->cookieFilePath = m_cookieFilePath;
            reply->d_func()->cookieMode     = m_cookieModeFlag;
        }

        wireResponseMiddlewares(this, reply, middlewaresSnapshot);
        runReplyCreatedMiddlewares(reply, middlewaresSnapshot);
        reply->execute(); // 自动启动请求
        return reply;
    };

    if (QThread::currentThread() != thread()) {
        if (!hasEventDispatcher(thread())) {
            return makeNoEventLoopErrorReply(request,
                                             HttpMethod::Patch,
                                             data,
                                             nullptr,
                                             "QCNetworkAccessManager::sendPatch");
        }

        QCNetworkReply *result = nullptr;
        QElapsedTimer timer;
        timer.start();
        QMetaObject::invokeMethod(
            this, [impl, &result]() { result = impl(); }, Qt::BlockingQueuedConnection);
        if (timer.elapsed() > 1000) {
            qWarning() << "QCNetworkAccessManager::sendPatch: cross-thread blocking call took"
                       << timer.elapsed()
                       << "ms (potential deadlock risk if owner thread is blocked)";
        }
        return result;
    }

    if (!hasEventDispatcher(thread())) {
        return makeNoEventLoopErrorReply(request,
                                         HttpMethod::Patch,
                                         data,
                                         this,
                                         "QCNetworkAccessManager::sendPatch");
    }

    return impl();
}

QCNetworkReply *QCNetworkAccessManager::sendGetSync(const QCNetworkRequest &request)
{
    const auto impl = [this, request]() -> QCNetworkReply * {
        Q_D(QCNetworkAccessManager);
        const auto middlewaresSnapshot         = d->middlewares;
        const QCNetworkRequest modifiedRequest = applyRequestPreSendMiddlewares(request,
                                                                                middlewaresSnapshot);

        auto *reply = new QCNetworkReply(modifiedRequest,
                                         HttpMethod::Get,
                                         ExecutionMode::Sync,
                                         QByteArray(),
                                         this);

        // Cookie 配置传递
        if (m_cookieModeFlag != NotOpen && !m_cookieFilePath.isEmpty()) {
            reply->d_func()->cookieFilePath = m_cookieFilePath;
            reply->d_func()->cookieMode     = m_cookieModeFlag;
        }

        wireResponseMiddlewares(this, reply, middlewaresSnapshot);
        runReplyCreatedMiddlewares(reply, middlewaresSnapshot);
        reply->execute(); // 同步执行（会阻塞）
        return reply;
    };

    if (QThread::currentThread() != thread()) {
        if (!hasEventDispatcher(thread())) {
            return makeNoEventLoopErrorReply(request,
                                             HttpMethod::Get,
                                             QByteArray(),
                                             nullptr,
                                             "QCNetworkAccessManager::sendGetSync");
        }

        QCNetworkReply *result = nullptr;
        QElapsedTimer timer;
        timer.start();
        QMetaObject::invokeMethod(
            this, [impl, &result]() { result = impl(); }, Qt::BlockingQueuedConnection);
        if (timer.elapsed() > 1000) {
            qWarning() << "QCNetworkAccessManager::sendGetSync: cross-thread blocking call took"
                       << timer.elapsed()
                       << "ms (potential deadlock risk if owner thread is blocked)";
        }
        return result;
    }

    return impl();
}

QCNetworkReply *QCNetworkAccessManager::sendPostSync(const QCNetworkRequest &request,
                                                     const QByteArray &data)
{
    const auto impl = [this, request, data]() -> QCNetworkReply * {
        Q_D(QCNetworkAccessManager);
        const auto middlewaresSnapshot         = d->middlewares;
        const QCNetworkRequest modifiedRequest = applyRequestPreSendMiddlewares(request,
                                                                                middlewaresSnapshot);

        auto *reply = new QCNetworkReply(modifiedRequest,
                                         HttpMethod::Post,
                                         ExecutionMode::Sync,
                                         data,
                                         this);

        // Cookie 配置传递
        if (m_cookieModeFlag != NotOpen && !m_cookieFilePath.isEmpty()) {
            reply->d_func()->cookieFilePath = m_cookieFilePath;
            reply->d_func()->cookieMode     = m_cookieModeFlag;
        }

        wireResponseMiddlewares(this, reply, middlewaresSnapshot);
        runReplyCreatedMiddlewares(reply, middlewaresSnapshot);
        reply->execute(); // 同步执行（会阻塞）
        return reply;
    };

    if (QThread::currentThread() != thread()) {
        if (!hasEventDispatcher(thread())) {
            return makeNoEventLoopErrorReply(request,
                                             HttpMethod::Post,
                                             data,
                                             nullptr,
                                             "QCNetworkAccessManager::sendPostSync");
        }

        QCNetworkReply *result = nullptr;
        QElapsedTimer timer;
        timer.start();
        QMetaObject::invokeMethod(
            this, [impl, &result]() { result = impl(); }, Qt::BlockingQueuedConnection);
        if (timer.elapsed() > 1000) {
            qWarning() << "QCNetworkAccessManager::sendPostSync: cross-thread blocking call took"
                       << timer.elapsed()
                       << "ms (potential deadlock risk if owner thread is blocked)";
        }
        return result;
    }

    return impl();
}

// ==================
// 请求优先级调度
// ==================

void QCNetworkAccessManager::enableRequestScheduler(bool enable)
{
    m_schedulerEnabled = enable;
}

bool QCNetworkAccessManager::isSchedulerEnabled() const
{
    return m_schedulerEnabled;
}

QCNetworkRequestScheduler *QCNetworkAccessManager::scheduler() const
{
    return QCNetworkRequestScheduler::instance();
}

QCNetworkReply *QCNetworkAccessManager::scheduleGet(const QCNetworkRequest &request)
{
    const auto impl = [this, request]() -> QCNetworkReply * {
        if (m_schedulerEnabled) {
            Q_D(QCNetworkAccessManager);
            const auto middlewaresSnapshot = d->middlewares;
            const QCNetworkRequest modifiedRequest
                = applyRequestPreSendMiddlewares(request, middlewaresSnapshot);

            auto *reply = scheduler()->scheduleRequest(modifiedRequest,
                                                       HttpMethod::Get,
                                                       request.priority(),
                                                       QByteArray(),
                                                       this);
            wireResponseMiddlewares(this, reply, middlewaresSnapshot);
            runReplyCreatedMiddlewares(reply, middlewaresSnapshot);
            return reply;
        }

        // 回退到直接执行
        return sendGet(request);
    };

    if (QThread::currentThread() != thread()) {
        if (!hasEventDispatcher(thread())) {
            return makeNoEventLoopErrorReply(request,
                                             HttpMethod::Get,
                                             QByteArray(),
                                             nullptr,
                                             "QCNetworkAccessManager::scheduleGet");
        }

        QCNetworkReply *result = nullptr;
        QElapsedTimer timer;
        timer.start();
        QMetaObject::invokeMethod(
            this, [impl, &result]() { result = impl(); }, Qt::BlockingQueuedConnection);

        if (timer.elapsed() > 1000) {
            qWarning() << "QCNetworkAccessManager::scheduleGet: cross-thread blocking call took"
                       << timer.elapsed()
                       << "ms (potential deadlock risk if owner thread is blocked)";
        }
        return result;
    }

    if (!hasEventDispatcher(thread())) {
        return makeNoEventLoopErrorReply(request,
                                         HttpMethod::Get,
                                         QByteArray(),
                                         this,
                                         "QCNetworkAccessManager::scheduleGet");
    }

    return impl();
}

QCNetworkReply *QCNetworkAccessManager::schedulePost(const QCNetworkRequest &request,
                                                     const QByteArray &data)
{
    const auto impl = [this, request, data]() -> QCNetworkReply * {
        if (m_schedulerEnabled) {
            Q_D(QCNetworkAccessManager);
            const auto middlewaresSnapshot = d->middlewares;
            const QCNetworkRequest modifiedRequest
                = applyRequestPreSendMiddlewares(request, middlewaresSnapshot);

            auto *reply = scheduler()->scheduleRequest(modifiedRequest,
                                                       HttpMethod::Post,
                                                       request.priority(),
                                                       data,
                                                       this);
            wireResponseMiddlewares(this, reply, middlewaresSnapshot);
            runReplyCreatedMiddlewares(reply, middlewaresSnapshot);
            return reply;
        }

        // 回退到直接执行
        return sendPost(request, data);
    };

    if (QThread::currentThread() != thread()) {
        if (!hasEventDispatcher(thread())) {
            return makeNoEventLoopErrorReply(request,
                                             HttpMethod::Post,
                                             data,
                                             nullptr,
                                             "QCNetworkAccessManager::schedulePost");
        }

        QCNetworkReply *result = nullptr;
        QElapsedTimer timer;
        timer.start();
        QMetaObject::invokeMethod(
            this, [impl, &result]() { result = impl(); }, Qt::BlockingQueuedConnection);

        if (timer.elapsed() > 1000) {
            qWarning() << "QCNetworkAccessManager::schedulePost: cross-thread blocking call took"
                       << timer.elapsed()
                       << "ms (potential deadlock risk if owner thread is blocked)";
        }
        return result;
    }

    if (!hasEventDispatcher(thread())) {
        return makeNoEventLoopErrorReply(request,
                                         HttpMethod::Post,
                                         data,
                                         this,
                                         "QCNetworkAccessManager::schedulePost");
    }

    return impl();
}

QCNetworkReply *QCNetworkAccessManager::schedulePut(const QCNetworkRequest &request,
                                                    const QByteArray &data)
{
    const auto impl = [this, request, data]() -> QCNetworkReply * {
        if (m_schedulerEnabled) {
            Q_D(QCNetworkAccessManager);
            const auto middlewaresSnapshot = d->middlewares;
            const QCNetworkRequest modifiedRequest
                = applyRequestPreSendMiddlewares(request, middlewaresSnapshot);

            auto *reply = scheduler()->scheduleRequest(modifiedRequest,
                                                       HttpMethod::Put,
                                                       request.priority(),
                                                       data,
                                                       this);
            wireResponseMiddlewares(this, reply, middlewaresSnapshot);
            runReplyCreatedMiddlewares(reply, middlewaresSnapshot);
            return reply;
        }

        // 回退到直接执行
        return sendPut(request, data);
    };

    if (QThread::currentThread() != thread()) {
        if (!hasEventDispatcher(thread())) {
            return makeNoEventLoopErrorReply(request,
                                             HttpMethod::Put,
                                             data,
                                             nullptr,
                                             "QCNetworkAccessManager::schedulePut");
        }

        QCNetworkReply *result = nullptr;
        QElapsedTimer timer;
        timer.start();
        QMetaObject::invokeMethod(
            this, [impl, &result]() { result = impl(); }, Qt::BlockingQueuedConnection);

        if (timer.elapsed() > 1000) {
            qWarning() << "QCNetworkAccessManager::schedulePut: cross-thread blocking call took"
                       << timer.elapsed()
                       << "ms (potential deadlock risk if owner thread is blocked)";
        }
        return result;
    }

    if (!hasEventDispatcher(thread())) {
        return makeNoEventLoopErrorReply(request,
                                         HttpMethod::Put,
                                         data,
                                         this,
                                         "QCNetworkAccessManager::schedulePut");
    }

    return impl();
}

// ==================
// JSON 快捷方法实现
// ==================

QCNetworkReply *QCNetworkAccessManager::postJson(const QUrl &url, const QJsonObject &json)
{
    // 序列化 JSON 对象
    QJsonDocument doc(json);
    QByteArray jsonData = doc.toJson(QJsonDocument::Compact);

    // 创建请求并设置 Content-Type
    QCNetworkRequest request(url);
    request.setRawHeader("Content-Type", "application/json");

    // 发送 POST 请求
    return sendPost(request, jsonData);
}

QCNetworkReply *QCNetworkAccessManager::postForm(const QUrl &url,
                                                 const QMap<QString, QString> &formData)
{
    // URL编码表单数据
    QStringList pairs;
    for (auto it = formData.constBegin(); it != formData.constEnd(); ++it) {
        QString key   = QUrl::toPercentEncoding(it.key());
        QString value = QUrl::toPercentEncoding(it.value());
        pairs.append(QStringLiteral("%1=%2").arg(key, value));
    }
    QByteArray encodedData = pairs.join("&").toUtf8();

    // 创建请求并设置 Content-Type
    QCNetworkRequest request(url);
    request.setRawHeader("Content-Type", "application/x-www-form-urlencoded");

    // 发送 POST 请求
    return sendPost(request, encodedData);
}

// ==================
// Multipart/form-data 支持
// ==================

QCNetworkReply *QCNetworkAccessManager::postMultipart(const QUrl &url,
                                                      const QCMultipartFormData &formData)
{
    // 编码 Multipart 表单数据
    QByteArray multipartData = formData.toByteArray();

    // 创建请求并设置 Content-Type
    QCNetworkRequest request(url);
    request.setRawHeader("Content-Type", formData.contentType().toUtf8());

    // 发送 POST 请求
    return sendPost(request, multipartData);
}

QCNetworkReply *QCNetworkAccessManager::postMultipart(const QCNetworkRequest &request,
                                                      const QCMultipartFormData &formData)
{
    // 创建请求副本并设置 Content-Type
    QCNetworkRequest modifiedRequest = request;
    modifiedRequest.setRawHeader("Content-Type", formData.contentType().toUtf8());

    // 编码 Multipart 表单数据
    QByteArray multipartData = formData.toByteArray();

    // 发送 POST 请求
    return sendPost(modifiedRequest, multipartData);
}

// ==================
// 文件操作便捷 API
// ==================

QCNetworkReply *QCNetworkAccessManager::downloadFile(const QUrl &url, const QString &savePath)
{
    // 创建请求
    QCNetworkRequest request(url);

    // 发送 GET 请求
    QCNetworkReply *reply = sendGet(request);

    // 连接信号以保存文件
    QObject::connect(reply, &QCNetworkReply::finished, reply, [reply, savePath]() {
        if (reply->error() == NetworkError::NoError) {
            // 读取响应数据
            auto responseData = reply->readAll();
            if (responseData.has_value()) {
                // 保存到文件
                QFile file(savePath);
                if (file.open(QIODevice::WriteOnly)) {
                    file.write(responseData.value());
                    file.close();
                    qDebug() << "File saved to:" << savePath;
                } else {
                    qWarning() << "Cannot open file for writing:" << savePath;
                }
            }
        }
    });

    return reply;
}

QCNetworkReply *QCNetworkAccessManager::uploadFile(const QUrl &url, const QString &filePath)
{
    // 使用默认字段名 "file"
    return uploadFile(url, "file", filePath);
}

QCNetworkReply *QCNetworkAccessManager::uploadFile(const QUrl &url,
                                                   const QString &fieldName,
                                                   const QString &filePath)
{
    // 创建 Multipart 表单数据
    QCMultipartFormData formData;

    // 添加文件字段
    if (!formData.addFileField(fieldName, filePath)) {
        qWarning() << "Cannot add file to form:" << filePath;
        QCNetworkRequest request(url);
        return sendGet(request);
    }

    // 使用 postMultipart 发送
    return postMultipart(url, formData);
}

// ==================
// 流式下载/上传 API
// ==================

QCNetworkReply *QCNetworkAccessManager::downloadToDevice(const QUrl &url, QIODevice *device)
{
    Q_D(QCNetworkAccessManager);
    const auto middlewaresSnapshot = d->middlewares;

    QCNetworkRequest request(url);
    const QCNetworkRequest modifiedRequest = applyRequestPreSendMiddlewares(request,
                                                                            middlewaresSnapshot);

    auto *reply = new QCNetworkReply(modifiedRequest,
                                     HttpMethod::Get,
                                     ExecutionMode::Async,
                                     QByteArray(),
                                     this);

    // Cookie 配置传递
    if (m_cookieModeFlag != NotOpen && !m_cookieFilePath.isEmpty()) {
        reply->d_func()->cookieFilePath = m_cookieFilePath;
        reply->d_func()->cookieMode     = m_cookieModeFlag;
    }

    wireResponseMiddlewares(this, reply, middlewaresSnapshot);
    runReplyCreatedMiddlewares(reply, middlewaresSnapshot);

    if (!device || !device->isWritable()) {
        qWarning() << "downloadToDevice: device is null or not writable";
        QMetaObject::invokeMethod(
            reply,
            [reply]() {
                reply->abortWithError(NetworkError::InvalidRequest,
                                      QStringLiteral(
                                          "downloadToDevice: 目标 QIODevice 为空或不可写"));
            },
            Qt::QueuedConnection);
        return reply;
    }

    if (device->thread() != thread()) {
        qWarning() << "downloadToDevice: device thread mismatch. deviceThread=" << device->thread()
                   << "managerThread=" << thread();
        QMetaObject::invokeMethod(
            reply,
            [reply]() {
                reply
                    ->abortWithError(NetworkError::InvalidRequest,
                                     QStringLiteral(
                                         "downloadToDevice: 目标 QIODevice 与 Reply 不在同一线程"));
            },
            Qt::QueuedConnection);
        return reply;
    }

    QPointer<QIODevice> safeDevice(device);

    QObject::connect(device, &QObject::destroyed, reply, [reply]() {
        reply->abortWithError(NetworkError::InvalidRequest,
                              QStringLiteral("downloadToDevice: 目标 QIODevice 在传输中被销毁"));
    });

    // 连接 readyRead 信号实现流式写入
    QObject::connect(reply, &QCNetworkReply::readyRead, reply, [reply, safeDevice]() {
        if (!safeDevice) {
            reply->abortWithError(NetworkError::InvalidRequest,
                                  QStringLiteral(
                                      "downloadToDevice: 目标 QIODevice 在传输中被销毁"));
            return;
        }

        if (!safeDevice->isWritable()) {
            reply->abortWithError(NetworkError::InvalidRequest,
                                  QStringLiteral("downloadToDevice: 目标 QIODevice 已不可写"));
            return;
        }

        auto data = reply->readAll();
        if (data.has_value() && !data->isEmpty()) {
            const QByteArray &buf = data.value();
            qint64 totalWritten   = 0;
            while (totalWritten < buf.size()) {
                const qint64 written = safeDevice->write(buf.constData() + totalWritten,
                                                         buf.size() - totalWritten);
                if (written <= 0) {
                    reply->abortWithError(NetworkError::InvalidRequest,
                                          QStringLiteral("downloadToDevice: 写入目标设备失败: %1")
                                              .arg(safeDevice->errorString()));
                    return;
                }
                totalWritten += written;
            }
        }
    });

    reply->execute(); // 自动启动请求
    return reply;
}

QCNetworkReply *QCNetworkAccessManager::uploadFromDevice(const QUrl &url,
                                                         const QString &fieldName,
                                                         QIODevice *device,
                                                         const QString &fileName,
                                                         const QString &mimeType)
{
    Q_D(QCNetworkAccessManager);
    const auto middlewaresSnapshot = d->middlewares;

    if (!device || !device->isReadable()) {
        qWarning() << "uploadFromDevice: device is null or not readable";
        QCNetworkRequest request(url);
        const QCNetworkRequest modifiedRequest = applyRequestPreSendMiddlewares(request,
                                                                                middlewaresSnapshot);
        auto *reply                            = new QCNetworkReply(modifiedRequest,
                                         HttpMethod::Post,
                                         ExecutionMode::Async,
                                         QByteArray(),
                                         this);

        // Cookie 配置传递
        if (m_cookieModeFlag != NotOpen && !m_cookieFilePath.isEmpty()) {
            reply->d_func()->cookieFilePath = m_cookieFilePath;
            reply->d_func()->cookieMode     = m_cookieModeFlag;
        }

        wireResponseMiddlewares(this, reply, middlewaresSnapshot);
        runReplyCreatedMiddlewares(reply, middlewaresSnapshot);

        QMetaObject::invokeMethod(
            reply,
            [reply]() {
                reply->abortWithError(NetworkError::InvalidRequest,
                                      QStringLiteral(
                                          "uploadFromDevice: 源 QIODevice 为空或不可读"));
            },
            Qt::QueuedConnection);
        return reply;
    }

    if (device->thread() != thread()) {
        qWarning() << "uploadFromDevice: device thread mismatch. deviceThread=" << device->thread()
                   << "managerThread=" << thread();
        QCNetworkRequest request(url);
        const QCNetworkRequest modifiedRequest = applyRequestPreSendMiddlewares(request,
                                                                                middlewaresSnapshot);
        auto *reply                            = new QCNetworkReply(modifiedRequest,
                                         HttpMethod::Post,
                                         ExecutionMode::Async,
                                         QByteArray(),
                                         this);

        // Cookie 配置传递
        if (m_cookieModeFlag != NotOpen && !m_cookieFilePath.isEmpty()) {
            reply->d_func()->cookieFilePath = m_cookieFilePath;
            reply->d_func()->cookieMode     = m_cookieModeFlag;
        }

        wireResponseMiddlewares(this, reply, middlewaresSnapshot);
        runReplyCreatedMiddlewares(reply, middlewaresSnapshot);

        QMetaObject::invokeMethod(
            reply,
            [reply]() {
                reply->abortWithError(NetworkError::InvalidRequest,
                                      QStringLiteral(
                                          "uploadFromDevice: 源 QIODevice 与 Reply 不在同一线程"));
            },
            Qt::QueuedConnection);
        return reply;
    }

    QCMultipartFormData formData;

    if (!formData.addFileFieldStream(fieldName, device, fileName, mimeType)) {
        qWarning() << "uploadFromDevice: cannot add stream field to form";
        QCNetworkRequest request(url);
        const QCNetworkRequest modifiedRequest = applyRequestPreSendMiddlewares(request,
                                                                                middlewaresSnapshot);
        auto *reply                            = new QCNetworkReply(modifiedRequest,
                                         HttpMethod::Post,
                                         ExecutionMode::Async,
                                         QByteArray(),
                                         this);

        // Cookie 配置传递
        if (m_cookieModeFlag != NotOpen && !m_cookieFilePath.isEmpty()) {
            reply->d_func()->cookieFilePath = m_cookieFilePath;
            reply->d_func()->cookieMode     = m_cookieModeFlag;
        }

        wireResponseMiddlewares(this, reply, middlewaresSnapshot);
        runReplyCreatedMiddlewares(reply, middlewaresSnapshot);

        QMetaObject::invokeMethod(
            reply,
            [reply]() {
                reply
                    ->abortWithError(NetworkError::InvalidRequest,
                                     QStringLiteral(
                                         "uploadFromDevice: 无法从 QIODevice 构建 multipart 表单"));
            },
            Qt::QueuedConnection);
        return reply;
    }

    // 使用 postMultipart 发送
    return postMultipart(url, formData);
}

QCNetworkReply *QCNetworkAccessManager::downloadFileResumable(const QUrl &url,
                                                              const QString &savePath,
                                                              bool overwrite)
{
    // 检查文件是否存在，用于断点续传
    QFile file(savePath);
    qint64 existingSize = 0;

    if (!overwrite && file.exists()) {
        existingSize = file.size();
        qDebug() << "File exists, resuming download from byte:" << existingSize;
    }

    QCNetworkRequest request(url);

    // 断点续传：设置 Range 头（RFC 7233）
    if (existingSize > 0) {
        QString rangeHeader = QStringLiteral("bytes=%1-").arg(existingSize);
        request.setRawHeader("Range", rangeHeader.toUtf8());
    }

    QCNetworkReply *reply = sendGet(request);

    // 使用 QSharedPointer 管理文件句柄生命周期
    auto filePtr = QSharedPointer<QFile>::create(savePath);

    QObject::connect(reply, &QCNetworkReply::readyRead, reply, [reply, filePtr, existingSize]() {
        if (!filePtr->isOpen()) {
            QIODevice::OpenMode mode = existingSize > 0 ? QIODevice::Append : QIODevice::WriteOnly;

            if (!filePtr->open(mode)) {
                qWarning() << "Cannot open file:" << filePtr->fileName();
                reply->cancel();
                return;
            }
        }

        auto data = reply->readAll();
        if (data.has_value()) {
            filePtr->write(data.value());
            filePtr->flush();
        }
    });

    QObject::connect(reply, &QCNetworkReply::finished, reply, [reply, filePtr, savePath]() {
        if (filePtr->isOpen()) {
            filePtr->close();
        }

        if (reply->error() == NetworkError::NoError) {
            qDebug() << "Download completed:" << savePath;
        } else {
            qWarning() << "Download failed:" << reply->errorString();
        }
    });

    return reply;
}

// ==================
// 缓存管理实现
// ==================

void QCNetworkAccessManager::setCache(QCNetworkCache *cache)
{
    m_cache = cache;
}

QCNetworkCache *QCNetworkAccessManager::cache() const
{
    return m_cache;
}

void QCNetworkAccessManager::setCachePath(const QString &path, qint64 maxSize)
{
    auto *diskCache = new QCNetworkDiskCache(this);
    diskCache->setCacheDirectory(path);
    diskCache->setMaxCacheSize(maxSize);
    m_cache = diskCache;
}

// ==================
// 日志系统
// ==================

void QCNetworkAccessManager::setLogger(QCNetworkLogger *logger)
{
    Q_D(QCNetworkAccessManager);
    d->logger = logger;
}

QCNetworkLogger *QCNetworkAccessManager::logger() const
{
    Q_D(const QCNetworkAccessManager);
    return d->logger;
}

void QCNetworkAccessManager::setDebugTraceEnabled(bool enabled)
{
    Q_D(QCNetworkAccessManager);
    d->debugTraceEnabled = enabled;
}

bool QCNetworkAccessManager::debugTraceEnabled() const noexcept
{
    Q_D(const QCNetworkAccessManager);
    return d->debugTraceEnabled;
}

// ==================
// 中间件系统
// ==================

void QCNetworkAccessManager::addMiddleware(QCNetworkMiddleware *middleware)
{
    Q_D(QCNetworkAccessManager);
    if (middleware && !d->middlewares.contains(middleware)) {
        d->middlewares.append(middleware);
    }
}

void QCNetworkAccessManager::removeMiddleware(QCNetworkMiddleware *middleware)
{
    Q_D(QCNetworkAccessManager);
    d->middlewares.removeAll(middleware);
}

void QCNetworkAccessManager::clearMiddlewares()
{
    Q_D(QCNetworkAccessManager);
    d->middlewares.clear();
}

QList<QCNetworkMiddleware *> QCNetworkAccessManager::middlewares() const
{
    Q_D(const QCNetworkAccessManager);
    return d->middlewares;
}

// ==================
// 流式构建器
// ==================

QCNetworkRequestBuilder QCNetworkAccessManager::newRequest(const QUrl &url)
{
    return QCNetworkRequestBuilder(this, url);
}

// ==================
// Mock 工具
// ==================

void QCNetworkAccessManager::setMockHandler(QCNetworkMockHandler *handler)
{
    Q_D(QCNetworkAccessManager);
    d->mockHandler = handler;
}

QCNetworkMockHandler *QCNetworkAccessManager::mockHandler() const
{
    Q_D(const QCNetworkAccessManager);
    return d->mockHandler;
}

} // namespace QCurl
