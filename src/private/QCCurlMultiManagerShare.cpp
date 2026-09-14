#include "QCCurlMultiManager.h"
#include "QCNetworkAccessManager.h"
#include "QCNetworkReply_p.h"
#include "private/QCCookiePolicyCode_p.h"
#include "private/QCCurlMultiTransferRecord_p.h"
#include "private/QCCurlRequiredOptionAdapter_p.h"

#include <QByteArray>
#include <QDebug>
#include <QMutexLocker>

namespace QCurl {

namespace {

template<typename Context, typename Config>
void beginShareInitialization(Context *context, const Config &desired)
{
    context->applied         = Config{};
    context->lastInitAttempt = desired;
    context->lastInitFailed  = false;
    context->lastInitError.clear();
    context->lastInitResult = Internal::CookieStoreResult{};
}

template<typename Context>
[[nodiscard]] bool failShareInitialization(Context *context,
                                           QString *error,
                                           const Internal::RequiredOptionResult &failure)
{
    context->lastInitFailed            = true;
    context->lastInitError             = failure.message;
    context->lastInitResult.status     = Internal::CookieStoreStatus::RejectedBeforeMutation;
    context->lastInitResult.shareCode  = failure.shareCode;
    context->lastInitResult.optionName = failure.optionName;
    context->lastInitResult.policyCode = failure.policyCode;
    context->lastInitResult.message    = failure.message;
    if (error) {
        *error = failure.message;
    }
    return false;
}

template<typename Context, typename Value>
[[nodiscard]] bool setRequiredShareOption(Context *context,
                                          CURLSH *share,
                                          QString *error,
                                          CURLSHoption option,
                                          const char *name,
                                          const char *detailName,
                                          Value value)
{
    const Internal::RequiredOptionResult result
        = Internal::CookieOptionAdapter::setShare(share, option, name, detailName, value);
    if (result.isSuccess()) {
        return true;
    }
    return failShareInitialization(context, error, result);
}

template<typename Context, typename LockFunction, typename UnlockFunction>
[[nodiscard]] bool configureShareCallbacks(
    Context *context, CURLSH *share, QString *error, LockFunction lock, UnlockFunction unlock)
{
    return setRequiredShareOption(context,
                                  share,
                                  error,
                                  CURLSHOPT_USERDATA,
                                  "CURLSHOPT_USERDATA",
                                  nullptr,
                                  context)
           && setRequiredShareOption(context,
                                     share,
                                     error,
                                     CURLSHOPT_LOCKFUNC,
                                     "CURLSHOPT_LOCKFUNC",
                                     nullptr,
                                     lock)
           && setRequiredShareOption(context,
                                     share,
                                     error,
                                     CURLSHOPT_UNLOCKFUNC,
                                     "CURLSHOPT_UNLOCKFUNC",
                                     nullptr,
                                     unlock);
}

template<typename Context, typename Config>
[[nodiscard]] bool configureSharedData(Context *context,
                                       CURLSH *share,
                                       const Config &desired,
                                       QString *error)
{
    const auto apply = [context, share, error](bool enabled,
                                               curl_lock_data data,
                                               const char *detail,
                                               bool *target) {
        if (!enabled) {
            return true;
        }
        if (!setRequiredShareOption(
                context, share, error, CURLSHOPT_SHARE, "CURLSHOPT_SHARE", detail, data)) {
            return false;
        }
        *target = true;
        return true;
    };
    return apply(desired.dnsCache,
                 CURL_LOCK_DATA_DNS,
                 "CURL_LOCK_DATA_DNS",
                 &context->applied.dnsCache)
           && apply(desired.cookies,
                    CURL_LOCK_DATA_COOKIE,
                    "CURL_LOCK_DATA_COOKIE",
                    &context->applied.cookies)
           && apply(desired.sslSession,
                    CURL_LOCK_DATA_SSL_SESSION,
                    "CURL_LOCK_DATA_SSL_SESSION",
                    &context->applied.sslSession);
}

} // namespace

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
        if (!cleanupShareHandleLocked(
                context, "QCCurlMultiManager::onAccessManagerDestroyedLocked/share-cleanup")) {
            return;
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

    if (!cleanupShareHandleLocked(
            context, "QCCurlMultiManager::applyShareConfigIfIdleLocked/reconfigure-cleanup")) {
        if (error) {
            *error = m_initializationError;
        }
        return false;
    }
    context->pending.reset();
    if (!context->share && context->lastInitFailed && context->lastInitAttempt == desired) {
        if (error) {
            *error = context->lastInitError;
        }
        return false;
    }
    return initializeShareContextLocked(context, desired, error);
}

/**
 * @brief 初始化 context 的 share handle 与 required options。
 *
 * @param context 待初始化且由 manager 持有的 context
 * @param desired 需要启用的 share 数据类型
 * @param error 失败时写入诊断信息
 * @return 全部 required options 成功返回 true；否则返回 false
 *
 * @note 必须持有 m_mutex；cleanup 失败时保留 handle 并 poison manager。
 */
bool QCCurlMultiManager::initializeShareContextLocked(ShareContext *context,
                                                      const ShareConfig &desired,
                                                      QString *error)
{
    beginShareInitialization(context, desired);
    if (!desired.enabled()) {
        return true;
    }

    CURLSH *share = curl_share_init();
    if (!share) {
        context->lastInitFailed            = true;
        context->lastInitError             = QStringLiteral("curl_share_init 失败");
        context->lastInitResult.status     = Internal::CookieStoreStatus::RejectedBeforeMutation;
        context->lastInitResult.policyCode = QCurl::Internal::cookiepolicy::kShareInitFailed;
        context->lastInitResult.message    = context->lastInitError;
        if (error) {
            *error = context->lastInitError;
        }
        return false;
    }
    if (!configureShareCallbacks(context, share, error, shareLockCallback, shareUnlockCallback)
        || !configureSharedData(context, share, desired, error)) {
        context->share = share;
        static_cast<void>(cleanupShareHandleLocked(
            context, "QCCurlMultiManager::initializeShareContextLocked/configuration-cleanup"));
        return false;
    }
    context->share = share;
    return true;
}

/**
 * @brief 先从 easy handle 解绑定 share，再提交 share 映射和引用计数变更。
 *
 * @param easy 待解除绑定的 easy handle
 *
 * @note 必须持有 m_mutex；解绑失败时保持所有权图并将 manager 标记为 poisoned。
 */
void QCCurlMultiManager::releaseShareForEasyHandleLocked(CURL *easy)
{
    static_cast<void>(
        detachShareBindingLocked(easy, "QCCurlMultiManager::releaseShareForEasyHandleLocked"));
}

} // namespace QCurl
