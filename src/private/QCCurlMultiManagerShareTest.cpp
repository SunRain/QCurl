#include "QCCurlMultiManager.h"

#ifdef QCURL_ENABLE_TEST_HOOKS

#include "private/QCCurlMultiManagerTestAccess_p.h"
#include "private/QCCurlMultiTransferRecord_p.h"
#include "private/QCCurlShareCookieStoreTestAccess_p.h"

#include <QMutexLocker>

namespace QCurl {
namespace {

[[nodiscard]] CURL *easyForToken(
    const QHash<CURL *, QSharedPointer<QCCurlMultiTransferRecord>> &transfers,
    QCCurlMultiManager::TransferToken token)
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
    const auto result = Internal::QCCurlShareCookieStoreTestAccess::prepareDetach(
        *manager->m_shareCookieStore, easyForToken(manager->m_activeTransfers, token));
    if (error) {
        *error = result.message;
    }
    return manager->consumeShareResultLocked(result);
}

bool QCCurlMultiManagerTestAccess::prepareShareRollback(QCCurlMultiManager *manager,
                                                        QCCurlMultiManager::TransferToken token,
                                                        QString *error)
{
    Q_ASSERT(manager);
    QMutexLocker locker(&manager->m_mutex);
    const auto result = Internal::QCCurlShareCookieStoreTestAccess::prepareRollback(
        *manager->m_shareCookieStore, easyForToken(manager->m_activeTransfers, token));
    if (error) {
        *error = result.message;
    }
    static_cast<void>(manager->consumeShareResultLocked(result));
    return result.state == Internal::ShareCallState::NonCallable;
}

int QCCurlMultiManagerTestAccess::activeShareBindings(QCCurlMultiManager *manager)
{
    QMutexLocker locker(&manager->m_mutex);
    return Internal::QCCurlShareCookieStoreTestAccess::snapshot(*manager->m_shareCookieStore)
        .bindings;
}

int QCCurlMultiManagerTestAccess::activeShareContexts(QCCurlMultiManager *manager)
{
    QMutexLocker locker(&manager->m_mutex);
    return Internal::QCCurlShareCookieStoreTestAccess::snapshot(*manager->m_shareCookieStore)
        .contexts;
}

int QCCurlMultiManagerTestAccess::activeShareContextsWithHandle(QCCurlMultiManager *manager)
{
    QMutexLocker locker(&manager->m_mutex);
    return Internal::QCCurlShareCookieStoreTestAccess::snapshot(*manager->m_shareCookieStore).handles;
}

int QCCurlMultiManagerTestAccess::activeShareUsers(QCCurlMultiManager *manager)
{
    QMutexLocker locker(&manager->m_mutex);
    return Internal::QCCurlShareCookieStoreTestAccess::snapshot(*manager->m_shareCookieStore).users;
}

bool QCCurlMultiManagerTestAccess::bindShare(QCCurlMultiManager *engine,
                                             QCNetworkAccessManager *manager,
                                             QCCurlMultiManager::TransferToken token)
{
    QMutexLocker locker(&engine->m_mutex);
    return engine->consumeShareResultLocked(
        engine->m_shareCookieStore->bindHttp(manager,
                                             manager->shareHandleConfig(),
                                             easyForToken(engine->m_activeTransfers, token),
                                             false));
}

Internal::ShareCookieSnapshot QCCurlMultiManagerTestAccess::shareSnapshot(QCCurlMultiManager *manager)
{
    QMutexLocker locker(&manager->m_mutex);
    return Internal::QCCurlShareCookieStoreTestAccess::snapshot(*manager->m_shareCookieStore);
}

QWeakPointer<Internal::QCCurlShareCookieStore> QCCurlMultiManagerTestAccess::shareStore(
    QCCurlMultiManager *manager)
{
    return manager->m_shareCookieStore;
}

} // namespace QCurl
#endif // QCURL_ENABLE_TEST_HOOKS
