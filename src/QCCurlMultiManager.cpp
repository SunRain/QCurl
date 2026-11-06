#include "QCCurlMultiManager.h"

#include "private/QCCurlMultiDomain_p.h"
#include "private/QCCurlMultiTransferRecord_p.h"
#include "private/QCCurlShareCookieStore_p.h"

#ifdef QCURL_ENABLE_TEST_HOOKS
#include "private/QCCurlRequiredOptionAdapter_p.h"
#endif
#include "private/QCurlRuntimeState_p.h"

#include <QDebug>
#include <QList>
#include <QMutex>
#include <QMutexLocker>
#include <QThread>
#include <QTimer>

#include <utility>
#include <vector>

namespace QCurl {

QCCurlMultiManager *QCCurlMultiManager::instance()
{
#ifdef QCURL_ENABLE_TEST_HOOKS
    // 观测器先构造、后析构，覆盖 thread-local manager 的原生资源收尾。
    static_cast<void>(Internal::shareCookieLifecycleForTest());
#endif
    // 线程内单例：每个线程拥有独立的 multi engine，避免同一 CURLM* 被跨线程并发访问
    static thread_local QCCurlMultiManager s_instance;
    return &s_instance;
}

QCCurlMultiManager::QCCurlMultiManager(QObject *parent)
    : QObject(parent)
    , m_runtimeLease(Internal::acquireRuntimeLease())
    , m_shareCookieStore(QSharedPointer<Internal::QCCurlShareCookieStore>::create(thread()))
{
    qDebug() << "QCCurlMultiManager: Initializing global instance";

    if (!m_runtimeLease.isValid()) {
        m_initializationError = m_runtimeLease.diagnostic();
        qCritical().noquote() << "QCCurlMultiManager:" << m_initializationError;
        return;
    }

    m_baseDomain = createDomain(&m_initializationError);
    if (!m_baseDomain) {
        qCritical().noquote() << "QCCurlMultiManager:" << m_initializationError;
        return;
    }
    m_domainReaper = new QTimer(this);
    m_domainReaper->setInterval(1000);
    connect(m_domainReaper, &QTimer::timeout, this, &QCCurlMultiManager::reclaimIdleDomains);
    m_domainReaper->start();

    m_runtimeParticipant = Internal::registerRuntimeShutdownParticipant(this, [this]() {
        shutdown(false);
    });
    if (m_runtimeParticipant == 0) {
        m_initializationError = QStringLiteral("QCurlRuntime 已停止 admission");
        retireDomain(m_baseDomain.get());
        m_baseDomain.reset();
        m_runtimeLease.reset();
        return;
    }

    m_isReady = true;
    qDebug() << "QCCurlMultiManager: Initialization complete";
}

QCCurlMultiManager::~QCCurlMultiManager()
{
    unregisterRuntimeParticipant();
    shutdown(true);
}

#ifdef QCURL_ENABLE_TEST_HOOKS
void QCCurlMultiManager::shutdownForTest()
{
    shutdown(false);
}
#endif

void QCCurlMultiManager::shutdown(bool retainPoisonedGraph)
{
    if (m_isShuttingDown.exchange(true, std::memory_order_relaxed)) {
        if (retainPoisonedGraph && m_isPoisoned.load(std::memory_order_relaxed)) {
            retainPoisonedObjectGraph();
        }
        return;
    }

    qDebug() << "QCCurlMultiManager: Destroying instance";
    m_isReady = false;

    if (m_domainReaper) {
        m_domainReaper->stop();
    }
    for (const auto &domain : allDomains()) {
        stopDomainEvents(domain.get());
        if (!m_isPoisoned.load(std::memory_order_relaxed)) {
            disableMultiCallbacks(domain.get());
        }
    }
    m_shareCookieStore->beginShutdown();
    if (!m_isPoisoned.load(std::memory_order_relaxed)) {
        shutdownTransportGraph();
    }
    if (m_isPoisoned.load(std::memory_order_relaxed)) {
        if (retainPoisonedGraph) {
            retainPoisonedObjectGraph();
        }
        releaseRuntimeParticipation();
        return;
    }

    releaseRuntimeParticipation();
    qDebug() << "QCCurlMultiManager: Destruction complete";
}

void QCCurlMultiManager::shutdownTransportGraph()
{
    QList<CURL *> activeHandles;
    {
        QMutexLocker locker(&m_mutex);
        activeHandles = m_activeTransfers.keys();
    }

    // 只有前一层安全脱离后才能销毁下一层；失败时保留整个剩余对象图。
    cleanupActiveHandlesForShutdown(activeHandles);
    if (m_isPoisoned.load(std::memory_order_relaxed)) {
        return;
    }
    {
        QMutexLocker locker(&m_mutex);
        if (!consumeShareResultLocked(m_shareCookieStore->shutdownScopes())) {
            return;
        }
    }
    for (const auto &domain : allDomains()) {
        retireDomain(domain.get());
        if (m_isPoisoned.load(std::memory_order_relaxed)) {
            return;
        }
    }
    m_isolatedDomains.clear();
    m_nativeDomain.reset();
    m_baseDomain.reset();
}

void QCCurlMultiManager::releaseRuntimeParticipation() noexcept
{
    unregisterRuntimeParticipant();
    m_runtimeLease.reset();
}

void QCCurlMultiManager::unregisterRuntimeParticipant() noexcept
{
    Internal::unregisterRuntimeShutdownParticipant(m_runtimeParticipant);
    m_runtimeParticipant = 0;
}

int QCCurlMultiManager::runningRequestsCount() const noexcept
{
    return m_runningRequests.load(std::memory_order_relaxed);
}

void QCCurlMultiManager::poisonLocked(const char *context, CURLMcode code)
{
    if (m_isPoisoned.exchange(true, std::memory_order_relaxed)) {
        return;
    }

    m_shareCookieStore->makeNonCallable();
    m_isReady = false;
    m_initializationError
        = QStringLiteral("curl multi manager 已永久 poisoned：ownership 无法证明（%1, code=%2）")
              .arg(QString::fromUtf8(context ? context : "unknown"))
              .arg(static_cast<int>(code));
    Internal::markRuntimePoisoned(m_initializationError);

    if (m_domainReaper) {
        m_domainReaper->stop();
    }
    for (const auto &domain : allDomains()) {
        stopDomainEvents(domain.get());
    }

    const QString message = m_initializationError;
    for (const auto &transfer : std::as_const(m_activeTransfers)) {
        if (transfer) {
            // 隔离不转移 ownership，但必须撤销借用，阻止控制路径继续调用原生 handle。
            transfer->clearActiveHandle();
        }
        if (transfer && transfer->observer() && transfer->hasCompletionHandler()) {
            auto completion = transfer->takeCompletionHandler();
            // 不归还隔离的 handle；解锁并离开原生调用栈后交由 reply-side adapter 报错。
            QMetaObject::invokeMethod(
                this,
                [completion = std::move(completion)]() {
                    completion(QCCurlHandleManager(nullptr), CURLE_FAILED_INIT, 0);
                },
                Qt::QueuedConnection);
        }
    }

    qCritical() << "QCCurlMultiManager: entering permanent poisoned state:" << message;
}

void QCCurlMultiManager::cleanupActiveHandlesForShutdown(const QList<CURL *> &activeHandles)
{
    if (!activeHandles.isEmpty()) {
        qWarning() << "QCCurlMultiManager: Destroying with" << activeHandles.size()
                   << "active requests";
    }

    std::vector<FinishedTransfer> completedTransfers;
    QMutexLocker locker(&m_mutex);
    for (CURL *easy : activeHandles) {
        auto transferIt = m_activeTransfers.find(easy);
        if (transferIt == m_activeTransfers.end()) {
            poisonLocked("QCCurlMultiManager::~QCCurlMultiManager", CURLM_BAD_EASY_HANDLE);
            break;
        }
        auto detached = detachTransferRecordLocked(transferIt.value().data(),
                                                   CURLE_ABORTED_BY_CALLBACK,
                                                   "QCCurlMultiManager::~QCCurlMultiManager");
        if (detached.has_value()) {
            completedTransfers.push_back(std::move(detached.value()));
        }
        if (m_isPoisoned.load(std::memory_order_relaxed)) {
            break;
        }
    }
    locker.unlock();

    for (auto &transfer : completedTransfers) {
        dispatchFinishedTransfer(std::move(transfer));
    }
}

void QCCurlMultiManager::disableSocketsForShutdown(const QList<QSharedPointer<SocketInfo>> &sockets)
{
    for (const auto &infoHolder : sockets) {
        SocketInfo *info = infoHolder.data();
        if (!info) {
            continue;
        }
        if (info->readNotifier) {
            info->readNotifier->setEnabled(false);
        }
        if (info->writeNotifier) {
            info->writeNotifier->setEnabled(false);
        }
    }
}

} // namespace QCurl
