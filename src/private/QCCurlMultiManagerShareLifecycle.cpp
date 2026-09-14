#include "QCCurlMultiManager.h"
#include "QCNetworkAccessManager.h"
#include "QCNetworkReply.h"
#include "QCNetworkReply_p.h"
#include "private/QCCurlRequiredOptionAdapter_p.h"

#include <QByteArray>

namespace QCurl {

namespace {

/**
 * @brief 执行 share cleanup，并为生命周期合同提供确定性失败注入。
 *
 * @param share 待清理的 share handle
 * @return libcurl cleanup 结果；两个注入场景均返回 CURLSHE_IN_USE
 */
[[nodiscard]] CURLSHcode cleanupShareHandleForLifecycle(CURLSH *share)
{
#ifdef QCURL_ENABLE_TEST_HOOKS
    const QByteArray forcedError = qgetenv("QCURL_TEST_FORCE_SHARE_CLEANUP_ERROR").trimmed();
    if (forcedError == QByteArrayLiteral("cleanup-in-use")
        || forcedError == QByteArrayLiteral("shutdown-cleanup-failure")) {
        return CURLSHE_IN_USE;
    }
#endif
    return curl_share_cleanup(share);
}

} // namespace

/**
 * @brief 按 libcurl 返回码清理 context 持有的 share handle。
 *
 * @param context 待清理的 share context
 * @param operation 失败诊断使用的稳定操作名
 * @return 无 handle 或 cleanup 成功返回 true；失败返回 false
 *
 * @note 必须持有 m_mutex；失败时不清空 handle 或 applied 状态。
 */
bool QCCurlMultiManager::cleanupShareHandleLocked(ShareContext *context, const char *operation)
{
    if (!context || !context->share) {
        return true;
    }
    if (context->activeUsers != 0) {
        poisonLocked(operation, CURLM_INTERNAL_ERROR);
        return false;
    }

    const CURLSHcode result = cleanupShareHandleForLifecycle(context->share);
    if (result != CURLSHE_OK) {
        poisonLocked(operation, CURLM_INTERNAL_ERROR);
        return false;
    }
    context->share   = nullptr;
    context->applied = {};
    return true;
}

/**
 * @brief 先解绑 easy 的 share option，再提交映射与引用计数变更。
 *
 * @param easy 待解绑的 easy handle
 * @param operation 失败诊断使用的稳定操作名
 * @return 无绑定或解绑完成返回 true；失败并 poison 时返回 false
 *
 * @note 必须持有 m_mutex；解绑失败时保留全部绑定状态。
 */
bool QCCurlMultiManager::detachShareBindingLocked(CURL *easy, const char *operation)
{
    auto bindingIt       = m_easyToShareContext.find(easy);
    auto optionIt        = m_easyShareOptionSet.find(easy);
    const bool optionSet = optionIt != m_easyShareOptionSet.end() && optionIt.value();
    if (bindingIt == m_easyToShareContext.end() && !optionSet) {
        return true;
    }
    if (bindingIt == m_easyToShareContext.end() || !optionSet || !bindingIt.value()
        || bindingIt.value()->activeUsers <= 0) {
        poisonLocked(operation, CURLM_INTERNAL_ERROR);
        return false;
    }

    Internal::CookieOptionAdapter adapter;
    const Internal::RequiredOptionResult result
        = adapter.setEasy(easy,
                          QCURL_CURL_OPTION(CURLOPT_SHARE),
                          Internal::CookieOptionStage::Rollback,
                          nullptr);
    if (!result.isSuccess()) {
        poisonLocked(operation, CURLM_INTERNAL_ERROR);
        return false;
    }

    ShareContext *context = bindingIt.value();
    m_easyToShareContext.erase(bindingIt);
    m_easyShareOptionSet.erase(optionIt);
    context->activeUsers -= 1;
    maybeFinalizeShareContextLocked(context);
    return !m_isPoisoned.load(std::memory_order_relaxed);
}

QCCurlMultiManager::ShareConfig QCCurlMultiManager::toShareConfig(
    const QCNetworkAccessManager *manager)
{
    ShareConfig out;
    if (!manager) {
        return out;
    }

    const auto config = manager->shareHandleConfig();
    out.dnsCache      = config.shareDnsCache();
    out.cookies       = config.shareCookies();
    out.sslSession    = config.shareSslSession();
    return out;
}

void QCCurlMultiManager::maybeFinalizeShareContextLocked(ShareContext *context)
{
    if (!context || context->activeUsers != 0) {
        return;
    }

    if (context->pending.has_value()) {
        QString error;
        if (!applyShareConfigIfIdleLocked(context, context->pending.value(), &error)
            && m_isPoisoned.load(std::memory_order_relaxed)) {
            return;
        }
    }

    if (!context->pendingDelete) {
        return;
    }

    if (!cleanupShareHandleLocked(
            context, "QCCurlMultiManager::maybeFinalizeShareContextLocked/share-cleanup")) {
        return;
    }
    m_shareContexts.remove(context->scopeKey);
}

QCCurlMultiManager::ShareContext *QCCurlMultiManager::prepareShareForReplyLocked(
    QCNetworkReply *reply, CURL *)
{
    auto *accessManager                  = qobject_cast<QCNetworkAccessManager *>(reply->parent());
    const ShareConfig desiredShareConfig = toShareConfig(accessManager);
    if (!accessManager || !desiredShareConfig.enabled()) {
        return nullptr;
    }

    ShareContext *shareContext = getOrCreateShareContextLocked(accessManager);
    if (!shareContext || shareContext->pendingDelete) {
        reply->d_func()->capabilityWarnings.append(
            QStringLiteral("share handle 作用域已销毁，已降级为不共享缓存"));
        return shareContext;
    }

    if (shareContext->cookieStorePoisoned) {
        reply->d_func()->capabilityWarnings.append(
            QStringLiteral("share cookie context 已 poisoned，拒绝继续使用不可信共享状态"));
        return nullptr;
    }

    if (desiredShareConfig == shareContext->applied) {
        return shareContext;
    }

    if (shareContext->activeUsers > 0) {
        shareContext->pending = desiredShareConfig;
        reply->d_func()->capabilityWarnings.append(
            QStringLiteral("share handle 配置变更延迟生效：当前仍按 %1 生效，"
                           "待在途请求结束后切换为 %2")
                .arg(shareConfigSummary(shareContext->applied))
                .arg(shareConfigSummary(desiredShareConfig)));
        return shareContext;
    }

    QString initError;
    if (!applyShareConfigIfIdleLocked(shareContext, desiredShareConfig, &initError)) {
        const QString reason = initError.isEmpty() ? shareContext->lastInitError : initError;
        reply->d_func()->capabilityWarnings.append(
            QStringLiteral("share handle 不可用（%1），已降级为不共享缓存")
                .arg(reason.isEmpty() ? QStringLiteral("unknown") : reason));
    } else if (shareContext->applied != desiredShareConfig) {
        reply->d_func()->capabilityWarnings.append(
            QStringLiteral("share handle 降级：期望 %1，但实际仅启用 %2")
                .arg(shareConfigSummary(desiredShareConfig))
                .arg(shareConfigSummary(shareContext->applied)));
    }
    return shareContext;
}

QCCurlMultiManager::ShareApplyResult QCCurlMultiManager::applyShareToEasyLocked(
    QCNetworkReply *reply, CURL *easy, ShareContext *shareContext)
{
    if (m_isPoisoned.load(std::memory_order_relaxed)) {
        return {ShareApplyState::NonCallable, m_initializationError};
    }
    if (shareContext && shareContext->cookieStorePoisoned) {
        reply->d_func()->capabilityWarnings.append(
            QStringLiteral("share cookie context 已 poisoned，未向请求绑定共享状态"));
        resetShareOnEasyIfNeeded(easy);
        return m_isPoisoned.load(std::memory_order_relaxed)
                   ? ShareApplyResult{ShareApplyState::NonCallable, m_initializationError}
                   : ShareApplyResult{};
    }
    if (!shareContext || !shareContext->share || !shareContext->applied.enabled()) {
        resetShareOnEasyIfNeeded(easy);
        return m_isPoisoned.load(std::memory_order_relaxed)
                   ? ShareApplyResult{ShareApplyState::NonCallable, m_initializationError}
                   : ShareApplyResult{};
    }

    Internal::CookieOptionAdapter adapter;
    const Internal::RequiredOptionResult easyShare
        = adapter.setEasy(easy,
                          QCURL_CURL_OPTION(CURLOPT_SHARE),
                          Internal::CookieOptionStage::Setup,
                          shareContext->share);
    const CURLcode rc = easyShare.curlCode;
    if (rc != CURLE_OK) {
        reply->d_func()->capabilityWarnings.append(
            QStringLiteral("设置 CURLOPT_SHARE 失败（%1），已降级为不共享缓存")
                .arg(QString::fromUtf8(curl_easy_strerror(rc))));
        return {};
    }

    shareContext->activeUsers += 1;
    m_easyToShareContext.insert(easy, shareContext);
    m_easyShareOptionSet.insert(easy, true);

    auto *d                 = reply->d_func();
    const bool hasCookieJar = (d->cookieMode != 0) && !d->cookieFilePath.isEmpty();
    if (shareContext->applied.cookies && !hasCookieJar) {
        const Internal::RequiredOptionResult cookieEngine
            = adapter.setEasy(easy,
                              QCURL_CURL_OPTION(CURLOPT_COOKIEFILE),
                              Internal::CookieOptionStage::Setup,
                              "");
        if (!cookieEngine.isSuccess()) {
            reply->d_func()->capabilityWarnings.append(
                QStringLiteral("启用共享 cookie engine 失败（%1）").arg(cookieEngine.message));
            if (!detachShareBindingLocked(
                    easy, "QCCurlMultiManager::applyShareToEasyLocked/cookie-engine-rollback")) {
                return {ShareApplyState::NonCallable, m_initializationError};
            }
        }
    }
    return {};
}

void QCCurlMultiManager::resetShareOnEasyIfNeeded(CURL *easy)
{
    auto it = m_easyShareOptionSet.find(easy);
    if (it == m_easyShareOptionSet.end() || !it.value()) {
        return;
    }

    static_cast<void>(
        detachShareBindingLocked(easy, "QCCurlMultiManager::resetShareOnEasyIfNeeded"));
}

} // namespace QCurl
