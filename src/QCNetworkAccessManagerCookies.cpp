#include "QCNetworkAccessManager.h"

#include "QCCurlMultiManager.h"
#include "QCNetworkAccessManager_p.h"

#include <QAbstractEventDispatcher>
#include <QMetaObject>
#include <QPromise>
#include <QThread>

#include <memory>

namespace {

bool hasEventDispatcher(QThread *thread)
{
    return (thread != nullptr) && (QAbstractEventDispatcher::instance(thread) != nullptr);
}

QCurl::QCCookieOperationResult cookieOperationFailure(QString error)
{
    return QCurl::QCCookieOperationResult::failure(std::move(error));
}

QCurl::QCCookieExportResult cookieExportFailure(QString error)
{
    return QCurl::QCCookieExportResult::failure(std::move(error));
}

template <typename Result>
QFuture<Result> finishedFuture(const Result &result)
{
    QPromise<Result> promise;
    promise.start();
    promise.addResult(result);
    promise.finish();
    return promise.future();
}

} // namespace

namespace QCurl {

QString QCNetworkAccessManager::cookieFilePath() const
{
    Q_D(const QCNetworkAccessManager);
    return d->cookieFilePath;
}

QCNetworkAccessManager::CookieFileModeFlag QCNetworkAccessManager::cookieFileMode() const
{
    Q_D(const QCNetworkAccessManager);
    return d->cookieModeFlag;
}

void QCNetworkAccessManager::setCookieFilePath(const QString &cookieFilePath,
                                               CookieFileModeFlag flag)
{
    Q_D(QCNetworkAccessManager);
    d->cookieFilePath = cookieFilePath;
    d->cookieModeFlag = flag;
}

QCCookieOperationResult
QCNetworkAccessManagerPrivate::importCookiesOnOwnerThread(const QList<QNetworkCookie> &cookies,
                                                          const QUrl &originUrl)
{
    auto *multi = QCCurlMultiManager::instance();
    if (!multi) {
        return cookieOperationFailure(QStringLiteral("QCCurlMultiManager 不可用"));
    }
    QString localError;
    const bool ok = multi->importCookiesForManager(q_func(), cookies, originUrl, &localError);
    return ok ? QCCookieOperationResult::success()
              : QCCookieOperationResult::failure(std::move(localError));
}

QCCookieExportResult
QCNetworkAccessManagerPrivate::exportCookiesOnOwnerThread(const QUrl &filterUrl) const
{
    auto *multi = QCCurlMultiManager::instance();
    if (!multi) {
        return cookieExportFailure(QStringLiteral("QCCurlMultiManager 不可用"));
    }
    QString localError;
    auto *manager = const_cast<QCNetworkAccessManager *>(q_func());
    const auto cookies = multi->exportCookiesForManager(manager, filterUrl, &localError);
    if (!cookies.has_value()) {
        return cookieExportFailure(localError);
    }
    return QCCookieExportResult::success(cookies.value());
}

QCCookieOperationResult
QCNetworkAccessManagerPrivate::clearAllCookiesOnOwnerThread()
{
    auto *multi = QCCurlMultiManager::instance();
    if (!multi) {
        return cookieOperationFailure(QStringLiteral("QCCurlMultiManager 不可用"));
    }
    QString localError;
    const bool ok = multi->clearAllCookiesForManager(q_func(), &localError);
    return ok ? QCCookieOperationResult::success()
              : QCCookieOperationResult::failure(std::move(localError));
}

QFuture<QCCookieOperationResult>
QCNetworkAccessManagerPrivate::runCookieOperationAsync(
    QCNetworkAccessManager *manager,
    std::function<QCCookieOperationResult()> command,
    void (QCNetworkAccessManager::*signal)(const QCCookieOperationResult &))
{
    if (!manager || !hasEventDispatcher(manager->thread())) {
        const auto result =
            cookieOperationFailure(QStringLiteral("QCNetworkAccessManager: owner 线程缺少 Qt "
                                                 "事件循环，无法执行 cookie async bridge"));
        if (manager) {
            Q_EMIT (manager->*signal)(result);
        }
        return finishedFuture(result);
    }

    auto promise = std::make_shared<QPromise<QCCookieOperationResult>>();
    promise->start();
    auto future = promise->future();
    const bool invoked = QMetaObject::invokeMethod(
        manager,
        [manager, promise, command = std::move(command), signal]() mutable {
            const auto result = command();
            promise->addResult(result);
            promise->finish();
            Q_EMIT (manager->*signal)(result);
        },
        Qt::QueuedConnection);
    if (!invoked) {
        const auto result =
            cookieOperationFailure(QStringLiteral("QCNetworkAccessManager: 无法提交 cookie async "
                                                 "bridge 到 owner 线程"));
        promise->addResult(result);
        promise->finish();
        Q_EMIT (manager->*signal)(result);
    }
    return future;
}

QFuture<QCCookieExportResult> QCNetworkAccessManagerPrivate::runCookieExportAsync(
    QCNetworkAccessManager *manager,
    std::function<QCCookieExportResult()> command)
{
    if (!manager || !hasEventDispatcher(manager->thread())) {
        const auto result =
            cookieExportFailure(QStringLiteral("QCNetworkAccessManager: owner 线程缺少 Qt "
                                              "事件循环，无法执行 cookie async bridge"));
        if (manager) {
            Q_EMIT manager->cookiesExported(result);
        }
        return finishedFuture(result);
    }

    auto promise = std::make_shared<QPromise<QCCookieExportResult>>();
    promise->start();
    auto future = promise->future();
    const bool invoked = QMetaObject::invokeMethod(
        manager,
        [manager, promise, command = std::move(command)]() mutable {
            const auto result = command();
            promise->addResult(result);
            promise->finish();
            Q_EMIT manager->cookiesExported(result);
        },
        Qt::QueuedConnection);
    if (!invoked) {
        const auto result =
            cookieExportFailure(QStringLiteral("QCNetworkAccessManager: 无法提交 cookie async "
                                              "bridge 到 owner 线程"));
        promise->addResult(result);
        promise->finish();
        Q_EMIT manager->cookiesExported(result);
    }
    return future;
}

bool QCNetworkAccessManager::importCookies(const QList<QNetworkCookie> &cookies,
                                           const QUrl &originUrl,
                                           QString *error)
{
    if (QThread::currentThread() != thread()) {
        if (error) {
            *error = QStringLiteral("QCNetworkAccessManager::importCookies: 只允许在 owner "
                                    "thread 调用；跨线程请使用 importCookiesAsync()");
        }
        return false;
    }

    Q_D(QCNetworkAccessManager);
    const auto result = d->importCookiesOnOwnerThread(cookies, originUrl);
    if (error) {
        *error = result.error();
    }
    return result.isSuccess();
}

std::optional<QList<QNetworkCookie>> QCNetworkAccessManager::exportCookies(const QUrl &filterUrl,
                                                                           QString *error) const
{
    if (QThread::currentThread() != thread()) {
        if (error) {
            *error = QStringLiteral("QCNetworkAccessManager::exportCookies: 只允许在 owner "
                                    "thread 调用；跨线程请使用 exportCookiesAsync()");
        }
        return std::nullopt;
    }

    Q_D(const QCNetworkAccessManager);
    const auto result = d->exportCookiesOnOwnerThread(filterUrl);
    if (error) {
        *error = result.error();
    }
    if (!result.isSuccess()) {
        return std::nullopt;
    }
    return result.cookies();
}

bool QCNetworkAccessManager::clearAllCookies(QString *error)
{
    if (QThread::currentThread() != thread()) {
        if (error) {
            *error = QStringLiteral("QCNetworkAccessManager::clearAllCookies: 只允许在 owner "
                                    "thread 调用；跨线程请使用 clearAllCookiesAsync()");
        }
        return false;
    }

    Q_D(QCNetworkAccessManager);
    const auto result = d->clearAllCookiesOnOwnerThread();
    if (error) {
        *error = result.error();
    }
    return result.isSuccess();
}

QFuture<QCCookieOperationResult>
QCNetworkAccessManager::importCookiesAsync(const QList<QNetworkCookie> &cookies,
                                           const QUrl &originUrl)
{
    Q_D(QCNetworkAccessManager);
    return d->runCookieOperationAsync(
        this,
        [d, cookies, originUrl]() { return d->importCookiesOnOwnerThread(cookies, originUrl); },
        &QCNetworkAccessManager::cookiesImported);
}

QFuture<QCCookieExportResult>
QCNetworkAccessManager::exportCookiesAsync(const QUrl &filterUrl) const
{
    Q_D(const QCNetworkAccessManager);
    auto *manager = const_cast<QCNetworkAccessManager *>(this);
    auto *mutableD = const_cast<QCNetworkAccessManagerPrivate *>(d);
    return mutableD->runCookieExportAsync(
        manager,
        [d, filterUrl]() { return d->exportCookiesOnOwnerThread(filterUrl); });
}

QFuture<QCCookieOperationResult>
QCNetworkAccessManager::clearAllCookiesAsync()
{
    Q_D(QCNetworkAccessManager);
    return d->runCookieOperationAsync(
        this,
        [d]() { return d->clearAllCookiesOnOwnerThread(); },
        &QCNetworkAccessManager::cookiesCleared);
}

void QCNetworkAccessManager::setShareHandleConfig(const ShareHandleConfig &config)
{
    Q_D(QCNetworkAccessManager);
    d->shareHandleConfig = config;
}

QCNetworkAccessManager::ShareHandleConfig QCNetworkAccessManager::shareHandleConfig() const noexcept
{
    Q_D(const QCNetworkAccessManager);
    return d->shareHandleConfig;
}

void QCNetworkAccessManager::setHstsAltSvcCacheConfig(const HstsAltSvcCacheConfig &config)
{
    Q_D(QCNetworkAccessManager);
    d->hstsAltSvcCacheConfig = config;
}

QCNetworkAccessManager::HstsAltSvcCacheConfig QCNetworkAccessManager::hstsAltSvcCacheConfig()
    const noexcept
{
    Q_D(const QCNetworkAccessManager);
    return d->hstsAltSvcCacheConfig;
}

} // namespace QCurl
