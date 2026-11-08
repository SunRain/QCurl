#include "QCNetworkReply_p.h"
#include "QCNetworkTimeoutConfig.h"
#include "private/QCNetworkReplyCallbacks_p.h"
#include "private/QCNetworkReplyExecution_p.h"

#include <QMetaObject>
#include <QThread>

namespace QCurl::Internal {

void QCNetworkReceiveIdleExecution::initialize(QObject *owner,
                                               std::optional<std::chrono::milliseconds> timeout,
                                               std::function<void()> expire)
{
    m_rules.initialize(timeout);
    if (!timeout) {
        return;
    }
    Q_ASSERT(owner && QThread::currentThread() == owner->thread());
    m_timer = new QTimer(owner);
    m_timer->setSingleShot(true);
    m_timer->setTimerType(Qt::PreciseTimer);
    QObject::connect(m_timer.data(), &QTimer::timeout, owner, std::move(expire));
}

void QCNetworkReceiveIdleExecution::beginRequest(CURL *easy)
{
    m_rules.beginRequest(QCNetworkReceiveIdleTimeout::Clock::now());
    if (!m_rules.active()) {
        return;
    }
    m_redirectCount = 0;
    m_redirectTime  = 0;
    if (easy) {
        curl_easy_getinfo(easy, CURLINFO_REDIRECT_COUNT, &m_redirectCount);
        curl_easy_getinfo(easy, CURLINFO_REDIRECT_TIME_T, &m_redirectTime);
    }
    schedule();
}

void QCNetworkReceiveIdleExecution::receivedBytes()
{
    m_rules.receivedBytes(QCNetworkReceiveIdleTimeout::Clock::now());
    schedule();
}

void QCNetworkReceiveIdleExecution::synchronizeConnection(CURL *easy)
{
    if (!m_rules.active() || !easy) {
        return;
    }
    long redirects          = 0;
    curl_off_t redirectTime = 0;
    curl_easy_getinfo(easy, CURLINFO_REDIRECT_COUNT, &redirects);
    curl_easy_getinfo(easy, CURLINFO_REDIRECT_TIME_T, &redirectTime);
    // follow/重连事实先于下一 PREREQ；空 progress 只采样阶段，不能续期。
    if (redirects != m_redirectCount || redirectTime != m_redirectTime) {
        stop();
    }
}

void QCNetworkReceiveIdleExecution::setReceivePaused(bool paused)
{
    m_rules.setReceivePaused(paused, QCNetworkReceiveIdleTimeout::Clock::now());
    schedule();
}

void QCNetworkReceiveIdleExecution::stop()
{
    m_rules.stop();
    if (m_timer) {
        m_timer->stop();
    }
}

void QCNetworkReceiveIdleExecution::reset()
{
    stop();
    m_rules.reset();
}

void QCNetworkReceiveIdleExecution::schedule()
{
    if (!m_timer) {
        return;
    }
    const auto remaining = m_rules.remaining(QCNetworkReceiveIdleTimeout::Clock::now());
    if (remaining) {
        // 共同 policy 已验证正毫秒数可由 int 无损表示。
        m_timer->start(static_cast<int>(remaining->count()));
    } else {
        m_timer->stop();
    }
}

bool QCNetworkReceiveIdleExecution::expireIfDue(CURL *easy)
{
    synchronizeConnection(easy);
    const bool expired = m_rules.expireIfDue(QCNetworkReceiveIdleTimeout::Clock::now());
    schedule();
    return expired;
}

void QCNetworkReplyExecution::initializeReceiveIdle(QCNetworkReply *reply)
{
    auto *d = reply->d_func();
    if (d->transferState->lifecycle.error() != NetworkError::NoError) {
        return;
    }
    const auto timeout = d->transferState->configuration.requestPolicy().timeout.receiveIdleTimeout;
    const QPointer<QCNetworkReply> observer(reply);
    d->transferState->receiveIdle.initialize(reply, timeout, [observer]() {
        if (observer) {
            expireReceiveIdle(observer.data());
        }
    });
}

void QCNetworkReplyExecution::expireReceiveIdle(QCNetworkReply *reply)
{
    auto *d           = reply->d_func();
    const auto holder = d->transferState;
    holder->receiveIdle.setReceivePaused((holder->flow.desiredMask() & CURLPAUSE_RECV) != 0);
    if (holder->lifecycle.isTerminal() || !holder->receiveIdle.expireIfDue(d->activeCurlHandle())) {
        return;
    }
    if (isInReplyCurlCallback()) {
        // 嵌套事件循环可能在原生回调中投递 timer；只标记事实，安全点再移除。
        const QPointer<QCNetworkReply> observer(reply);
        const auto token      = d->transferToken;
        const auto generation = holder->receiveIdle.generation();
        QMetaObject::invokeMethod(
            reply,
            [observer, token, generation]() {
                if (observer && observer->d_func()->transferToken == token
                    && observer->d_func()->transferState->receiveIdle.generation() == generation) {
                    finishReceiveIdleTimeout(observer.data());
                }
            },
            Qt::QueuedConnection);
        return;
    }
    finishReceiveIdleTimeout(reply);
}

void QCNetworkReplyExecution::finishReceiveIdleTimeout(QCNetworkReply *reply)
{
    auto *d = reply->d_func();
    if (d->transferState->lifecycle.isTerminal() || !d->transferState->receiveIdle.expired()) {
        return;
    }
    if (d->transferToken != 0) {
        // 正常 detach completion 消费 timeout 事实并评估既有有限重试，不走取消分支。
        QCCurlMultiManager::instance()->removeTransfer(d->transferToken);
    } else {
        // 回放没有适用的原生结果；本地分类之后复用相同的有限重试与回退。
        const auto &holder = d->transferState;
        const auto info    = holder->lifecycle.classifyMockResult(true,
                                                                  NetworkError::ConnectionTimeout,
                                                                  holder->response.status(),
                                                                  holder->bodySource.failure(),
                                                                  true);
        d->finishAttempt(info);
    }
}

} // namespace QCurl::Internal
