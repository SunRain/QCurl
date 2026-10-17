#include "QCCurlMultiManager.h"
#include "private/QCCurlMultiDomain_p.h"
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

bool QCCurlMultiManager::configureMultiCallbacks(QCCurlMultiDomain *domain, const char *context)
{
    if (!domain->multi) {
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
        const CURLMcode ret = curl_multi_setopt(domain->multi, option, value);
        if (ret != CURLM_OK) {
            qCritical() << context << ": Failed to set" << name << ":" << ret;
            ok = false;
        }
    };

    setCallback(CURLMOPT_SOCKETDATA, domain, "CURLMOPT_SOCKETDATA");
    setCallback(CURLMOPT_SOCKETFUNCTION, curlSocketCallback, "CURLMOPT_SOCKETFUNCTION");
    setCallback(CURLMOPT_TIMERDATA, domain, "CURLMOPT_TIMERDATA");
    setCallback(CURLMOPT_TIMERFUNCTION, curlTimerCallback, "CURLMOPT_TIMERFUNCTION");
    return ok;
}

void QCCurlMultiManager::disableMultiCallbacks(QCCurlMultiDomain *domain)
{
    if (!domain->multi) {
        return;
    }

    curl_multi_setopt(domain->multi, CURLMOPT_SOCKETDATA, nullptr);
    curl_multi_setopt(domain->multi, CURLMOPT_SOCKETFUNCTION, nullptr);
    curl_multi_setopt(domain->multi, CURLMOPT_TIMERDATA, nullptr);
    curl_multi_setopt(domain->multi, CURLMOPT_TIMERFUNCTION, nullptr);
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

    for (const auto &domain : allDomains()) {
        wakeupDomain(domain);
    }
}

void QCCurlMultiManager::wakeupDomain(const Domain &domain)
{
    if (!domain || !domain->multi || domain->wakeupQueued) {
        return;
    }
    const auto code = Internal::CurlOptions::wakeupMultiHandle(domain->multi);
    if (code != CURLM_OK) {
        qWarning() << "curl_multi_wakeup failed:" << code;
    }
    domain->wakeupQueued = true;
    const std::weak_ptr<QCCurlMultiDomain> weakDomain(domain);
    QTimer::singleShot(0, this, [this, weakDomain]() {
        if (const auto retained = weakDomain.lock()) {
            retained->wakeupQueued = false;
            handleSocketAction(retained.get(), CURL_SOCKET_TIMEOUT, 0);
        }
    });
}

