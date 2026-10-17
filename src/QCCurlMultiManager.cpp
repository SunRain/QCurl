#include "QCCurlMultiManager.h"

#include "QCNetworkAccessManager.h"
#include "QCNetworkConnectionPoolManager.h"
#include "QCNetworkConnectionPoolManager_p.h"
#include "QCNetworkReply.h"
#include "QCNetworkReply_p.h"
#include "private/QCCurlMultiDomain_p.h"
#include "private/QCCurlMultiTransferRecord_p.h"
#include "private/QCCurlOptionAdapter_p.h"
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
    // 线程内单例：每个线程拥有独立的 multi engine，避免同一 CURLM* 被跨线程并发访问
    static thread_local QCCurlMultiManager s_instance;
    return &s_instance;
}

QCCurlMultiManager::QCCurlMultiManager(QObject *parent)
    : QObject(parent)
    , m_runtimeLease(Internal::acquireRuntimeLease())
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
    QList<QSharedPointer<ShareContext>> shareContexts;
    {
        QMutexLocker locker(&m_mutex);
        activeHandles = m_activeTransfers.keys();
        shareContexts = m_shareContexts.values() + m_retiredShareContexts;
    }

    // 只有前一层安全脱离后才能销毁下一层；失败时保留整个剩余对象图。
    cleanupActiveHandlesForShutdown(activeHandles);
    if (m_isPoisoned.load(std::memory_order_relaxed)) {
        return;
    }
    cleanupShareContextsForShutdown(shareContexts);
    if (m_isPoisoned.load(std::memory_order_relaxed)) {
        return;
    }

    {
        QMutexLocker locker(&m_mutex);
        m_shareContexts.clear();
        m_easyToShareContext.clear();
        m_easyShareOptionSet.clear();
        m_retiredShareContexts.clear();
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
        if (QCNetworkReply *observer = transfer ? transfer->observer() : nullptr) {
            QPointer<QCNetworkReply> safeObserver(observer);
            QMetaObject::invokeMethod(
                observer,
                [safeObserver, message]() {
                    if (safeObserver) {
                        safeObserver->abortWithError(NetworkError::Unknown, message);
                    }
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

void QCCurlMultiManager::cleanupShareContextsForShutdown(
    const QList<QSharedPointer<ShareContext>> &shareContexts)
{
    QMutexLocker locker(&m_mutex);
    for (const auto &contextHolder : shareContexts) {
        ShareContext *context = contextHolder.data();
        if (!context) {
            continue;
        }
        if (context->hasUsers()) {
            poisonLocked("QCCurlMultiManager::cleanupShareContextsForShutdown/active-users",
                         CURLM_INTERNAL_ERROR);
            return;
        }
        context->pendingDelete = true;
        maybeFinalizeShareContextLocked(context);
        if (m_isPoisoned.load(std::memory_order_relaxed)) {
            return;
        }
    }
}

} // namespace QCurl
