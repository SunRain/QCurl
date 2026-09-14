#include "QCCurlMultiManager.h"

#ifdef QCURL_ENABLE_TEST_HOOKS

#include "private/QCCurlMultiManagerTestAccess_p.h"
#include "private/QCCurlMultiTransferRecord_p.h"
#include "private/QCCurlRequiredOptionAdapter_p.h"

#include <QByteArray>
#include <QMutexLocker>

namespace QCurl {

namespace {

template<typename Transfers, typename Token>
[[nodiscard]] CURL *easyForToken(const Transfers &transfers, Token token)
{
    for (auto it = transfers.cbegin(); it != transfers.cend(); ++it) {
        if (it.value() && it.value()->token() == token) {
            return it.key();
        }
    }
    return nullptr;
}

} // namespace

bool QCCurlMultiManagerTestAccess::prepareShareDetach(QCCurlMultiManager *manager,
                                                      QCCurlMultiManager::TransferToken token,
                                                      QString *error)
{
    Q_ASSERT(manager);
    QMutexLocker locker(&manager->m_mutex);
    CURL *easy = easyForToken(manager->m_activeTransfers, token);
    if (!easy || manager->m_easyToShareContext.contains(easy)
        || manager->m_shareContexts.contains(nullptr)) {
        if (error) {
            *error = easy ? QStringLiteral("测试传输已经绑定 share handle")
                          : QStringLiteral("未找到测试传输 token");
        }
        return false;
    }

    auto context            = QSharedPointer<QCCurlMultiManager::ShareContext>::create();
    context->scopeDestroyed = true;
    context->pendingDelete  = qgetenv("QCURL_TEST_FORCE_SHARE_CLEANUP_ERROR").trimmed()
                              != QByteArrayLiteral("shutdown-cleanup-failure");
    manager->m_shareContexts.insert(nullptr, context);

    QCCurlMultiManager::ShareConfig desired;
    desired.cookies = true;
    if (!manager->initializeShareContextLocked(context.data(), desired, error)) {
        if (!manager->m_isPoisoned.load(std::memory_order_relaxed)) {
            manager->m_shareContexts.remove(nullptr);
        }
        return false;
    }

    Internal::CookieOptionAdapter adapter;
    const Internal::RequiredOptionResult bindResult
        = adapter.setEasy(easy,
                          QCURL_CURL_OPTION(CURLOPT_SHARE),
                          Internal::CookieOptionStage::Setup,
                          context->share);
    if (!bindResult.isSuccess()) {
        if (manager->cleanupShareHandleLocked(
                context.data(), "QCCurlMultiManagerTestAccess::prepareShareDetach/bind-cleanup")) {
            manager->m_shareContexts.remove(nullptr);
        }
        if (error) {
            *error = bindResult.message;
        }
        return false;
    }

    context->activeUsers = 1;
    manager->m_easyToShareContext.insert(easy, context.data());
    manager->m_easyShareOptionSet.insert(easy, true);
    if (error) {
        error->clear();
    }
    return true;
}

bool QCCurlMultiManagerTestAccess::prepareShareRollback(QCCurlMultiManager *manager,
                                                        QCCurlMultiManager::TransferToken token,
                                                        QString *error)
{
    Q_ASSERT(manager);
    if (!prepareShareDetach(manager, token, error)) {
        return false;
    }

    QMutexLocker locker(&manager->m_mutex);
    CURL *easy = easyForToken(manager->m_activeTransfers, token);
    Internal::CookieOptionAdapter adapter;
    const Internal::RequiredOptionResult cookieEngine
        = adapter.setEasy(easy,
                          QCURL_CURL_OPTION(CURLOPT_COOKIEFILE),
                          Internal::CookieOptionStage::Setup,
                          "");
    if (cookieEngine.isSuccess()) {
        static_cast<void>(manager->detachShareBindingLocked(
            easy, "QCCurlMultiManagerTestAccess::prepareShareRollback/unexpected-success"));
        if (error) {
            *error = QStringLiteral("未观察到预期的 cookie engine setup 失败");
        }
        return false;
    }

    if (manager->detachShareBindingLocked(
            easy, "QCCurlMultiManagerTestAccess::prepareShareRollback/rollback")) {
        if (error) {
            *error = QStringLiteral("未观察到预期的 share rollback 失败");
        }
        return false;
    }
    return manager->m_isPoisoned.load(std::memory_order_relaxed);
}

int QCCurlMultiManagerTestAccess::activeShareBindings(QCCurlMultiManager *manager)
{
    Q_ASSERT(manager);
    QMutexLocker locker(&manager->m_mutex);
    return manager->m_easyToShareContext.size();
}

int QCCurlMultiManagerTestAccess::activeShareContexts(QCCurlMultiManager *manager)
{
    Q_ASSERT(manager);
    QMutexLocker locker(&manager->m_mutex);
    return manager->m_shareContexts.size();
}

int QCCurlMultiManagerTestAccess::activeShareContextsWithHandle(QCCurlMultiManager *manager)
{
    Q_ASSERT(manager);
    QMutexLocker locker(&manager->m_mutex);
    int count = 0;
    for (auto it = manager->m_shareContexts.cbegin(); it != manager->m_shareContexts.cend(); ++it) {
        if (it.value() && it.value()->share) {
            ++count;
        }
    }
    return count;
}

int QCCurlMultiManagerTestAccess::activeShareUsers(QCCurlMultiManager *manager)
{
    Q_ASSERT(manager);
    QMutexLocker locker(&manager->m_mutex);
    int count = 0;
    for (auto it = manager->m_shareContexts.cbegin(); it != manager->m_shareContexts.cend(); ++it) {
        if (it.value()) {
            count += it.value()->activeUsers;
        }
    }
    return count;
}

} // namespace QCurl

#endif
