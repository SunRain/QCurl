#include "QCCurlMultiManager.h"
#include "private/QCCurlMultiDomain_p.h"
#include "private/QCCurlMultiTransferRecord_p.h"
#include "private/QCCurlShareCookieStore_p.h"

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
    QSharedPointer<Internal::QCCurlShareCookieStore> shareCookieStore;
    QList<std::shared_ptr<QCCurlMultiDomain>> domains;
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
    QSharedPointer<QCCurlMultiTransferRecord> &&record) noexcept
{
    QuarantinedObjectGraph graph;
    graph.unregisteredTransfers.append(std::move(record));
    graph.shareCookieStore = m_shareCookieStore;
    appendNonCallableGraph(std::move(graph));
}

/// 保留无法证明所有权的 poisoned 对象图，且不再调用其中的 libcurl handle。
void QCCurlMultiManager::retainPoisonedObjectGraph() noexcept
{
    QuarantinedObjectGraph graph;
    graph.activeTransfers  = std::move(m_activeTransfers);
    graph.shareCookieStore = m_shareCookieStore;
    graph.domains          = allDomains();
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
