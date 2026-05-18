#include "QCCurlMultiManager.h"

#include "QCNetworkAccessManager.h"

#include <QDebug>
#include <QMutexLocker>

namespace QCurl {

void QCCurlMultiManager::shareLockCallback(CURL *,
                                           curl_lock_data data,
                                           curl_lock_access,
                                           void *userptr)
{
    auto *context = static_cast<ShareContext *>(userptr);
    if (!context) {
        return;
    }

    switch (data) {
        case CURL_LOCK_DATA_DNS:
            context->dnsMutex.lock();
            return;
        case CURL_LOCK_DATA_COOKIE:
            context->cookieMutex.lock();
            return;
        case CURL_LOCK_DATA_SSL_SESSION:
            context->sslMutex.lock();
            return;
        default:
            context->otherMutex.lock();
            return;
    }
}

void QCCurlMultiManager::shareUnlockCallback(CURL *, curl_lock_data data, void *userptr)
{
    auto *context = static_cast<ShareContext *>(userptr);
    if (!context) {
        return;
    }

    switch (data) {
        case CURL_LOCK_DATA_DNS:
            context->dnsMutex.unlock();
            return;
        case CURL_LOCK_DATA_COOKIE:
            context->cookieMutex.unlock();
            return;
        case CURL_LOCK_DATA_SSL_SESSION:
            context->sslMutex.unlock();
            return;
        default:
            context->otherMutex.unlock();
            return;
    }
}

QString QCCurlMultiManager::shareConfigSummary(const ShareConfig &config)
{
    if (!config.enabled()) {
        return QStringLiteral("关闭");
    }

    QStringList parts;
    if (config.dnsCache) {
        parts.append(QStringLiteral("DNS"));
    }
    if (config.cookies) {
        parts.append(QStringLiteral("Cookie"));
    }
    if (config.sslSession) {
        parts.append(QStringLiteral("SSL session"));
    }
    return parts.join(QStringLiteral(","));
}

QCCurlMultiManager::ShareContext *QCCurlMultiManager::getOrCreateShareContextLocked(
    const QCNetworkAccessManager *manager)
{
    if (!manager) {
        return nullptr;
    }

    auto it = m_shareContexts.find(manager);
    if (it != m_shareContexts.end()) {
        return it.value().data();
    }

    auto context             = QSharedPointer<ShareContext>::create();
    context->scopeKey        = manager;
    ShareContext *contextPtr = context.data();
    m_shareContexts.insert(manager, context);

    QObject::connect(const_cast<QCNetworkAccessManager *>(manager),
                     &QObject::destroyed,
                     this,
                     [this, manager]() {
                         QMutexLocker locker(&m_mutex);
                         onAccessManagerDestroyedLocked(manager);
                     });

    return contextPtr;
}

void QCCurlMultiManager::onAccessManagerDestroyedLocked(const QCNetworkAccessManager *manager)
{
    auto it = m_shareContexts.find(manager);
    if (it == m_shareContexts.end()) {
        return;
    }

    ShareContext *context = it.value().data();
    if (!context) {
        m_shareContexts.erase(it);
        return;
    }

    context->scopeDestroyed = true;
    context->pendingDelete  = true;

    if (context->activeUsers == 0) {
        if (context->share) {
            curl_share_cleanup(context->share);
            context->share = nullptr;
        }
        m_shareContexts.erase(it);
        return;
    }

    context->pending = ShareConfig{};
}

bool QCCurlMultiManager::applyShareConfigIfIdleLocked(ShareContext *context,
                                                      const ShareConfig &desired,
                                                      QString *error)
{
    if (!context || context->activeUsers != 0) {
        if (error) {
            *error = QStringLiteral("share handle 正在使用中，无法切换配置");
        }
        return false;
    }

    context->pending.reset();

    if (!desired.enabled()) {
        if (context->share) {
            curl_share_cleanup(context->share);
            context->share = nullptr;
        }
        context->applied         = ShareConfig{};
        context->lastInitAttempt = desired;
        context->lastInitFailed  = false;
        context->lastInitError.clear();
        return true;
    }

    if (!context->share && context->lastInitFailed && context->lastInitAttempt == desired) {
        if (error) {
            *error = context->lastInitError;
        }
        return false;
    }

    if (context->share) {
        curl_share_cleanup(context->share);
        context->share = nullptr;
    }

    context->applied         = ShareConfig{};
    context->lastInitAttempt = desired;
    context->lastInitFailed  = false;
    context->lastInitError.clear();

    CURLSH *share = curl_share_init();
    if (!share) {
        context->lastInitFailed = true;
        context->lastInitError  = QStringLiteral("curl_share_init 失败");
        if (error) {
            *error = context->lastInitError;
        }
        return false;
    }

    auto failHard = [context, share, error](const QString &message) {
        curl_share_cleanup(share);
        context->lastInitFailed = true;
        context->lastInitError  = message;
        if (error) {
            *error = message;
        }
    };

    CURLSHcode rc = curl_share_setopt(share, CURLSHOPT_USERDATA, context);
    if (rc != CURLSHE_OK) {
        failHard(QStringLiteral("设置 CURLSHOPT_USERDATA 失败（%1）")
                     .arg(QString::fromUtf8(curl_share_strerror(rc))));
        return false;
    }

    rc = curl_share_setopt(share, CURLSHOPT_LOCKFUNC, shareLockCallback);
    if (rc != CURLSHE_OK) {
        failHard(QStringLiteral("设置 CURLSHOPT_LOCKFUNC 失败（%1）")
                     .arg(QString::fromUtf8(curl_share_strerror(rc))));
        return false;
    }

    rc = curl_share_setopt(share, CURLSHOPT_UNLOCKFUNC, shareUnlockCallback);
    if (rc != CURLSHE_OK) {
        failHard(QStringLiteral("设置 CURLSHOPT_UNLOCKFUNC 失败（%1）")
                     .arg(QString::fromUtf8(curl_share_strerror(rc))));
        return false;
    }

    if (desired.dnsCache) {
        rc = curl_share_setopt(share, CURLSHOPT_SHARE, CURL_LOCK_DATA_DNS);
        if (rc == CURLSHE_OK) {
            context->applied.dnsCache = true;
        }
    }

    if (desired.cookies) {
        rc = curl_share_setopt(share, CURLSHOPT_SHARE, CURL_LOCK_DATA_COOKIE);
        if (rc == CURLSHE_OK) {
            context->applied.cookies = true;
        }
    }

    if (desired.sslSession) {
        rc = curl_share_setopt(share, CURLSHOPT_SHARE, CURL_LOCK_DATA_SSL_SESSION);
        if (rc == CURLSHE_OK) {
            context->applied.sslSession = true;
        }
    }

    if (!context->applied.enabled()) {
        failHard(
            QStringLiteral("curl_share_setopt(CURLSHOPT_SHARE) 全部失败，share handle 不可用"));
        return false;
    }

    context->share = share;
    return true;
}

void QCCurlMultiManager::releaseShareForEasyHandleLocked(CURL *easy)
{
    auto it = m_easyToShareContext.find(easy);
    if (it == m_easyToShareContext.end()) {
        return;
    }

    ShareContext *context = it.value();
    m_easyToShareContext.erase(it);

    if (context && context->activeUsers > 0) {
        context->activeUsers -= 1;
    }

    if (context) {
        maybeFinalizeShareContextLocked(context);
    }
}

void QCCurlMultiManager::cleanupEasyHandleLocked(CURL *easy,
                                                 bool removeFromMulti,
                                                 bool decrementRunningCount,
                                                 const char *context)
{
    if (!easy) {
        return;
    }

    if (removeFromMulti && m_multiHandle) {
        const CURLMcode ret = curl_multi_remove_handle(m_multiHandle, easy);
        if (ret != CURLM_OK && ret != CURLM_BAD_EASY_HANDLE) {
            qWarning() << context << ": curl_multi_remove_handle failed:" << ret;
        }
    }

    const bool removedReply = m_activeReplies.remove(easy) > 0;
    if (removedReply && decrementRunningCount) {
        m_runningRequests.fetch_sub(1, std::memory_order_relaxed);
    }

    releaseShareForEasyHandleLocked(easy);
}

} // namespace QCurl
