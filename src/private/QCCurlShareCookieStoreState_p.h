#ifndef QCCURLSHARECOOKIESTORESTATE_P_H
#define QCCURLSHARECOOKIESTORESTATE_P_H

#include "QCCurlHandleManager.h"
#include "private/QCCurlRequiredOptionAdapter_p.h"
#include "private/QCCurlShareCookieStore_p.h"

#include <QHash>
#include <QMutex>
#include <QSharedPointer>

#include <optional>
#include <vector>

namespace QCurl::Internal {

class QCCurlShareCookieStore::State final
{
public:
    explicit State(QThread *ownerThread);
    [[nodiscard]] bool openScope(const QCNetworkAccessManager *manager);
    [[nodiscard]] ShareCallResult synchronizeScope(
        const QCNetworkAccessManager *manager,
        const QCNetworkAccessManager::ShareHandleConfig &config);
    [[nodiscard]] ShareCallResult bindHttp(const QCNetworkAccessManager *manager,
                                           const QCNetworkAccessManager::ShareHandleConfig &config,
                                           CURL *easy,
                                           bool hasCookiePersistence);
    [[nodiscard]] ShareCallResult unbindHttp(CURL *easy, const char *operation);
    [[nodiscard]] ShareCallResult closeScope(const QCNetworkAccessManager *manager);
    [[nodiscard]] CookieCommandResult importCookies(
        const QCNetworkAccessManager *manager,
        const QCNetworkAccessManager::ShareHandleConfig &config,
        const QList<QCCookie> &cookies,
        const QUrl &originUrl);
    [[nodiscard]] CookieCommandResult exportCookies(
        const QCNetworkAccessManager *manager,
        const QCNetworkAccessManager::ShareHandleConfig &config,
        const QUrl &filterUrl);
    [[nodiscard]] CookieCommandResult clearAllCookies(
        const QCNetworkAccessManager *manager,
        const QCNetworkAccessManager::ShareHandleConfig &config);
    void beginShutdown() noexcept;
    [[nodiscard]] ShareCallResult shutdownScopes();
    void makeNonCallable() noexcept;

private:
    friend class QCCurlShareCookieStoreTestAccess;
    using Config = QCNetworkAccessManager::ShareHandleConfig;
    enum class Phase {
        Running,
        Draining,
        NonCallable,
        Closed,
    };

    class Context final
    {
    private:
        friend class State;
        friend class QCCurlShareCookieStoreTestAccess;
        CURLSH *m_share = nullptr;
        Config m_applied;
        std::optional<Config> m_pending;
        Config m_lastInitAttempt;
        bool m_lastInitFailed = false;
        CookieStoreResult m_lastInitResult;
        bool m_retired             = false;
        bool m_cookieStorePoisoned = false;
        int m_users                = 0;
        int m_operations           = 0;
        // 回调只访问这些互斥量，绝不回调 module 或获取 multi graph mutex。
        QMutex m_cookieMutex;
        QMutex m_sslMutex;
        QMutex m_otherMutex;
    };

    class Operation final
    {
    private:
        friend class State;
        friend class QCCurlShareCookieStoreTestAccess;
        QSharedPointer<Context> m_context;
        std::optional<QCCurlHandleManager> m_handle;
        CookieOptionAdapter m_adapter;
        bool m_bound = false;
    };

    [[nodiscard]] ShareCallResult admission() const;
    [[nodiscard]] ShareCallResult failOwnership(const char *operation,
                                                CURLcode curlCode    = CURLE_OK,
                                                CURLSHcode shareCode = CURLSHE_OK);
    [[nodiscard]] static bool sameConfig(const Config &left, const Config &right);
    [[nodiscard]] static bool hasUsers(const Context &context);
    [[nodiscard]] static QString summary(const Config &config);
    [[nodiscard]] QSharedPointer<Context> contextFor(const QCNetworkAccessManager *manager);
    [[nodiscard]] ShareCallResult synchronize(Context &context, const Config &desired);
    [[nodiscard]] ShareCallResult apply(Context &context, const Config &desired);
    [[nodiscard]] ShareCallResult initialize(Context &context, const Config &desired);
    [[nodiscard]] static RequiredOptionResult configureCallbacks(Context &context);
    [[nodiscard]] static RequiredOptionResult configureData(Context &context, const Config &desired);
    [[nodiscard]] static ShareCallResult initializationFailure(Context &context,
                                                               const RequiredOptionResult &option);
    [[nodiscard]] ShareCallResult bindToContext(const QSharedPointer<Context> &context,
                                                CURL *easy,
                                                bool hasCookiePersistence);
    [[nodiscard]] ShareCallResult shutdownContextList(const QList<QSharedPointer<Context>> &contexts);
    [[nodiscard]] static QMutex &callbackMutex(Context &context, curl_lock_data data);
    [[nodiscard]] ShareCallResult cleanup(Context &context, const char *operation);
    [[nodiscard]] ShareCallResult finalize(const QSharedPointer<Context> &context);
    static void lockCallback(CURL *, curl_lock_data data, curl_lock_access, void *userdata);
    static void unlockCallback(CURL *, curl_lock_data data, void *userdata);
    [[nodiscard]] static CookieStoreResult failure(CookieStoreStatus status,
                                                   const QString &policy,
                                                   const QString &message);
    [[nodiscard]] static CookieStoreResult optionFailure(CookieStoreStatus status,
                                                         const QString &policy,
                                                         const RequiredOptionResult &option);
    [[nodiscard]] static CookieStoreResult applied(QList<QCCookie> cookies = {});
    [[nodiscard]] std::unique_ptr<Operation> prepareOperation(const QCNetworkAccessManager *manager,
                                                              const Config &desired,
                                                              CookieCommandResult &result);
    [[nodiscard]] CookieStoreResult setupOperation(Operation &operation);
    [[nodiscard]] CookieCommandResult finishOperation(std::unique_ptr<Operation> operation,
                                                      CookieStoreResult result);
    [[nodiscard]] static CookieStoreResult snapshot(Operation &operation, QList<QByteArray> &lines);
    [[nodiscard]] static RequiredOptionResult restore(Operation &operation,
                                                      const QList<QByteArray> &lines);
    [[nodiscard]] static CookieStoreResult applyLines(Operation &operation,
                                                      const QList<QByteArray> &lines,
                                                      const QList<QByteArray> &previous);
    [[nodiscard]] static CookieStoreResult persist(Operation &operation);
    [[nodiscard]] static CookieStoreResult readCookies(Operation &operation, const QUrl &filterUrl);
    void observe(const char *event);

    QThread *m_ownerThread = nullptr;
    Phase m_phase          = Phase::Running;
    ShareCallResult m_ownershipFailure;
    QHash<const QCNetworkAccessManager *, QSharedPointer<Context>> m_scopes;
    QList<QSharedPointer<Context>> m_retired;
    QHash<CURL *, QSharedPointer<Context>> m_bindings;
    std::vector<std::unique_ptr<Operation>> m_retainedOperations;
#ifdef QCURL_ENABLE_TEST_HOOKS
    void (*m_observer)(void *, const char *) = nullptr;
    void *m_observerContext                  = nullptr;
#endif
};

} // namespace QCurl::Internal

#endif // QCCURLSHARECOOKIESTORESTATE_P_H
