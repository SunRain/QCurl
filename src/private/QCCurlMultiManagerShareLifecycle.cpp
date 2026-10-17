#include "QCCurlMultiManager.h"
#include "QCNetworkAccessManager.h"
#include "QCNetworkReply.h"
#include "QCNetworkReply_p.h"
#include "private/QCCurlRequiredOptionAdapter_p.h"

#include <QByteArray>
#include <QDebug>
#include <QMutexLocker>
#include <QThread>

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
    if (context->hasUsers()) {
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
    out.cookies       = config.shareCookies();
    out.sslSession    = config.shareSslSession();
    return out;
}

void QCCurlMultiManager::maybeFinalizeShareContextLocked(ShareContext *context)
{
    if (!context || context->hasUsers()) {
        return;
    }

    if (!context->pendingDelete && context->pending.has_value()) {
        QString error;
        // apply 会清除 pending；必须传值拷贝，不能借用随后失效的 optional 内容。
        const auto desired = context->pending.value();
        if (!applyShareConfigIfIdleLocked(context, desired, &error)) {
            qWarning().noquote() << "share 配置应用失败：" << error;
            if (m_isPoisoned.load(std::memory_order_relaxed)) {
                return;
            }
        }
    }

    if (!context->pendingDelete) {
        return;
    }

    if (!cleanupShareHandleLocked(
            context, "QCCurlMultiManager::maybeFinalizeShareContextLocked/share-cleanup")) {
        return;
    }
    const auto current = m_shareContexts.constFind(context->scopeKey);
    if (current != m_shareContexts.cend() && current.value().data() == context) {
        m_shareContexts.remove(context->scopeKey);
    }
    m_retiredShareContexts.removeIf(
        [context](const auto &holder) { return holder.data() == context; });
}

void QCCurlMultiManager::synchronizeShareConfig(const QCNetworkAccessManager *manager)
{
    Q_ASSERT(QThread::currentThread() == thread());
    QMutexLocker locker(&m_mutex);
    const auto it = m_shareContexts.constFind(manager);
    if (it == m_shareContexts.cend()) {
        return;
    }
    ShareContext *context = it.value().data();
    const auto desired    = toShareConfig(manager);
    if (desired == context->applied) {
        context->pending.reset();
        return;
    }
    context->pending = desired;
    if (!context->hasUsers()) {
        maybeFinalizeShareContextLocked(context);
    }
}

QCCurlMultiManager::ShareContext *QCCurlMultiManager::prepareShareForReplyLocked(
    QCNetworkReply *reply, CURL *)
{
    auto *manager = qobject_cast<QCNetworkAccessManager *>(reply->parent());
    if (!manager) {
        return nullptr;
    }
    synchronizeShareConfig(manager);
    const auto desired = toShareConfig(manager);
    if (!desired.enabled()) {
        return nullptr;
    }
    ShareContext *context = getOrCreateShareContextLocked(manager);
    if (!context || context->pendingDelete || context->cookieStorePoisoned) {
        return context;
    }
    if (desired == context->applied && context->share) {
        context->pending.reset();
        return context;
    }
    if (context->hasUsers()) {
        context->pending = desired;
        reply->d_func()->capabilityWarnings.append(
            QStringLiteral("share handle 配置变更延迟生效：期望 %1，实际 %2；仍有在途用户")
                .arg(shareConfigSummary(desired), shareConfigSummary(context->applied)));
        return context;
    }
    QString error;
    if (!applyShareConfigIfIdleLocked(context, desired, &error)) {
        reply->d_func()->capabilityWarnings.append(error);
    }
    return context;
}

QCCurlMultiManager::ShareApplyResult QCCurlMultiManager::applyShareToEasyLocked(
    QCNetworkReply *reply, CURL *easy, ShareContext *context)
{
    if (m_isPoisoned.load(std::memory_order_relaxed)) {
        return {ShareApplyState::NonCallable, m_initializationError};
    }
    auto *manager = qobject_cast<QCNetworkAccessManager *>(reply->parent());
    if (!toShareConfig(manager).enabled()) {
        resetShareOnEasyIfNeeded(easy);
        return m_isPoisoned.load(std::memory_order_relaxed)
                   ? ShareApplyResult{ShareApplyState::NonCallable, m_initializationError}
                   : ShareApplyResult{};
    }
    if (!context || context->pendingDelete || context->cookieStorePoisoned || !context->share
        || !context->applied.enabled() || context->lastInitFailed) {
        const auto error = context && !context->lastInitError.isEmpty()
                               ? context->lastInitError
                               : QStringLiteral("必要的 manager Cookie/TLS share context 不可用");
        return {ShareApplyState::Rejected, error};
    }
    Internal::CookieOptionAdapter adapter;
    const auto binding = adapter.setEasy(easy,
                                         QCURL_CURL_OPTION(CURLOPT_SHARE),
                                         Internal::CookieOptionStage::Setup,
                                         context->share);
    if (!binding.isSuccess()) {
        return {ShareApplyState::Rejected, binding.message};
    }
    context->activeUsers += 1;
    m_easyToShareContext.insert(easy, context);
    m_easyShareOptionSet.insert(easy, true);
    const auto *state       = reply->d_func();
    const bool hasCookieJar = state->cookieMode != 0 && !state->cookieFilePath.isEmpty();
    if (!context->applied.cookies || hasCookieJar) {
        return {};
    }
    const auto cookie = adapter.setEasy(easy,
                                        QCURL_CURL_OPTION(CURLOPT_COOKIEFILE),
                                        Internal::CookieOptionStage::Setup,
                                        "");
    if (cookie.isSuccess()) {
        return {};
    }
    if (!detachShareBindingLocked(easy, "QCCurlMultiManager::applyShareToEasyLocked/rollback")) {
        return {ShareApplyState::NonCallable, m_initializationError};
    }
    return {ShareApplyState::Rejected, cookie.message};
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
