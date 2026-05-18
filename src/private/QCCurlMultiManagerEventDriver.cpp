#include "QCCurlMultiManager.h"

#include "QCNetworkReply.h"
#include "QCNetworkReply_p.h"

#include <QDebug>
#include <QMetaObject>
#include <QMutexLocker>
#include <QSocketNotifier>
#include <QThread>
#include <QTimer>

#include <limits>

namespace QCurl {

bool QCCurlMultiManager::configureMultiCallbacks(const char *context)
{
    if (!m_multiHandle) {
        return false;
    }

    bool ok = true;
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
    if (m_socketTimer) {
        m_socketTimer->stop();
    }

    if (m_multiHandle) {
        disableMultiCallbacks();
        curl_multi_cleanup(m_multiHandle);
        m_multiHandle = nullptr;
    }

    m_multiHandle = curl_multi_init();
    if (!m_multiHandle) {
        qCritical() << "QCCurlMultiManager::applyLimitsConfig: Failed to reinitialize curl multi handle";
        return false;
    }

    return configureMultiCallbacks("QCCurlMultiManager::applyLimitsConfig");
}

void QCCurlMultiManager::wakeup()
{
    if (m_isShuttingDown.load(std::memory_order_relaxed)) {
        return;
    }

    if (QThread::currentThread() != thread()) {
        QMetaObject::invokeMethod(this, [this]() { wakeup(); }, Qt::QueuedConnection);
        return;
    }

    if (!m_multiHandle) {
        return;
    }

#if defined(LIBCURL_VERSION_NUM) && (LIBCURL_VERSION_NUM >= 0x074400)
    const CURLMcode wakeupCode = curl_multi_wakeup(m_multiHandle);
    if (wakeupCode != CURLM_OK) {
        qWarning() << "QCCurlMultiManager::wakeup: curl_multi_wakeup failed:" << wakeupCode;
    }
#endif

    QTimer::singleShot(0, this, [this]() { handleSocketAction(CURL_SOCKET_TIMEOUT, 0); });
}

void QCCurlMultiManager::handleSocketAction(curl_socket_t socketfd, int eventsBitmask)
{
    if (m_isShuttingDown.load(std::memory_order_relaxed)) {
        return;
    }

    qDebug() << "QCCurlMultiManager::handleSocketAction: socketfd=" << socketfd
             << "events=" << eventsBitmask;

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
    if (m_isShuttingDown.load(std::memory_order_relaxed)) {
        return;
    }

    int messagesLeft = 0;

    do {
        CURLMsg *message = curl_multi_info_read(m_multiHandle, &messagesLeft);
        if (!message) {
            break;
        }
        const auto finishedTransfer = takeFinishedTransferLocked(message);
        if (finishedTransfer.has_value()) {
            dispatchFinishedTransfer(finishedTransfer.value());
        }
    } while (messagesLeft > 0);
}

std::optional<QCCurlMultiManager::FinishedTransfer>
QCCurlMultiManager::takeFinishedTransferLocked(CURLMsg *message)
{
    if (!message || message->msg != CURLMSG_DONE || !message->easy_handle) {
        return std::nullopt;
    }

    CURL *easy = message->easy_handle;
    QMutexLocker locker(&m_mutex);
    QPointer<QCNetworkReply> safeReply = m_activeReplies.value(easy);
    if (!safeReply) {
        qWarning() << "QCCurlMultiManager::checkMultiInfo: Reply object already destroyed";
        cleanupEasyHandleLocked(easy, true, true, "QCCurlMultiManager::checkMultiInfo");
        return std::nullopt;
    }

    long responseCode = 0;
    curl_easy_getinfo(easy, CURLINFO_RESPONSE_CODE, &responseCode);

    char *redirectUrl = nullptr;
    curl_easy_getinfo(easy, CURLINFO_REDIRECT_URL, &redirectUrl);

    qDebug() << "QCCurlMultiManager::checkMultiInfo: Request finished"
             << "Reply:" << safeReply.data() << "CURLcode:" << message->data.result
             << "HTTP code:" << responseCode << "Redirect:" << (redirectUrl ? redirectUrl : "none");

    cleanupEasyHandleLocked(easy, true, true, "QCCurlMultiManager::checkMultiInfo");
    return FinishedTransfer{safeReply, message->data.result};
}

void QCCurlMultiManager::dispatchFinishedTransfer(const FinishedTransfer &transfer)
{
    if (!transfer.reply) {
        return;
    }

    QPointer<QCNetworkReply> safeReply = transfer.reply;
    const CURLcode curlCode            = transfer.curlCode;
    QMetaObject::invokeMethod(
        safeReply.data(),
        [safeReply, curlCode]() {
            if (safeReply) {
                safeReply->d_func()->onCurlMultiFinished(curlCode);
            }
        },
        Qt::AutoConnection);

    if (safeReply) {
        emit requestFinished(safeReply.data(), static_cast<int>(curlCode));
    }
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
    if (m_isShuttingDown.load(std::memory_order_relaxed)) {
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
        socketInfo->readNotifier = new QSocketNotifier(socketfd, QSocketNotifier::Read, this);
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
        socketInfo->writeNotifier = new QSocketNotifier(socketfd, QSocketNotifier::Write, this);
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

    if (manager->m_isShuttingDown.load(std::memory_order_relaxed)) {
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