void QCCurlMultiManager::handleSocketAction(QCCurlMultiDomain *domain,
                                            curl_socket_t socketfd,
                                            int eventsBitmask)
{
    if (m_isPoisoned.load(std::memory_order_relaxed)
        || m_isShuttingDown.load(std::memory_order_relaxed) || !domain->multi) {
        return;
    }

    qDebug() << "QCCurlMultiManager::handleSocketAction: socketfd=" << socketfd
             << "events=" << eventsBitmask;

#ifdef QCURL_ENABLE_TEST_HOOKS
    ++m_testSocketActionCount;
#endif
    int runningHandles = 0;
    CURLMcode ret      = curl_multi_socket_action(domain->multi,
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
    checkMultiInfo(domain);
}

void QCCurlMultiManager::checkMultiInfo(QCCurlMultiDomain *domain)
{
    if (m_isPoisoned.load(std::memory_order_relaxed)
        || m_isShuttingDown.load(std::memory_order_relaxed) || !domain->multi) {
        return;
    }

    int messagesLeft = 0;

    do {
        CURLMsg *message = curl_multi_info_read(domain->multi, &messagesLeft);
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

void QCCurlMultiManager::cleanupSocket(QCCurlMultiDomain *domain, curl_socket_t socketfd)
{
    QMutexLocker locker(&m_mutex);

    auto it = domain->sockets.find(socketfd);
    if (it == domain->sockets.end()) {
        return;
    }
    SocketInfo *info = it.value().data();

    qDebug() << "QCCurlMultiManager::cleanupSocket: Cleaning socket" << socketfd;

    // 从 libcurl 解除关联
    curl_multi_assign(domain->multi, socketfd, nullptr);

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

    domain->sockets.erase(it);
}

int QCCurlMultiManager::manageSocketNotifiers(QCCurlMultiDomain *domain,
                                              curl_socket_t socketfd,
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
        cleanupSocket(domain, socketfd);
        return 0;
    }

    // CURL_POLL_NONE: 无操作
    if (what == CURL_POLL_NONE) {
        return 0;
    }

    socketInfo = ensureSocketInfo(domain, socketfd, socketInfo);
    updateReadNotifier(domain, socketInfo, what);
    updateWriteNotifier(domain, socketInfo, what);

    return 0;
}

SocketInfo *QCCurlMultiManager::ensureSocketInfo(QCCurlMultiDomain *domain,
                                                 curl_socket_t socketfd,
                                                 SocketInfo *socketInfo)
{
    if (socketInfo) {
        return socketInfo;
    }

    QMutexLocker locker(&m_mutex);
    auto it = domain->sockets.find(socketfd);
    if (it != domain->sockets.end()) {
        socketInfo = it.value().data();
    } else {
        auto newInfo      = QSharedPointer<SocketInfo>::create();
        newInfo->socketfd = socketfd;
        socketInfo        = newInfo.data();
        domain->sockets.insert(socketfd, newInfo);
    }

    curl_multi_assign(domain->multi, socketfd, socketInfo);
    return socketInfo;
}

void QCCurlMultiManager::updateReadNotifier(QCCurlMultiDomain *domain,
                                            SocketInfo *socketInfo,
                                            int what)
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
        const auto weakDomain        = domain->weakSelf;
        connect(socketInfo->readNotifier,
                &QSocketNotifier::activated,
                this,
                [this, weakDomain, socketfd]() {
                    if (const auto retained = weakDomain.lock()) {
                        handleSocketAction(retained.get(), socketfd, CURL_CSELECT_IN);
                    }
                });
    }
    socketInfo->readNotifier->setEnabled(true);
}

void QCCurlMultiManager::updateWriteNotifier(QCCurlMultiDomain *domain,
                                             SocketInfo *socketInfo,
                                             int what)
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
        const auto weakDomain        = domain->weakSelf;
        connect(socketInfo->writeNotifier,
                &QSocketNotifier::activated,
                this,
                [this, weakDomain, socketfd]() {
                    if (const auto retained = weakDomain.lock()) {
                        handleSocketAction(retained.get(), socketfd, CURL_CSELECT_OUT);
                    }
                });
    }
    socketInfo->writeNotifier->setEnabled(true);
}

int QCCurlMultiManager::curlSocketCallback(
    CURL *easy, curl_socket_t socketfd, int what, void *userp, void *socketp)
{
    Q_UNUSED(easy);

    auto *domain  = static_cast<QCCurlMultiDomain *>(userp);
    auto *manager = domain ? domain->owner : nullptr;
    if (!manager) {
        qCritical() << "QCCurlMultiManager::curlSocketCallback: Invalid manager pointer";
        return -1;
    }

    auto *socketInfo = static_cast<SocketInfo *>(socketp);

    return manager->manageSocketNotifiers(domain, socketfd, what, socketInfo);
}

int QCCurlMultiManager::curlTimerCallback(CURLM *multi, long timeout_ms, void *userp)
{
    Q_UNUSED(multi);

    auto *domain  = static_cast<QCCurlMultiDomain *>(userp);
    auto *manager = domain ? domain->owner : nullptr;
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
        domain->timer->start(timeoutMs);
    } else {
        domain->timer->stop();
    }

    return 0;
}

} // namespace QCurl
