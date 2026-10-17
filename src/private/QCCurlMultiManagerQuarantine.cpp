#include "QCCurlMultiManager.h"
#include "private/QCCurlMultiDomain_p.h"
#include "private/QCCurlMultiTransferRecord_p.h"

#include <QMutexLocker>

#include <utility>

namespace QCurl {
namespace {

enum class QuarantineCallableState {
    NonCallable,
};

/// 保存禁止再次传入 libcurl 的 poisoned ownership 对象图。
struct QuarantinedObjectGraph final
{
    QuarantineCallableState state = QuarantineCallableState::NonCallable;
    QHash<CURL *, QSharedPointer<QCCurlMultiTransferRecord>> activeTransfers;
    QList<QSharedPointer<QCCurlMultiTransferRecord>> unregisteredTransfers;
    QHash<const QCNetworkAccessManager *, QSharedPointer<Internal::QCCurlMultiManagerShareContext>>
        shareContexts;
    QHash<CURL *, Internal::QCCurlMultiManagerShareContext *> shareBindings;
    QHash<CURL *, bool> shareOptions;
    QList<std::shared_ptr<QCCurlMultiDomain>> domains;
    QList<QSharedPointer<Internal::QCCurlMultiManagerShareContext>> retiredShareContexts;
};

/// 返回覆盖全部 thread_local manager 析构期的进程级 quarantine registry。
struct ProcessQuarantineRegistry final
{
    QMutex mutex;
    QList<QuarantinedObjectGraph> graphs;
};

[[nodiscard]] ProcessQuarantineRegistry &processQuarantineRegistry()
{
    static auto *s_registry = new ProcessQuarantineRegistry;
    return *s_registry;
}

void appendNonCallableGraph(QuarantinedObjectGraph &&graph) noexcept
{
    ProcessQuarantineRegistry &registry = processQuarantineRegistry();
    QMutexLocker locker(&registry.mutex);
    registry.graphs.append(std::move(graph));
}

} // namespace

void QCCurlMultiManager::quarantineUnregisteredTransferLocked(
    QSharedPointer<QCCurlMultiTransferRecord> &&record,
    CURL *easy,
    ShareContext *shareContext) noexcept
{
    QuarantinedObjectGraph graph;
    graph.unregisteredTransfers.append(std::move(record));

    if (shareContext) {
        const auto contextIt = m_shareContexts.constFind(shareContext->scopeKey);
        if (contextIt != m_shareContexts.cend() && contextIt.value().data() == shareContext) {
            graph.shareContexts.insert(shareContext->scopeKey, contextIt.value());
        }

        const auto bindingIt = m_easyToShareContext.constFind(easy);
        if (bindingIt != m_easyToShareContext.cend() && bindingIt.value() == shareContext) {
            graph.shareBindings.insert(easy, shareContext);
        }

        const auto optionIt = m_easyShareOptionSet.constFind(easy);
        if (optionIt != m_easyShareOptionSet.cend()) {
            graph.shareOptions.insert(easy, optionIt.value());
        }
    }

    appendNonCallableGraph(std::move(graph));
}

/// 保留无法证明所有权的 poisoned 对象图，且不再调用其中的 libcurl handle。
void QCCurlMultiManager::retainPoisonedObjectGraph() noexcept
{
    QuarantinedObjectGraph graph;
    graph.activeTransfers      = std::move(m_activeTransfers);
    graph.shareContexts        = std::move(m_shareContexts);
    graph.shareBindings        = std::move(m_easyToShareContext);
    graph.shareOptions         = std::move(m_easyShareOptionSet);
    graph.domains              = allDomains();
    graph.retiredShareContexts = std::move(m_retiredShareContexts);
    m_baseDomain.reset();
    m_nativeDomain.reset();
    m_isolatedDomains.clear();
    appendNonCallableGraph(std::move(graph));
}

#ifdef QCURL_ENABLE_TEST_HOOKS
int QCCurlMultiManager::quarantinedGraphCountForTest() noexcept
{
    ProcessQuarantineRegistry &registry = processQuarantineRegistry();
    QMutexLocker locker(&registry.mutex);
    return registry.graphs.size();
}

int QCCurlMultiManager::quarantinedPendingTransfersForTest() noexcept
{
    ProcessQuarantineRegistry &registry = processQuarantineRegistry();
    QMutexLocker locker(&registry.mutex);

    int count = 0;
    for (const QuarantinedObjectGraph &graph : std::as_const(registry.graphs)) {
        count += graph.unregisteredTransfers.size();
    }
    return count;
}

bool QCCurlMultiManager::quarantineIsNonCallableForTest() noexcept
{
    ProcessQuarantineRegistry &registry = processQuarantineRegistry();
    QMutexLocker locker(&registry.mutex);
    for (const QuarantinedObjectGraph &graph : std::as_const(registry.graphs)) {
        if (graph.state != QuarantineCallableState::NonCallable) {
            return false;
        }
    }
    return true;
}
#endif

} // namespace QCurl
