#include "private/QCCurlShareCookieStoreTestAccess_p.h"

#ifdef QCURL_ENABLE_TEST_HOOKS
#include "private/QCCurlShareCookieStoreState_p.h"

namespace QCurl::Internal {

ShareCookieSnapshot QCCurlShareCookieStoreTestAccess::snapshot(const QCCurlShareCookieStore &store,
                                                               const QCNetworkAccessManager *manager)
{
    const auto &state = *store.m_state;
    ShareCookieSnapshot result;
    result.bindings           = state.m_bindings.size();
    result.retainedOperations = static_cast<int>(state.m_retainedOperations.size());
    for (const auto &context : state.m_scopes.values() + state.m_retired) {
        if (context) {
            ++result.contexts;
            result.handles += context->m_share != nullptr;
            result.users += context->m_users;
            result.operations += context->m_operations;
        }
    }
    if (const auto context = state.m_scopes.value(manager)) {
        result.applied = context->m_applied;
        result.pending = context->m_pending.has_value();
    }
    return result;
}

bool QCCurlShareCookieStoreTestAccess::hasBinding(const QCCurlShareCookieStore &store, CURL *easy)
{
    return store.m_state->m_bindings.contains(easy);
}

ShareCallResult QCCurlShareCookieStoreTestAccess::prepareDetach(QCCurlShareCookieStore &store,
                                                                CURL *easy)
{
    auto &state = *store.m_state;
    if (!easy || state.m_bindings.contains(easy) || state.m_scopes.contains(nullptr)) {
        return {ShareCallState::Rejected,
                {},
                CURLE_OK,
                CURLSHE_OK,
                QStringLiteral("测试传输或 share 绑定前置无效"),
                {}};
    }
    auto context       = QSharedPointer<QCCurlShareCookieStore::State::Context>::create();
    context->m_retired = qgetenv("QCURL_TEST_FORCE_SHARE_CLEANUP_ERROR").trimmed()
                         != QByteArrayLiteral("shutdown-cleanup-failure");
    state.m_scopes.insert(nullptr, context);
    QCNetworkAccessManager::ShareHandleConfig config;
    config.setShareCookies(true);
    auto result = state.initialize(*context, config);
    if (!context->m_lastInitFailed && result.state == ShareCallState::Callable) {
        result = state.bindToContext(context, easy, true);
    }
    return result;
}

ShareCallResult QCCurlShareCookieStoreTestAccess::prepareRollback(QCCurlShareCookieStore &store,
                                                                  CURL *easy)
{
    auto result = prepareDetach(store, easy);
    if (result.state != ShareCallState::Callable) {
        return result;
    }
    CookieOptionAdapter adapter;
    const auto option = adapter.setEasy(easy,
                                        QCURL_CURL_OPTION(CURLOPT_COOKIEFILE),
                                        CookieOptionStage::Setup,
                                        "");
    if (option.isSuccess()) {
        result.state   = ShareCallState::Rejected;
        result.message = QStringLiteral("未观察到预期的 cookie engine setup 失败");
        return result;
    }
    return store.unbindHttp(easy, "test-share-rollback");
}

void QCCurlShareCookieStoreTestAccess::observe(QCCurlShareCookieStore &store,
                                               void (*observer)(void *, const char *),
                                               void *context)
{
    store.m_state->m_observer        = observer;
    store.m_state->m_observerContext = context;
}

} // namespace QCurl::Internal
#endif // QCURL_ENABLE_TEST_HOOKS
