#include "QCCurlMultiManager.h"

#include "QCNetworkAccessManager.h"
#include "QCNetworkReply.h"
#include "QCNetworkReply_p.h"
#include "private/QCCurlMultiManagerSocketInfo_p.h"
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

namespace {

[[nodiscard]] bool isTerminalReplyState(ReplyState state)
{
    return state == ReplyState::Cancelled || state == ReplyState::Error
           || state == ReplyState::Finished;
}

[[nodiscard]] QString managerReadinessError(bool isReady,
                                            CURLM *multiHandle,
                                            const QString &initializationError)
{
    if (isReady && multiHandle) {
        return {};
    }

    return initializationError.isEmpty() ? QStringLiteral("curl multi manager 未就绪")
                                         : initializationError;
}

[[nodiscard]] QSharedPointer<QCCurlMultiTransferRecord> createBoundTransferRecord(
    QCNetworkReplyPrivate *replyPrivate, QCNetworkReply *reply, QString *errorMessage)
{
    auto record = QSharedPointer<QCCurlMultiTransferRecord>::create(
        std::move(replyPrivate->curlManager));
    record->bindObserver(replyPrivate->transferState, reply);
    if (record->bindCallbacks(replyPrivate->readCallbackConfigured,
                              replyPrivate->debugCallbackConfigured,
                              errorMessage)) {
        return record;
    }

    replyPrivate->curlManager = record->takeHandle();
    record->clearObserver();
    return {};
}

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
    QHash<curl_socket_t, QSharedPointer<SocketInfo>> sockets;
    CURLM *multiHandle = nullptr;
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

QCCurlMultiManager *QCCurlMultiManager::instance()
{
    // 线程内单例：每个线程拥有独立的 multi engine，避免同一 CURLM* 被跨线程并发访问
    static thread_local QCCurlMultiManager s_instance;
    return &s_instance;
}

QCCurlMultiManager::QCCurlMultiManager(QObject *parent)
    : QObject(parent)
    , m_runtimeLease(Internal::acquireRuntimeLease())
    , m_multiHandle(nullptr)
    , m_socketTimer(nullptr)
{
    qDebug() << "QCCurlMultiManager: Initializing global instance";

    if (!m_runtimeLease.isValid()) {
        m_initializationError = m_runtimeLease.diagnostic();
        qCritical().noquote() << "QCCurlMultiManager:" << m_initializationError;
        return;
    }

    // 初始化 curl multi handle
    m_multiHandle = Internal::CurlOptions::createMultiHandle();
    if (!m_multiHandle) {
        m_initializationError = QStringLiteral("curl_multi_init 失败");
        qCritical() << "QCCurlMultiManager: Failed to initialize curl multi handle";
        return;
    }

    if (!configureMultiCallbacks("QCCurlMultiManager")) {
        m_initializationError = QStringLiteral("配置 curl multi 回调失败");
        disableMultiCallbacks();
        curl_multi_cleanup(m_multiHandle);
        m_multiHandle = nullptr;
        qCritical() << "QCCurlMultiManager: Failed to configure curl multi callbacks";
        return;
    }

    // 创建定时器
    m_socketTimer = new QTimer(this);
    m_socketTimer->setSingleShot(true);

    connect(m_socketTimer, &QTimer::timeout, this, [this]() {
        qDebug() << "QCCurlMultiManager: Socket timeout triggered";
        handleSocketAction(CURL_SOCKET_TIMEOUT, 0);
    });

    m_runtimeParticipant = Internal::registerRuntimeShutdownParticipant(this, [this]() {
        shutdown(false);
    });
    if (m_runtimeParticipant == 0) {
        m_initializationError = QStringLiteral("QCurlRuntime 已停止 admission");
        disableMultiCallbacks();
        curl_multi_cleanup(m_multiHandle);
        m_multiHandle = nullptr;
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

    if (m_isPoisoned.load(std::memory_order_relaxed)) {
        QList<QSharedPointer<SocketInfo>> sockets;
        {
            QMutexLocker locker(&m_mutex);
            sockets = m_socketMap.values();
        }
        disableSocketsForShutdown(sockets);
        if (m_socketTimer) {
            m_socketTimer->stop();
        }
        if (retainPoisonedGraph) {
            retainPoisonedObjectGraph();
        }
        releaseRuntimeParticipation();
        return;
    }

    // 先禁用 libcurl 回调，避免在清理过程中重入导致死锁
    disableMultiCallbacks();

    // 停止定时器
    if (m_socketTimer) {
        m_socketTimer->stop();
    }

    QList<CURL *> activeHandles;
    QList<QSharedPointer<SocketInfo>> sockets;
    QList<QSharedPointer<ShareContext>> shareContexts;
    {
        QMutexLocker locker(&m_mutex);
        activeHandles = m_activeTransfers.keys();
        sockets       = m_socketMap.values();
        shareContexts = m_shareContexts.values();
    }

    cleanupActiveHandlesForShutdown(activeHandles);

    if (m_isPoisoned.load(std::memory_order_relaxed)) {
        if (retainPoisonedGraph) {
            retainPoisonedObjectGraph();
        }
        releaseRuntimeParticipation();
        return;
    }

    disableSocketsForShutdown(sockets);
    cleanupShareContextsForShutdown(shareContexts);

    if (m_isPoisoned.load(std::memory_order_relaxed)) {
        if (retainPoisonedGraph) {
            retainPoisonedObjectGraph();
        }
        releaseRuntimeParticipation();
        return;
    }

    {
        QMutexLocker locker(&m_mutex);
        m_shareContexts.clear();
        m_easyToShareContext.clear();
        m_easyShareOptionSet.clear();
        m_socketMap.clear();
    }

    // 清理 multi handle
    if (m_multiHandle) {
        curl_multi_cleanup(m_multiHandle);
        m_multiHandle = nullptr;
    }

    releaseRuntimeParticipation();
    qDebug() << "QCCurlMultiManager: Destruction complete";
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

void QCCurlMultiManager::addReply(QCNetworkReply *reply)
{
    if (QThread::currentThread() != thread()) {
        qWarning() << "QCCurlMultiManager::addReply: owner thread required";
        return;
    }
    Q_ASSERT_X(QThread::currentThread() == thread(),
               "QCCurlMultiManager::addReply",
               "manager owner thread required");

    if (!reply) {
        qWarning() << "QCCurlMultiManager::addReply: reply is null";
        return;
    }

    QPointer<QCNetworkReply> safeReply(reply);
    const AddReplyResult result = tryAddReplyOnOwnerThread(reply);
    if (!result.success) {
        queueAddReplyFailure(safeReply, result.errorMessage);
    }
}

QCCurlMultiManager::AddReplyResult QCCurlMultiManager::tryAddReplyOnOwnerThread(QCNetworkReply *reply)
{
    Q_ASSERT(QThread::currentThread() == thread());

    if (m_isShuttingDown.load(std::memory_order_relaxed)) {
        return {false, QStringLiteral("curl multi manager 正在关闭")};
    }
    if (const QString reason = managerReadinessError(m_isReady,
                                                     m_multiHandle,
                                                     m_initializationError);
        !reason.isEmpty()) {
        return {false, reason};
    }

    QMutexLocker locker(&m_mutex);
    QCNetworkReplyPrivate *replyPrivate = reply->d_func();
    if (isTerminalReplyState(replyPrivate->state)) {
        return {true, QString()};
    }

    CURL *easy = validatedEasyHandle(reply);
    if (!easy) {
        return {false, QStringLiteral("reply 的 curl easy handle 无效")};
    }
    if (m_activeTransfers.contains(easy)) {
        qWarning() << "QCCurlMultiManager::addReply: easy handle already registered";
        return {false, QStringLiteral("curl easy handle 已注册")};
    }

    QString callbackError;
    auto record = createBoundTransferRecord(replyPrivate, reply, &callbackError);
    if (!record) {
        return {false, callbackError};
    }

    ShareContext *shareContext         = prepareShareForReplyLocked(reply, easy);
    const ShareApplyResult shareResult = applyShareToEasyLocked(reply, easy, shareContext);
    if (shareResult.state == ShareApplyState::NonCallable) {
        quarantineUnregisteredTransferLocked(std::move(record), easy, shareContext);
        return {false, shareResult.errorMessage};
    }

    QString addError;
    if (!addEasyToMultiLocked(easy, &addError)) {
        releaseShareForEasyHandleLocked(easy);
        replyPrivate->curlManager = record->takeHandle();
        record->clearObserver();
        return {false, addError};
    }

    replyPrivate->multiTransferRecord = record.data();
    m_activeTransfers.insert(easy, record);
    m_runningRequests.fetch_add(1, std::memory_order_relaxed);
#ifdef QCURL_ENABLE_TEST_HOOKS
    reply->setProperty("_qcurl_transfer_record_owns_state",
                       record->ownsTransferState(replyPrivate->transferState));
#endif
    // scheduler 的 start handoff 依赖 multi loop 及时被唤醒，否则 execute() 后可能延迟首轮 poll。
    wakeup();

    qDebug() << "QCCurlMultiManager::addReply: Added reply" << reply
             << "Total running:" << m_runningRequests.load();
    return {true, QString()};
}

void QCCurlMultiManager::queueAddReplyFailure(const QPointer<QCNetworkReply> &reply,
                                              const QString &message)
{
    if (!reply) {
        return;
    }

    const QString resolvedMessage = message.isEmpty() ? QStringLiteral("curl multi 注册失败")
                                                      : message;
    QMetaObject::invokeMethod(
        reply.data(),
        [reply, resolvedMessage]() {
            if (reply) {
                reply->abortWithError(NetworkError::InvalidRequest, resolvedMessage);
            }
        },
        Qt::QueuedConnection);
}

void QCCurlMultiManager::removeReply(QCNetworkReply *reply)
{
    if (QThread::currentThread() != thread()) {
        qWarning() << "QCCurlMultiManager::removeReply: owner thread required";
        return;
    }
    Q_ASSERT_X(QThread::currentThread() == thread(),
               "QCCurlMultiManager::removeReply",
               "manager owner thread required");

    if (!reply) {
        return;
    }

    if (m_isShuttingDown.load(std::memory_order_relaxed)) {
        return;
    }

    removeTransferRecord(reply->d_func()->multiTransferRecord);
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

    if (m_socketTimer) {
        m_socketTimer->stop();
    }
    for (const auto &socketHolder : std::as_const(m_socketMap)) {
        if (!socketHolder) {
            continue;
        }
        if (socketHolder->readNotifier) {
            socketHolder->readNotifier->setEnabled(false);
        }
        if (socketHolder->writeNotifier) {
            socketHolder->writeNotifier->setEnabled(false);
        }
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
    graph.activeTransfers = std::move(m_activeTransfers);
    graph.shareContexts   = std::move(m_shareContexts);
    graph.shareBindings   = std::move(m_easyToShareContext);
    graph.shareOptions    = std::move(m_easyShareOptionSet);
    graph.sockets         = std::move(m_socketMap);
    graph.multiHandle     = std::exchange(m_multiHandle, nullptr);
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
        if (context->activeUsers != 0) {
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

CURL *QCCurlMultiManager::validatedEasyHandle(QCNetworkReply *reply) const
{
    CURL *easy = reply->d_func()->curlManager.handle();
    if (!easy) {
        qCritical() << "QCCurlMultiManager::addReply: reply has invalid curl handle";
    }
    return easy;
}

bool QCCurlMultiManager::addEasyToMultiLocked(CURL *easy)
{
    const CURLMcode ret = Internal::CurlOptions::addMultiHandle(m_multiHandle, easy);
    if (ret == CURLM_OK) {
        return true;
    }

    qCritical() << "QCCurlMultiManager::addReply: curl_multi_add_handle failed:" << ret;
    return false;
}

bool QCCurlMultiManager::addEasyToMultiLocked(CURL *easy, QString *error)
{
    if (addEasyToMultiLocked(easy)) {
        return true;
    }
    if (error) {
        *error = QStringLiteral("curl_multi_add_handle 失败");
    }
    return false;
}

} // namespace QCurl
