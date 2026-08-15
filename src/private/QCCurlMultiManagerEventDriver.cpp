#include "QCCurlMultiManager.h"
#include "private/QCCurlMultiManagerSocketInfo_p.h"
#include "private/QCCurlOptionAdapter_p.h"
#include "private/QCurlRuntimeState_p.h"

#include <QDebug>
#include <QMetaObject>
#include <QMutexLocker>
#include <QSocketNotifier>
#include <QThread>
#include <QTimer>

#include <limits>
#include <utility>

namespace QCurl {

bool QCCurlMultiManager::configureMultiCallbacks(const char *context)
{
    if (!m_multiHandle) {
        return false;
    }
#ifdef QCURL_ENABLE_TEST_HOOKS
    if (Internal::CurlOptions::shouldForceMultiFailure("callbacks")) {
        qCritical() << context << ": Forced curl multi callback configuration failure";
        return false;
    }
#endif

    bool ok          = true;
    auto setCallback = [&](CURLMoption option, auto value, const char *name) {
        const CURLMcode ret = curl_multi_setopt(m_multiHandle, option, value);
        if (ret != CURLM_OK) {
            qCritical() << context << ": Failed to set" << name << ":" << ret;
            ok = false;
        }
    };

    setCallback(CURLMOPT_SOCKETDATA, this, "CURLMOPT_SOCKETDATA");
    setCallback(CURLMOPT_SOCKETFUNCTION, curlSocketCallback, "CURLMOPT_SOCKETFUNCTION");
    setCallback(CURLMOPT_TIMERDATA, this, "CURLMOPT_TIMERDATA");
    setCallback(CURLMOPT_TIMERFUNCTION, curlTimerCallback, "CURLMOPT_TIMERFUNCTION");
    return ok;
}

void QCCurlMultiManager::disableMultiCallbacks()
{
    if (!m_multiHandle) {
        return;
    }

    curl_multi_setopt(m_multiHandle, CURLMOPT_SOCKETDATA, nullptr);
    curl_multi_setopt(m_multiHandle, CURLMOPT_SOCKETFUNCTION, nullptr);
    curl_multi_setopt(m_multiHandle, CURLMOPT_TIMERDATA, nullptr);
    curl_multi_setopt(m_multiHandle, CURLMOPT_TIMERFUNCTION, nullptr);
}

bool QCCurlMultiManager::recreateMultiHandleForLimits()
{
    m_isReady = false;

    if (!m_runtimeLease.isValid()) {
        m_initializationError = m_runtimeLease.diagnostic();
        return false;
    }

    if (m_socketTimer) {
        m_socketTimer->stop();
    }

    if (m_multiHandle) {
        disableMultiCallbacks();
        curl_multi_cleanup(m_multiHandle);
        m_multiHandle = nullptr;
    }

    m_multiHandle = Internal::CurlOptions::createMultiHandle();
    if (!m_multiHandle) {
        m_initializationError = QStringLiteral("curl_multi_init 重新初始化失败");
        qCritical()
            << "QCCurlMultiManager::applyLimitsConfig: Failed to reinitialize curl multi handle";
        return false;
    }

    if (!configureMultiCallbacks("QCCurlMultiManager::applyLimitsConfig")) {
        m_initializationError = QStringLiteral("重新配置 curl multi 回调失败");
        disableMultiCallbacks();
        curl_multi_cleanup(m_multiHandle);
        m_multiHandle = nullptr;
        return false;
    }

    m_initializationError.clear();
    m_isReady = true;
    return true;
}

void QCCurlMultiManager::wakeup()
{
    if (m_isPoisoned.load(std::memory_order_relaxed)
        || m_isShuttingDown.load(std::memory_order_relaxed)) {
        return;
    }

    if (QThread::currentThread() != thread()) {
        QMetaObject::invokeMethod(this, [this]() { wakeup(); }, Qt::QueuedConnection);
        return;
    }

    if (!m_multiHandle) {
        return;
    }

    const CURLMcode wakeupCode = Internal::CurlOptions::wakeupMultiHandle(m_multiHandle);
    if (wakeupCode != CURLM_OK) {
        qWarning() << "QCCurlMultiManager::wakeup: curl_multi_wakeup failed:" << wakeupCode;
    }

    QTimer::singleShot(0, this, [this]() { handleSocketAction(CURL_SOCKET_TIMEOUT, 0); });
}

void QCCurlMultiManager::handleSocketAction(curl_socket_t socketfd, int eventsBitmask)
{
    if (m_isPoisoned.load(std::memory_order_relaxed)
        || m_isShuttingDown.load(std::memory_order_relaxed) || !m_multiHandle) {
        return;
    }

    qDebug() << "QCCurlMultiManager::handleSocketAction: socketfd=" << socketfd
             << "events=" << eventsBitmask;

#ifdef QCURL_ENABLE_TEST_HOOKS
    ++m_testSocketActionCount;
#endif
    int runningHandles = 0;
    CURLMcode ret      = curl_multi_socket_action(m_multiHandle,
                                                  socketfd,
                                                  eventsBitmask,
                                                  &runningHandles);

    if (ret != CURLM_OK) {
        qWarning() << "QCCurlMultiManager::handleSocketAction: curl_multi_socket_action failed:"
                   << ret;
        // 不要直接返回，继续检查完成的请求
    }

    qDebug() << "QCCurlMultiManager::handleSocketAction: Running handles:" << runningHandles;

    // 检查完成的请求
    checkMultiInfo();
}

void QCCurlMultiManager::checkMultiInfo()
{
    if (m_isPoisoned.load(std::memory_order_relaxed)
        || m_isShuttingDown.load(std::memory_order_relaxed) || !m_multiHandle) {
        return;
    }

    int messagesLeft = 0;

    do {
        CURLMsg *message = curl_multi_info_read(m_multiHandle, &messagesLeft);
        if (!message) {
            break;
        }
        auto finishedTransfer = takeFinishedTransferLocked(message);
        if (finishedTransfer.has_value()) {
            dispatchFinishedTransfer(std::move(finishedTransfer.value()));
        }
        if (m_isPoisoned.load(std::memory_order_relaxed)) {
            break;
        }
    } while (messagesLeft > 0);
}

void QCCurlMultiManager::cleanupSocket(curl_socket_t socketfd)
{
    QMutexLocker locker(&m_mutex);

    auto it = m_socketMap.find(socketfd);
    if (it == m_socketMap.end()) {
        return;
    }
    SocketInfo *info = it.value().data();

    qDebug() << "QCCurlMultiManager::cleanupSocket: Cleaning socket" << socketfd;

    // 从 libcurl 解除关联
    curl_multi_assign(m_multiHandle, socketfd, nullptr);

    if (info->readNotifier) {
        info->readNotifier->setEnabled(false);
        info->readNotifier->deleteLater();
        info->readNotifier = nullptr;
    }

    if (info->writeNotifier) {
        info->writeNotifier->setEnabled(false);
        info->writeNotifier->deleteLater();
        info->writeNotifier = nullptr;
    }

    m_socketMap.erase(it);
}

int QCCurlMultiManager::manageSocketNotifiers(curl_socket_t socketfd,
                                              int what,
                                              SocketInfo *socketInfo)
{
    if (m_isPoisoned.load(std::memory_order_relaxed)
        || m_isShuttingDown.load(std::memory_order_relaxed)) {
        return 0;
    }

    qDebug() << "QCCurlMultiManager::manageSocketNotifiers: socketfd=" << socketfd
             << "what=" << what;

    // CURL_POLL_REMOVE: 删除 socket
    if (what == CURL_POLL_REMOVE) {
        cleanupSocket(socketfd);
        return 0;
    }

    // CURL_POLL_NONE: 无操作
    if (what == CURL_POLL_NONE) {
        return 0;
    }

    socketInfo = ensureSocketInfo(socketfd, socketInfo);
    updateReadNotifier(socketInfo, what);
    updateWriteNotifier(socketInfo, what);

    return 0;
}

SocketInfo *QCCurlMultiManager::ensureSocketInfo(curl_socket_t socketfd, SocketInfo *socketInfo)
{
    if (socketInfo) {
        return socketInfo;
    }

    QMutexLocker locker(&m_mutex);
    auto it = m_socketMap.find(socketfd);
    if (it != m_socketMap.end()) {
        socketInfo = it.value().data();
    } else {
        auto newInfo      = QSharedPointer<SocketInfo>::create();
        newInfo->socketfd = socketfd;
        socketInfo        = newInfo.data();
        m_socketMap.insert(socketfd, newInfo);
    }

    curl_multi_assign(m_multiHandle, socketfd, socketInfo);
    return socketInfo;
}

void QCCurlMultiManager::updateReadNotifier(SocketInfo *socketInfo, int what)
{
    const bool enableRead = (what == CURL_POLL_IN || what == CURL_POLL_INOUT);
    if (!enableRead) {
        if (socketInfo->readNotifier) {
            socketInfo->readNotifier->setEnabled(false);
        }
        return;
    }

    if (!socketInfo->readNotifier) {
        const curl_socket_t socketfd = socketInfo->socketfd;
        socketInfo->readNotifier     = new QSocketNotifier(socketfd, QSocketNotifier::Read, this);
        connect(socketInfo->readNotifier, &QSocketNotifier::activated, this, [this, socketfd]() {
            qDebug() << "QCCurlMultiManager: Read event on socket" << socketfd;
            handleSocketAction(socketfd, CURL_CSELECT_IN);
        });
    }
    socketInfo->readNotifier->setEnabled(true);
}

void QCCurlMultiManager::updateWriteNotifier(SocketInfo *socketInfo, int what)
{
    const bool enableWrite = (what == CURL_POLL_OUT || what == CURL_POLL_INOUT);
    if (!enableWrite) {
        if (socketInfo->writeNotifier) {
            socketInfo->writeNotifier->setEnabled(false);
        }
        return;
    }

    if (!socketInfo->writeNotifier) {
        const curl_socket_t socketfd = socketInfo->socketfd;
        socketInfo->writeNotifier    = new QSocketNotifier(socketfd, QSocketNotifier::Write, this);
        connect(socketInfo->writeNotifier, &QSocketNotifier::activated, this, [this, socketfd]() {
            qDebug() << "QCCurlMultiManager: Write event on socket" << socketfd;
            handleSocketAction(socketfd, CURL_CSELECT_OUT);
        });
    }
    socketInfo->writeNotifier->setEnabled(true);
}

int QCCurlMultiManager::curlSocketCallback(
    CURL *easy, curl_socket_t socketfd, int what, void *userp, void *socketp)
{
    Q_UNUSED(easy);

    auto *manager = static_cast<QCCurlMultiManager *>(userp);
    if (!manager) {
        qCritical() << "QCCurlMultiManager::curlSocketCallback: Invalid manager pointer";
        return -1;
    }

    auto *socketInfo = static_cast<SocketInfo *>(socketp);

    return manager->manageSocketNotifiers(socketfd, what, socketInfo);
}

int QCCurlMultiManager::curlTimerCallback(CURLM *multi, long timeout_ms, void *userp)
{
    Q_UNUSED(multi);

    auto *manager = static_cast<QCCurlMultiManager *>(userp);
    if (!manager) {
        qCritical() << "QCCurlMultiManager::curlTimerCallback: Invalid manager pointer";
        return -1;
    }

    if (manager->m_isPoisoned.load(std::memory_order_relaxed)
        || manager->m_isShuttingDown.load(std::memory_order_relaxed)) {
        return 0;
    }

    // 转换为 int（避免溢出）
    int timeoutMs = -1;
    if (timeout_ms >= 0) {
        if (timeout_ms >= std::numeric_limits<int>::max()) {
            timeoutMs = std::numeric_limits<int>::max();
        } else {
            timeoutMs = static_cast<int>(timeout_ms);
        }
    }

    qDebug() << "QCCurlMultiManager::curlTimerCallback: timeout=" << timeoutMs << "ms";

    // 启动或停止定时器
    if (timeoutMs >= 0) {
        manager->m_socketTimer->start(timeoutMs);
    } else {
        manager->m_socketTimer->stop();
    }

    return 0;
}

} // namespace QCurl
