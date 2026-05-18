#include "QCNetworkAccessManager.h"

#include "QCCurlMultiManager.h"
#include "QCNetworkAccessManager_p.h"

#include <QAbstractEventDispatcher>
#include <QMetaObject>
#include <QThread>

namespace {

bool hasEventDispatcher(QThread *thread)
{
    return (thread != nullptr) && (QAbstractEventDispatcher::instance(thread) != nullptr);
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

std::optional<QList<QNetworkCookie>> QCNetworkAccessManager::exportCookies(const QUrl &filterUrl,
                                                                           QString *error) const
{
    if (QThread::currentThread() != thread()) {
        if (!hasEventDispatcher(thread())) {
            if (error) {
                *error = QStringLiteral("QCNetworkAccessManager::exportCookies: manager "
                                        "所在线程无事件循环，无法跨线程提交");
            }
            return std::nullopt;
        }

        std::optional<QList<QNetworkCookie>> cookies;
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
        return std::nullopt;
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
