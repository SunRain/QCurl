#include "private/QCCookiePolicyCode_p.h"
#include "private/QCCurlShareCookieStoreState_p.h"

namespace QCurl::Internal {

QCCurlShareCookieStore::QCCurlShareCookieStore(QThread *ownerThread)
    : m_state(std::make_unique<State>(ownerThread))
{}

QCCurlShareCookieStore::~QCCurlShareCookieStore() = default;

bool QCCurlShareCookieStore::openScope(const QCNetworkAccessManager *manager)
{
    return m_state->openScope(manager);
}

ShareCallResult QCCurlShareCookieStore::synchronizeScope(
    const QCNetworkAccessManager *manager, const QCNetworkAccessManager::ShareHandleConfig &config)
{
    return m_state->synchronizeScope(manager, config);
}

ShareCallResult QCCurlShareCookieStore::bindHttp(
    const QCNetworkAccessManager *manager,
    const QCNetworkAccessManager::ShareHandleConfig &config,
    CURL *easy,
    bool hasCookiePersistence)
{
    return m_state->bindHttp(manager, config, easy, hasCookiePersistence);
}

ShareCallResult QCCurlShareCookieStore::unbindHttp(CURL *easy, const char *operation)
{
    return m_state->unbindHttp(easy, operation);
}

ShareCallResult QCCurlShareCookieStore::closeScope(const QCNetworkAccessManager *manager)
{
    return m_state->closeScope(manager);
}

CookieCommandResult QCCurlShareCookieStore::importCookies(
    const QCNetworkAccessManager *manager,
    const QCNetworkAccessManager::ShareHandleConfig &config,
    const QList<QCCookie> &cookies,
    const QUrl &originUrl)
{
    return m_state->importCookies(manager, config, cookies, originUrl);
}

CookieCommandResult QCCurlShareCookieStore::exportCookies(
    const QCNetworkAccessManager *manager,
    const QCNetworkAccessManager::ShareHandleConfig &config,
    const QUrl &filterUrl)
{
    return m_state->exportCookies(manager, config, filterUrl);
}

CookieCommandResult QCCurlShareCookieStore::clearAllCookies(
    const QCNetworkAccessManager *manager, const QCNetworkAccessManager::ShareHandleConfig &config)
{
    return m_state->clearAllCookies(manager, config);
}

void QCCurlShareCookieStore::beginShutdown() noexcept
{
    m_state->beginShutdown();
}

ShareCallResult QCCurlShareCookieStore::shutdownScopes()
{
    return m_state->shutdownScopes();
}

void QCCurlShareCookieStore::makeNonCallable() noexcept
{
    m_state->makeNonCallable();
}

QCCurlShareCookieStore::State::State(QThread *ownerThread)
    : m_ownerThread(ownerThread)
{}

ShareCallResult QCCurlShareCookieStore::State::admission() const
{
    if (QThread::currentThread() != m_ownerThread) {
        return {ShareCallState::Rejected,
                {},
                CURLE_OK,
                CURLSHE_OK,
                QStringLiteral("share/Cookie 操作必须在 owner thread 执行"),
                {}};
    }
    if (m_phase == Phase::NonCallable) {
        return m_ownershipFailure;
    }
    if (m_phase != Phase::Running) {
        return {ShareCallState::Rejected,
                {},
                CURLE_OK,
                CURLSHE_OK,
                QStringLiteral("share/Cookie module 正在关闭"),
                {}};
    }
    return {};
}

bool QCCurlShareCookieStore::State::openScope(const QCNetworkAccessManager *manager)
{
    if (admission().state != ShareCallState::Callable || !manager || m_scopes.contains(manager)) {
        return false;
    }
    m_scopes.insert(manager, {});
    return true;
}

bool QCCurlShareCookieStore::State::sameConfig(const Config &left, const Config &right)
{
    return left.shareCookies() == right.shareCookies()
           && left.shareSslSession() == right.shareSslSession();
}

bool QCCurlShareCookieStore::State::hasUsers(const Context &context)
{
    return context.m_users != 0 || context.m_operations != 0;
}

QString QCCurlShareCookieStore::State::summary(const Config &config)
{
    if (!config.enabled()) {
        return QStringLiteral("关闭");
    }
    QStringList parts;
    if (config.shareCookies()) {
        parts.append(QStringLiteral("Cookie"));
    }
    if (config.shareSslSession()) {
        parts.append(QStringLiteral("SSL session"));
    }
    return parts.join(QLatin1Char(','));
}

QSharedPointer<QCCurlShareCookieStore::State::Context> QCCurlShareCookieStore::State::contextFor(
    const QCNetworkAccessManager *manager)
{
    auto found = m_scopes.find(manager);
    if (found == m_scopes.end()) {
        return {};
    }
    if (!found.value()) {
        found.value() = QSharedPointer<Context>::create();
    }
    return found.value();
}

ShareCallResult QCCurlShareCookieStore::State::synchronizeScope(
    const QCNetworkAccessManager *manager, const Config &config)
{
    const auto available = admission();
    if (available.state != ShareCallState::Callable) {
        return available;
    }
    const auto context = m_scopes.value(manager);
    return context ? synchronize(*context, config) : ShareCallResult{};
}

ShareCallResult QCCurlShareCookieStore::State::synchronize(Context &context, const Config &desired)
{
    if (sameConfig(context.m_applied, desired)) {
        context.m_pending.reset();
        return {};
    }
    context.m_pending = desired;
    return hasUsers(context) ? ShareCallResult{} : apply(context, desired);
}

ShareCallResult QCCurlShareCookieStore::State::apply(Context &context, const Config &desired)
{
    const auto released = cleanup(context, "share-reconfigure-cleanup");
    if (released.state != ShareCallState::Callable) {
        return released;
    }
    context.m_pending.reset();
    if (context.m_lastInitFailed && sameConfig(context.m_lastInitAttempt, desired)) {
        ShareCallResult result;
        result.message   = context.m_lastInitResult.message;
        result.curlCode  = context.m_lastInitResult.curlCode;
        result.shareCode = context.m_lastInitResult.shareCode;
        return result;
    }
    return initialize(context, desired);
}

ShareCallResult QCCurlShareCookieStore::State::initializationFailure(
    Context &context, const RequiredOptionResult &option)
{
    context.m_lastInitFailed = true;
    context.m_lastInitResult = optionFailure(CookieStoreStatus::RejectedBeforeMutation,
                                             option.policyCode,
                                             option);
    ShareCallResult result;
    result.curlCode  = option.curlCode;
    result.shareCode = option.shareCode;
    result.message   = option.message;
    return result;
}

RequiredOptionResult QCCurlShareCookieStore::State::configureCallbacks(Context &context)
{
    auto option = CookieOptionAdapter::setShare(context.m_share,
                                                CURLSHOPT_USERDATA,
                                                "CURLSHOPT_USERDATA",
                                                nullptr,
                                                &context);
    if (option.isSuccess()) {
        option = CookieOptionAdapter::setShare(context.m_share,
                                               CURLSHOPT_LOCKFUNC,
                                               "CURLSHOPT_LOCKFUNC",
                                               nullptr,
                                               lockCallback);
    }
    if (option.isSuccess()) {
        option = CookieOptionAdapter::setShare(context.m_share,
                                               CURLSHOPT_UNLOCKFUNC,
                                               "CURLSHOPT_UNLOCKFUNC",
                                               nullptr,
                                               unlockCallback);
    }
    return option;
}

RequiredOptionResult QCCurlShareCookieStore::State::configureData(Context &context,
                                                                  const Config &desired)
{
    RequiredOptionResult option;
    if (desired.shareCookies()) {
        option = CookieOptionAdapter::setShare(context.m_share,
                                               CURLSHOPT_SHARE,
                                               "CURLSHOPT_SHARE",
                                               "CURL_LOCK_DATA_COOKIE",
                                               CURL_LOCK_DATA_COOKIE);
        if (option.isSuccess()) {
            context.m_applied.setShareCookies(true);
        }
    }
    if (option.isSuccess() && desired.shareSslSession()) {
        option = CookieOptionAdapter::setShare(context.m_share,
                                               CURLSHOPT_SHARE,
                                               "CURLSHOPT_SHARE",
                                               "CURL_LOCK_DATA_SSL_SESSION",
                                               CURL_LOCK_DATA_SSL_SESSION);
        if (option.isSuccess()) {
            context.m_applied.setShareSslSession(true);
        }
    }
    return option;
}

ShareCallResult QCCurlShareCookieStore::State::initialize(Context &context, const Config &desired)
{
    context.m_applied         = Config{};
    context.m_lastInitAttempt = desired;
    context.m_lastInitFailed  = false;
    context.m_lastInitResult  = CookieStoreResult{};
    if (!desired.enabled()) {
        return {};
    }
#ifdef QCURL_ENABLE_TEST_HOOKS
    if (qgetenv("QCURL_TEST_FORCE_SHARE_INIT_ERROR").isEmpty()) {
        context.m_share = curl_share_init();
    }
#else
    context.m_share = curl_share_init();
#endif
    if (!context.m_share) {
        RequiredOptionResult failed;
        failed.policyCode = cookiepolicy::kShareInitFailed;
        failed.message    = QStringLiteral("curl_share_init 失败");
        return initializationFailure(context, failed);
    }
    auto option = configureCallbacks(context);
    if (option.isSuccess()) {
        option = configureData(context, desired);
    }
    if (option.isSuccess()) {
        observe("share-initialize");
        return {};
    }
    const auto failure  = initializationFailure(context, option);
    const auto released = cleanup(context, "share-configuration-cleanup");
    return released.state == ShareCallState::Callable ? failure : released;
}

void QCCurlShareCookieStore::State::observe(const char *event)
{
#ifdef QCURL_ENABLE_TEST_HOOKS
    recordShareCookieLifecycleForTest(event);
    if (m_observer) {
        m_observer(m_observerContext, event);
    }
#else
    Q_UNUSED(event)
#endif
}

} // namespace QCurl::Internal
