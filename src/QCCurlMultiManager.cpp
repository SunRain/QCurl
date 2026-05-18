#include "QCCurlMultiManager.h"

#include "QCNetworkAccessManager.h"
#include "QCNetworkReply.h"
#include "QCNetworkReply_p.h"

#include <QDebug>
#include <QList>
#include <QMutexLocker>
#include <QThread>
#include <QTimer>

namespace QCurl {

QCCurlMultiManager *QCCurlMultiManager::instance()
{
    // 线程内单例：每个线程拥有独立的 multi engine，避免同一 CURLM* 被跨线程并发访问
    static thread_local QCCurlMultiManager s_instance;
    return &s_instance;
}

QCCurlMultiManager::QCCurlMultiManager(QObject *parent)
    : QObject(parent)
    , m_multiHandle(nullptr)
    , m_socketTimer(nullptr)
{
    qDebug() << "QCCurlMultiManager: Initializing global instance";

    // 初始化 curl multi handle
    m_multiHandle = curl_multi_init();
    if (!m_multiHandle) {
        qCritical() << "QCCurlMultiManager: Failed to initialize curl multi handle";
        return;
    }

    configureMultiCallbacks("QCCurlMultiManager");

    // 创建定时器
    m_socketTimer = new QTimer(this);
    m_socketTimer->setSingleShot(true);

    connect(m_socketTimer, &QTimer::timeout, this, [this]() {
        qDebug() << "QCCurlMultiManager: Socket timeout triggered";
        handleSocketAction(CURL_SOCKET_TIMEOUT, 0);
    });

    qDebug() << "QCCurlMultiManager: Initialization complete";
}

QCCurlMultiManager::~QCCurlMultiManager()
{
    qDebug() << "QCCurlMultiManager: Destroying instance";

    m_isShuttingDown.store(true, std::memory_order_relaxed);

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
        activeHandles = m_activeReplies.keys();
        sockets       = m_socketMap.values();
        shareContexts = m_shareContexts.values();
        m_activeReplies.clear();
        m_socketMap.clear();
        m_shareContexts.clear();

        m_easyToShareContext.clear();
        m_easyShareOptionSet.clear();
    }

    cleanupActiveHandlesForShutdown(activeHandles);
    disableSocketsForShutdown(sockets);
    cleanupShareContextsForShutdown(shareContexts);

    // 清理 multi handle
    if (m_multiHandle) {
        curl_multi_cleanup(m_multiHandle);
        m_multiHandle = nullptr;
    }

    qDebug() << "QCCurlMultiManager: Destruction complete";
}

void QCCurlMultiManager::addReply(QCNetworkReply *reply)
{
    if (!reply) {
        qWarning() << "QCCurlMultiManager::addReply: reply is null";
        return;
    }

    if (m_isShuttingDown.load(std::memory_order_relaxed)) {
        qWarning() << "QCCurlMultiManager::addReply: manager is shutting down";
        return;
    }

    if (marshalAddReplyIfNeeded(reply)) {
        return;
    }

    QMutexLocker locker(&m_mutex);
    CURL *easy = validatedEasyHandle(reply);
    if (!easy || !registerActiveReplyLocked(easy, reply)) {
        return;
    }

    applyShareToEasyLocked(reply, easy, prepareShareForReplyLocked(reply, easy));
    if (!addEasyToMultiLocked(easy)) {
        cleanupEasyHandleLocked(easy, false, false, "QCCurlMultiManager::addReply");
        return;
    }

    m_runningRequests.fetch_add(1, std::memory_order_relaxed);
    // scheduler 的 start handoff 依赖 multi loop 及时被唤醒，否则 execute() 后可能延迟首轮 poll。
    wakeup();

    qDebug() << "QCCurlMultiManager::addReply: Added reply" << reply
             << "Total running:" << m_runningRequests.load();
}

void QCCurlMultiManager::removeReply(QCNetworkReply *reply)
{
    if (!reply) {
        return;
    }

    if (m_isShuttingDown.load(std::memory_order_relaxed)) {
        return;
    }

    // multi engine 只允许在所属线程串行访问；跨线程调用应投递到本线程执行
    if (QThread::currentThread() != thread()) {
        QPointer<QCNetworkReply> safeReply(reply);
        QMetaObject::invokeMethod(
            this,
            [this, safeReply]() {
                if (safeReply) {
                    removeReply(safeReply.data());
                }
            },
            Qt::QueuedConnection);
        return;
    }

    QMutexLocker locker(&m_mutex);

    CURL *easy = reply->d_func()->curlManager.handle();
    if (!easy) {
        return;
    }

    // 检查是否存在
    if (!m_activeReplies.contains(easy)) {
        return;
    }

    cleanupEasyHandleLocked(easy, true, true, "QCCurlMultiManager::removeReply");

    qDebug() << "QCCurlMultiManager::removeReply: Removed reply" << reply
             << "Remaining:" << m_runningRequests.load();
}

int QCCurlMultiManager::runningRequestsCount() const noexcept
{
    return m_runningRequests.load(std::memory_order_relaxed);
}

void QCCurlMultiManager::cleanupActiveHandlesForShutdown(const QList<CURL *> &activeHandles)
{
    if (!activeHandles.isEmpty()) {
        qWarning() << "QCCurlMultiManager: Destroying with" << activeHandles.size()
                   << "active requests";
    }
    for (CURL *easy : activeHandles) {
        if (easy && m_multiHandle) {
            curl_multi_remove_handle(m_multiHandle, easy);
        }
    }
}

void QCCurlMultiManager::disableSocketsForShutdown(
    const QList<QSharedPointer<SocketInfo>> &sockets)
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
    for (const auto &contextHolder : shareContexts) {
        ShareContext *context = contextHolder.data();
        if (context && context->share) {
            curl_share_cleanup(context->share);
            context->share = nullptr;
        }
    }
}

bool QCCurlMultiManager::marshalAddReplyIfNeeded(QCNetworkReply *reply)
{
    if (QThread::currentThread() == thread()) {
        return false;
    }

    QPointer<QCNetworkReply> safeReply(reply);
    QMetaObject::invokeMethod(
        this,
        [this, safeReply]() {
            if (safeReply) {
                addReply(safeReply.data());
            }
        },
        Qt::QueuedConnection);
    return true;
}

CURL *QCCurlMultiManager::validatedEasyHandle(QCNetworkReply *reply) const
{
    CURL *easy = reply->d_func()->curlManager.handle();
    if (!easy) {
        qCritical() << "QCCurlMultiManager::addReply: reply has invalid curl handle";
    }
    return easy;
}

bool QCCurlMultiManager::registerActiveReplyLocked(CURL *easy, QCNetworkReply *reply)
{
    if (m_activeReplies.contains(easy)) {
        qWarning() << "QCCurlMultiManager::addReply: easy handle already registered";
        return false;
    }

    m_activeReplies.insert(easy, QPointer<QCNetworkReply>(reply));
    return true;
}

bool QCCurlMultiManager::addEasyToMultiLocked(CURL *easy)
{
    const CURLMcode ret = curl_multi_add_handle(m_multiHandle, easy);
    if (ret == CURLM_OK) {
        return true;
    }

    qCritical() << "QCCurlMultiManager::addReply: curl_multi_add_handle failed:" << ret;
    return false;
}

} // namespace QCurl
