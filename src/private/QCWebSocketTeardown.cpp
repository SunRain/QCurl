/**
 * @file
 * @brief 实现 QCWebSocket 的资源撤销与 header backing quarantine。
 */

#include "QCWebSocket_p.h"

#ifdef QCURL_WEBSOCKET_SUPPORT

#include <QMutex>
#include <QMutexLocker>
#include <QSocketNotifier>
#include <QTimer>

#include <utility>
#include <vector>

namespace QCurl {

namespace {

namespace CurlOptions = Internal::CurlOptions;

/**
 * @brief 保存无法证明已从 easy handle 解绑的 header backing storage。
 *
 * registry 具有进程生命周期，持有对象只允许保留，禁止再次传入任何 libcurl API。
 */
struct ProcessHeaderBackingQuarantine final
{
    QMutex mutex;
    std::vector<CurlOptions::CurlSlistOwner> headerLists;
};

[[nodiscard]] ProcessHeaderBackingQuarantine &processHeaderBackingQuarantine()
{
    static auto *s_quarantine = new ProcessHeaderBackingQuarantine;
    return *s_quarantine;
}

/// 把仍可能被 easy handle 引用的 header list 转移到进程期 NonCallable quarantine。
void retainNonCallableHeaderBacking(CurlOptions::CurlSlistOwner &&headers)
{
    if (!headers) {
        return;
    }

    ProcessHeaderBackingQuarantine &quarantine = processHeaderBackingQuarantine();
    QMutexLocker locker(&quarantine.mutex);
    quarantine.headerLists.push_back(std::move(headers));
}

void stopTimer(QTimer *&timer, QCWebSocket *owner)
{
    if (!timer) {
        return;
    }
    timer->stop();
    QObject::disconnect(timer, nullptr, owner, nullptr);
    timer = nullptr;
}

void disableNotifier(QSocketNotifier *&notifier, QCWebSocket *owner)
{
    if (!notifier) {
        return;
    }
    notifier->setEnabled(false);
    QObject::disconnect(notifier, nullptr, owner, nullptr);
    notifier = nullptr;
}

} // namespace

QCWebSocketPrivate::QCWebSocketPrivate(QCWebSocket *parent)
    : q_ptr(parent)
{
    resetTransport();
}

QCWebSocketPrivate::~QCWebSocketPrivate()
{
    teardownForDestruction();
}

void QCWebSocketPrivate::teardownForDestruction()
{
    if (destructionTeardownComplete) {
        return;
    }
    destructionTeardownComplete = true;

    const quintptr token = transferToken;
    transferToken        = 0;
    resetTransport();

    disableNotifier(socketReadNotifier, q_ptr);
    disableNotifier(socketWriteNotifier, q_ptr);
    stopTimer(receiveTimer, q_ptr);
    stopTimer(reconnectTimer, q_ptr);
    stopTimer(closeTimer, q_ptr);

    eventDrivenMode = false;
    fragmentBuffer.clear();
    fragmentTypeFlags = 0;
    currentFrameBytes = 0;
    controlFrameBuffer.clear();
    controlFrameFlags = 0;
    sendQueue.clear();
    closeFrameSent        = false;
    peerCloseReceived     = false;
    hasRetriableCloseCode = false;
    state                 = QCWebSocket::State::Closed;

    if (token != 0) {
        removePersistentTransfer(token);
    }
}

void QCWebSocketPrivate::resetTransport()
{
    if (!clearRequestHeaders()) {
        retainNonCallableHeaderBacking(std::move(requestHeaders));
    }
    managedCurlHandle = nullptr;
}

bool QCWebSocketPrivate::clearRequestHeaders()
{
    if (!requestHeaders) {
        return true;
    }

    CURL *curl = transportHandle();
    if (curl) {
        const CURLcode result = CurlOptions::setHttpHeaderList(curl, nullptr);
        if (result != CURLE_OK) {
            return false;
        }
    }

    requestHeaders.reset();
    return true;
}

#ifdef QCURL_ENABLE_TEST_HOOKS
namespace Internal {

int quarantinedWebSocketHeaderBackingCountForTest() noexcept
{
    ProcessHeaderBackingQuarantine &quarantine = processHeaderBackingQuarantine();
    QMutexLocker locker(&quarantine.mutex);
    return static_cast<int>(quarantine.headerLists.size());
}

} // namespace Internal
#endif

} // namespace QCurl

#endif // QCURL_WEBSOCKET_SUPPORT
