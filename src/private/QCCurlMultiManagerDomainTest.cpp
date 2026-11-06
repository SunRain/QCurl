#include "private/QCCurlMultiManagerTestAccess_p.h"

#ifdef QCURL_ENABLE_TEST_HOOKS

#include "private/QCCurlMultiDomain_p.h"
#include "private/QCCurlMultiTransferRecord_p.h"

namespace QCurl {

std::weak_ptr<QCCurlMultiDomain> QCCurlMultiManagerTestAccess::domainForReply(
    QCCurlMultiManager *engine, const QCNetworkReply *reply)
{
    for (const auto &record : std::as_const(engine->m_activeTransfers)) {
        if (record->observer() == reply) {
            return record->domain();
        }
    }
    return {};
}

int QCCurlMultiManagerTestAccess::isolatedDomainCount(QCCurlMultiManager *engine)
{
    return engine->m_isolatedDomains.size();
}

void QCCurlMultiManagerTestAccess::ageIdleDomains(QCCurlMultiManager *engine)
{
    for (auto &domain : engine->m_isolatedDomains) {
        if (domain.use_count() == 1) {
            domain->idleSince = std::chrono::steady_clock::time_point();
        }
    }
}

void QCCurlMultiManagerTestAccess::reclaimIdleDomains(QCCurlMultiManager *engine)
{
    engine->reclaimIdleDomains();
}

QCNetworkAccessManager::ShareHandleConfig QCCurlMultiManagerTestAccess::appliedShareConfig(
    QCCurlMultiManager *engine, const QCNetworkAccessManager *manager)
{
    return Internal::QCCurlShareCookieStoreTestAccess::snapshot(*engine->m_shareCookieStore, manager)
        .applied;
}

bool QCCurlMultiManagerTestAccess::hasPendingShareConfig(QCCurlMultiManager *engine,
                                                         const QCNetworkAccessManager *manager)
{
    return Internal::QCCurlShareCookieStoreTestAccess::snapshot(*engine->m_shareCookieStore, manager)
        .pending;
}

bool QCCurlMultiManagerTestAccess::hasShareBinding(QCCurlMultiManager *engine,
                                                   const QCNetworkReply *reply)
{
    for (auto it = engine->m_activeTransfers.cbegin(); it != engine->m_activeTransfers.cend();
         ++it) {
        if (it.value()->observer() == reply) {
            return Internal::QCCurlShareCookieStoreTestAccess::hasBinding(*engine->m_shareCookieStore,
                                                                          it.key());
        }
    }
    return false;
}

} // namespace QCurl

#endif // QCURL_ENABLE_TEST_HOOKS
