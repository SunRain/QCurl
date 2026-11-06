#include "private/QCCurlShareCookieStoreState_p.h"

namespace QCurl::Internal {

ShareCallResult QCCurlShareCookieStore::State::failOwnership(const char *operation,
                                                             CURLcode curlCode,
                                                             CURLSHcode shareCode)
{
    if (m_phase != Phase::NonCallable) {
        m_ownershipFailure.state     = ShareCallState::NonCallable;
        m_ownershipFailure.operation = QString::fromLatin1(operation);
        m_ownershipFailure.curlCode  = curlCode;
        m_ownershipFailure.shareCode = shareCode;
        m_ownershipFailure.message   = QStringLiteral("share/Cookie ownership 无法证明（%1）")
                                           .arg(m_ownershipFailure.operation);
        m_phase                      = Phase::NonCallable;
    }
    return m_ownershipFailure;
}

ShareCallResult QCCurlShareCookieStore::State::cleanup(Context &context, const char *operation)
{
    if (m_phase == Phase::NonCallable) {
        return m_ownershipFailure;
    }
    if (!context.m_share) {
        return {};
    }
    if (hasUsers(context)) {
        return failOwnership(operation);
    }
    CURLSHcode code = CURLSHE_OK;
#ifdef QCURL_ENABLE_TEST_HOOKS
    const auto forced = qgetenv("QCURL_TEST_FORCE_SHARE_CLEANUP_ERROR").trimmed();
    if (forced == "cleanup-in-use" || forced == "shutdown-cleanup-failure") {
        code = CURLSHE_IN_USE;
    } else {
        code = curl_share_cleanup(context.m_share);
    }
#else
    code = curl_share_cleanup(context.m_share);
#endif
    if (code != CURLSHE_OK) {
        return failOwnership(operation, CURLE_OK, code);
    }
    context.m_share   = nullptr;
    context.m_applied = Config{};
    observe("share-cleanup");
    return {};
}

ShareCallResult QCCurlShareCookieStore::State::finalize(const QSharedPointer<Context> &context)
{
    if (m_phase == Phase::NonCallable) {
        return m_ownershipFailure;
    }
    if (hasUsers(*context) || m_phase == Phase::Draining) {
        return {};
    }
    if (!context->m_retired) {
        if (!context->m_pending) {
            return {};
        }
        // apply 清除 optional，先复制配置，避免继续借用失效内容。
        const auto desired = *context->m_pending;
        auto result        = apply(*context, desired);
        if (result.state == ShareCallState::Callable && !result.message.isEmpty()) {
            result.warnings.append(result.message);
        }
        return result;
    }
    auto result = cleanup(*context, "share-retired-cleanup");
    if (result.state == ShareCallState::Callable) {
        m_retired.removeAll(context);
    }
    return result;
}

ShareCallResult QCCurlShareCookieStore::State::closeScope(const QCNetworkAccessManager *manager)
{
    if (QThread::currentThread() != m_ownerThread) {
        return admission();
    }
    const auto context = m_scopes.take(manager);
    if (!context) {
        return m_phase == Phase::NonCallable ? m_ownershipFailure : ShareCallResult{};
    }
    // 先退出 live 索引；以后同地址的 manager 只会得到新的 context。
    context->m_retired = true;
    context->m_pending.reset();
    m_retired.append(context);
    return finalize(context);
}

ShareCallResult QCCurlShareCookieStore::State::bindHttp(const QCNetworkAccessManager *manager,
                                                        const Config &config,
                                                        CURL *easy,
                                                        bool hasCookiePersistence)
{
    auto result = admission();
    if (result.state != ShareCallState::Callable) {
        return result;
    }
    if (!config.enabled() || !manager) {
        result = synchronizeScope(manager, config);
        return result.state == ShareCallState::Callable ? unbindHttp(easy, "share-disabled-unbind")
                                                        : result;
    }
    const auto context = contextFor(manager);
    if (!context || !easy || context->m_retired || context->m_cookieStorePoisoned) {
        result.state   = ShareCallState::Rejected;
        result.message = QStringLiteral("必要的 manager Cookie/TLS share context 不可用");
        return result;
    }
    const bool busy = hasUsers(*context) && !sameConfig(config, context->m_applied);
    result          = synchronize(*context, config);
    if (result.state == ShareCallState::NonCallable) {
        return result;
    }
    if (busy) {
        result.warnings.append(
            QStringLiteral("share handle 配置变更延迟生效：期望 %1，实际 %2；仍有在途用户")
                .arg(summary(config), summary(context->m_applied)));
    } else if (!result.message.isEmpty()) {
        result.warnings.append(result.message);
    }
    if (!context->m_share || !context->m_applied.enabled() || context->m_lastInitFailed) {
        result.state   = ShareCallState::Rejected;
        result.message = context->m_lastInitResult.message.isEmpty()
                             ? QStringLiteral("必要的 manager Cookie/TLS share context 不可用")
                             : context->m_lastInitResult.message;
        return result;
    }
    auto binding     = bindToContext(context, easy, hasCookiePersistence);
    binding.warnings = result.warnings + binding.warnings;
    return binding;
}

ShareCallResult QCCurlShareCookieStore::State::bindToContext(const QSharedPointer<Context> &context,
                                                             CURL *easy,
                                                             bool hasCookiePersistence)
{
    if (m_bindings.contains(easy)) {
        return failOwnership("share-duplicate-binding");
    }
    CookieOptionAdapter adapter;
    auto option = adapter.setEasy(easy,
                                  QCURL_CURL_OPTION(CURLOPT_SHARE),
                                  CookieOptionStage::Setup,
                                  context->m_share);
    if (!option.isSuccess()) {
        return {ShareCallState::Rejected, {}, option.curlCode, option.shareCode, option.message, {}};
    }
    ++context->m_users;
    m_bindings.insert(easy, context);
    if (!context->m_applied.shareCookies() || hasCookiePersistence) {
        return {};
    }
    option = adapter.setEasy(easy,
                             QCURL_CURL_OPTION(CURLOPT_COOKIEFILE),
                             CookieOptionStage::Setup,
                             "");
    if (option.isSuccess()) {
        return {};
    }
    auto result = unbindHttp(easy, "share-cookie-setup-rollback");
    if (result.state == ShareCallState::Callable) {
        result.state    = ShareCallState::Rejected;
        result.curlCode = option.curlCode;
        result.message  = option.message;
    }
    return result;
}

ShareCallResult QCCurlShareCookieStore::State::unbindHttp(CURL *easy, const char *operation)
{
    if (QThread::currentThread() != m_ownerThread) {
        return admission();
    }
    if (m_phase == Phase::NonCallable) {
        return m_ownershipFailure;
    }
    const auto found = m_bindings.find(easy);
    if (found == m_bindings.end()) {
        return {};
    }
    const auto context = found.value();
    if (!context || context->m_users <= 0) {
        return failOwnership(operation);
    }
    CookieOptionAdapter adapter;
    const auto option = adapter.setEasy(easy,
                                        QCURL_CURL_OPTION(CURLOPT_SHARE),
                                        CookieOptionStage::Rollback,
                                        nullptr);
    if (!option.isSuccess()) {
        return failOwnership(operation, option.curlCode, option.shareCode);
    }
    // 原生解绑是提交点；pending 初始化失败不能撤销已证实安全的退出。
    m_bindings.erase(found);
    --context->m_users;
    observe("http-user-exit");
    return finalize(context);
}

void QCCurlShareCookieStore::State::beginShutdown() noexcept
{
    Q_ASSERT(QThread::currentThread() == m_ownerThread);
    if (m_phase == Phase::Running) {
        m_phase = Phase::Draining;
    }
}

ShareCallResult QCCurlShareCookieStore::State::shutdownContextList(
    const QList<QSharedPointer<Context>> &contexts)
{
    for (const auto &context : contexts) {
        if (!context) {
            continue;
        }
        const auto result = cleanup(*context, "share-shutdown-cleanup");
        if (result.state != ShareCallState::Callable) {
            return result;
        }
    }
    return {};
}

ShareCallResult QCCurlShareCookieStore::State::shutdownScopes()
{
    if (QThread::currentThread() != m_ownerThread) {
        return admission();
    }
    if (m_phase == Phase::NonCallable) {
        return m_ownershipFailure;
    }
    if (!m_bindings.isEmpty() || !m_retainedOperations.empty()) {
        return failOwnership("share-shutdown-active-users");
    }
    auto result = shutdownContextList(m_scopes.values() + m_retired);
    if (result.state == ShareCallState::Callable) {
        m_scopes.clear();
        m_retired.clear();
        m_phase = Phase::Closed;
    }
    return result;
}

void QCCurlShareCookieStore::State::makeNonCallable() noexcept
{
    if (m_phase != Phase::NonCallable) {
        static_cast<void>(failOwnership("multi-poisoned"));
    }
}

QMutex &QCCurlShareCookieStore::State::callbackMutex(Context &context, curl_lock_data data)
{
    switch (data) {
        case CURL_LOCK_DATA_COOKIE:
            return context.m_cookieMutex;
        case CURL_LOCK_DATA_SSL_SESSION:
            return context.m_sslMutex;
        default:
            return context.m_otherMutex;
    }
}

void QCCurlShareCookieStore::State::lockCallback(CURL *,
                                                 curl_lock_data data,
                                                 curl_lock_access,
                                                 void *userdata)
{
    if (auto *context = static_cast<Context *>(userdata)) {
        callbackMutex(*context, data).lock();
    }
}

void QCCurlShareCookieStore::State::unlockCallback(CURL *, curl_lock_data data, void *userdata)
{
    if (auto *context = static_cast<Context *>(userdata)) {
        callbackMutex(*context, data).unlock();
    }
}

} // namespace QCurl::Internal
