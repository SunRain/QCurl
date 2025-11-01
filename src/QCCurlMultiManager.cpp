#include "QCCurlMultiManager.h"
#include "QCNetworkReply.h"
#include "QCNetworkReply_p.h"
#include "QCNetworkRetryPolicy.h"

#include <QDebug>
#include <QMutexLocker>
#include <QTimer>

namespace QCurl {

// ============================================================================
// SocketInfo 实现
// ============================================================================

SocketInfo::~SocketInfo()
{
    // 注意：不能使用 deleteLater()，因为可能在事件循环外调用
    // 参考旧代码 CurlMultiHandleProcesser.cpp:200-206
    // "Because Qt-Doc says do not use deleteLater() when we are not in eventLoop"
    delete readNotifier;
    delete writeNotifier;
}

// ============================================================================
// QCCurlMultiManager 实现
// ============================================================================

QCCurlMultiManager* QCCurlMultiManager::instance()
{
    // C++11 静态局部变量，线程安全的单例初始化
    static QCCurlMultiManager s_instance;
    return &s_instance;
}

QCCurlMultiManager::QCCurlMultiManager(QObject *parent)
    : QObject(parent),
      m_multiHandle(nullptr),
      m_socketTimer(nullptr)
{
    qDebug() << "QCCurlMultiManager: Initializing global instance";

    // 初始化 curl multi handle
    m_multiHandle = curl_multi_init();
    if (!m_multiHandle) {
        qCritical() << "QCCurlMultiManager: Failed to initialize curl multi handle";
        return;
    }

    // 设置 socket 回调
    CURLMcode ret = curl_multi_setopt(m_multiHandle, CURLMOPT_SOCKETDATA, this);
    if (ret != CURLM_OK) {
        qCritical() << "QCCurlMultiManager: Failed to set CURLMOPT_SOCKETDATA:" << ret;
    }

    ret = curl_multi_setopt(m_multiHandle, CURLMOPT_SOCKETFUNCTION, curlSocketCallback);
    if (ret != CURLM_OK) {
        qCritical() << "QCCurlMultiManager: Failed to set CURLMOPT_SOCKETFUNCTION:" << ret;
    }

    // 设置定时器回调
    ret = curl_multi_setopt(m_multiHandle, CURLMOPT_TIMERDATA, this);
    if (ret != CURLM_OK) {
        qCritical() << "QCCurlMultiManager: Failed to set CURLMOPT_TIMERDATA:" << ret;
    }

    ret = curl_multi_setopt(m_multiHandle, CURLMOPT_TIMERFUNCTION, curlTimerCallback);
    if (ret != CURLM_OK) {
        qCritical() << "QCCurlMultiManager: Failed to set CURLMOPT_TIMERFUNCTION:" << ret;
    }

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

    QMutexLocker locker(&m_mutex);

    // 停止定时器
    if (m_socketTimer) {
        m_socketTimer->stop();
    }

    // 清理所有活动请求
    if (!m_activeReplies.isEmpty()) {
        qWarning() << "QCCurlMultiManager: Destroying with" << m_activeReplies.size()
                   << "active requests";

        for (auto it = m_activeReplies.begin(); it != m_activeReplies.end(); ++it) {
            CURL *easy = it.key();
            curl_multi_remove_handle(m_multiHandle, easy);
        }
        m_activeReplies.clear();
    }

    // 清理所有 socket
    for (auto it = m_socketMap.begin(); it != m_socketMap.end(); ++it) {
        delete it.value();
    }
    m_socketMap.clear();

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

    QMutexLocker locker(&m_mutex);

    // 获取 curl easy handle
    CURL *easy = reply->d_func()->curlManager.handle();
    if (!easy) {
        qCritical() << "QCCurlMultiManager::addReply: reply has invalid curl handle";
        return;
    }

    // 检查是否已存在
    if (m_activeReplies.contains(easy)) {
        qWarning() << "QCCurlMultiManager::addReply: easy handle already registered";
        return;
    }

    // 添加到活动列表（使用 QPointer 自动检测对象销毁）
    m_activeReplies.insert(easy, QPointer<QCNetworkReply>(reply));

    // 连接完成信号（每个 reply 只会添加一次，无需 Qt::UniqueConnection）
    // 注意：Lambda 不支持 Qt::UniqueConnection（需要成员函数指针）
    connect(this, &QCCurlMultiManager::requestFinished,
            reply, [reply](QCNetworkReply *finishedReply, int curlCode) {
        if (reply == finishedReply) {
            // 处理完成逻辑（设置错误、发射信号）
            auto *d = reply->d_func();

            // ========================================================
            // 检查 HTTP 状态码（即使 CURLcode 成功）
            // ========================================================
            CURL *handle = d->curlManager.handle();
            long httpCode = 0;
            if (handle) {
                curl_easy_getinfo(handle, CURLINFO_RESPONSE_CODE, &httpCode);
            }

            // 确定最终错误：优先使用 HTTP 错误，否则使用 curl 错误
            NetworkError error = NetworkError::NoError;
            QString errorMsg;

            if (curlCode != CURLE_OK) {
                // libcurl 层面的错误
                error = fromCurlCode(static_cast<CURLcode>(curlCode));
                errorMsg = QString::fromUtf8(curl_easy_strerror(static_cast<CURLcode>(curlCode)));
            } else if (httpCode >= 400) {
                // HTTP 错误（4xx, 5xx）
                error = fromHttpCode(httpCode);
                errorMsg = QString("HTTP error %1").arg(httpCode);
            }

            // 如果没有错误，标记为完成
            if (error == NetworkError::NoError) {
                d->setState(ReplyState::Finished);
                return;
            }

            // ========================================================
            // 异步重试逻辑
            // ========================================================

            // 获取重试策略
            QCNetworkRetryPolicy policy = d->request.retryPolicy();

            // 检查是否应该重试
            if (policy.shouldRetry(error, d->attemptCount)) {
                // 增加重试计数
                d->attemptCount++;

                // 发射重试尝试信号
                emit reply->retryAttempt(d->attemptCount, error);

                // 计算延迟时间（注意：attemptCount 已经++，所以使用 attemptCount-1）
                auto delay = policy.delayForAttempt(d->attemptCount - 1);

                qDebug() << "QCCurlMultiManager: Retry scheduled for reply" << reply
                         << "Attempt" << d->attemptCount << "after" << delay.count() << "ms"
                         << "Error:" << errorMsg;

                // 使用 QPointer 防止在延迟期间 reply 被销毁
                QPointer<QCNetworkReply> safeReply(reply);

                // 延迟后重新执行
                QTimer::singleShot(delay.count(), reply, [safeReply, d]() {
                    if (!safeReply) {
                        qWarning() << "QCCurlMultiManager: Reply destroyed during retry delay";
                        return;
                    }

                    // ⚠️ v2.1.0: 检查是否在重试延迟期间被取消
                    if (d->state == ReplyState::Cancelled) {
                        qDebug() << "QCCurlMultiManager: Retry cancelled for reply" << safeReply.data();
                        return;  // 不继续重试
                    }

                    // 重置状态和缓冲区（准备重试）
                    d->state = ReplyState::Idle;
                    d->errorCode = NetworkError::NoError;
                    d->errorMessage.clear();
                    d->bodyBuffer.clear();
                    d->headerData.clear();
                    d->headerMap.clear();
                    d->bytesDownloaded = 0;
                    d->bytesUploaded = 0;
                    d->downloadTotal = -1;
                    d->uploadTotal = -1;

                    qDebug() << "QCCurlMultiManager: Retrying request for reply" << safeReply.data();

                    // 重新执行请求
                    safeReply->execute();
                });

                return;  // ⚠️ 关键：不调用 setState(Error)，避免发射错误信号
            }

            // 超过最大重试次数或错误不可重试，标记为错误
            qDebug() << "QCCurlMultiManager: Request failed after" << d->attemptCount
                     << "attempts. Error:" << errorMsg;

            d->setError(error, errorMsg);
            d->setState(ReplyState::Error);
        }
    });

    // 添加到 multi handle
    CURLMcode ret = curl_multi_add_handle(m_multiHandle, easy);
    if (ret != CURLM_OK) {
        qCritical() << "QCCurlMultiManager::addReply: curl_multi_add_handle failed:" << ret;
        m_activeReplies.remove(easy);
        return;
    }

    m_runningRequests.fetch_add(1, std::memory_order_relaxed);

    qDebug() << "QCCurlMultiManager::addReply: Added reply" << reply
             << "Total running:" << m_runningRequests.load();
}

void QCCurlMultiManager::removeReply(QCNetworkReply *reply)
{
    if (!reply) {
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

    // 从 multi handle 移除
    CURLMcode ret = curl_multi_remove_handle(m_multiHandle, easy);
    if (ret != CURLM_OK) {
        qWarning() << "QCCurlMultiManager::removeReply: curl_multi_remove_handle failed:" << ret;
    }

    // 从活动列表移除
    m_activeReplies.remove(easy);
    m_runningRequests.fetch_sub(1, std::memory_order_relaxed);

    qDebug() << "QCCurlMultiManager::removeReply: Removed reply" << reply
             << "Remaining:" << m_runningRequests.load();
}

int QCCurlMultiManager::runningRequestsCount() const noexcept
{
    return m_runningRequests.load(std::memory_order_relaxed);
}

void QCCurlMultiManager::handleSocketAction(curl_socket_t socketfd, int eventsBitmask)
{
    qDebug() << "QCCurlMultiManager::handleSocketAction: socketfd=" << socketfd
             << "events=" << eventsBitmask;

    int runningHandles = 0;
    CURLMcode ret = curl_multi_socket_action(m_multiHandle, socketfd, eventsBitmask, &runningHandles);

    if (ret != CURLM_OK) {
        qWarning() << "QCCurlMultiManager::handleSocketAction: curl_multi_socket_action failed:" << ret;
        // 不要直接返回，继续检查完成的请求
    }

    qDebug() << "QCCurlMultiManager::handleSocketAction: Running handles:" << runningHandles;

    // 检查完成的请求
    checkMultiInfo();
}

void QCCurlMultiManager::checkMultiInfo()
{
    int messagesLeft = 0;

    do {
        CURLMsg *message = curl_multi_info_read(m_multiHandle, &messagesLeft);

        if (!message) {
            break;
        }

        if (message->msg != CURLMSG_DONE) {
            continue;
        }

        CURL *easy = message->easy_handle;
        if (!easy) {
            continue;
        }

        // 获取关联的 Reply 对象（线程安全）
        QMutexLocker locker(&m_mutex);

        QPointer<QCNetworkReply> replyPtr = m_activeReplies.value(easy);
        if (!replyPtr) {
            qWarning() << "QCCurlMultiManager::checkMultiInfo: Reply object already destroyed";
            curl_multi_remove_handle(m_multiHandle, easy);
            m_activeReplies.remove(easy);
            continue;
        }

        QCNetworkReply *reply = replyPtr.data();

        // 获取响应码和重定向 URL（调试信息）
        long responseCode = 0;
        curl_easy_getinfo(easy, CURLINFO_RESPONSE_CODE, &responseCode);

        char *redirectUrl = nullptr;
        curl_easy_getinfo(easy, CURLINFO_REDIRECT_URL, &redirectUrl);

        qDebug() << "QCCurlMultiManager::checkMultiInfo: Request finished"
                 << "Reply:" << reply
                 << "CURLcode:" << message->data.result
                 << "HTTP code:" << responseCode
                 << "Redirect:" << (redirectUrl ? redirectUrl : "none");

        // 从管理器移除
        curl_multi_remove_handle(m_multiHandle, easy);
        m_activeReplies.remove(easy);
        m_runningRequests.fetch_sub(1, std::memory_order_relaxed);

        // 解锁后发射信号（避免死锁）
        locker.unlock();

        // 发射完成信号（Reply 对象通过连接处理）
        emit requestFinished(reply, message->data.result);

    } while (messagesLeft > 0);
}

void QCCurlMultiManager::cleanupSocket(curl_socket_t socketfd)
{
    QMutexLocker locker(&m_mutex);

    SocketInfo *info = m_socketMap.value(socketfd, nullptr);
    if (!info) {
        return;
    }

    qDebug() << "QCCurlMultiManager::cleanupSocket: Cleaning socket" << socketfd;

    // 从 libcurl 解除关联
    curl_multi_assign(m_multiHandle, socketfd, nullptr);

    // 删除 SocketInfo（析构函数会清理 notifier）
    delete info;
    m_socketMap.remove(socketfd);
}

int QCCurlMultiManager::manageSocketNotifiers(curl_socket_t socketfd, int what, SocketInfo *socketInfo)
{
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

    // 创建 SocketInfo（如果不存在）
    if (!socketInfo) {
        socketInfo = new SocketInfo;
        socketInfo->socketfd = socketfd;

        QMutexLocker locker(&m_mutex);
        m_socketMap.insert(socketfd, socketInfo);

        curl_multi_assign(m_multiHandle, socketfd, socketInfo);
    }

    // CURL_POLL_IN 或 CURL_POLL_INOUT: 启用读取
    if (what == CURL_POLL_IN || what == CURL_POLL_INOUT) {
        if (!socketInfo->readNotifier) {
            socketInfo->readNotifier = new QSocketNotifier(socketfd, QSocketNotifier::Read, this);

            connect(socketInfo->readNotifier, &QSocketNotifier::activated,
                    this, [this, socketfd]() {
                qDebug() << "QCCurlMultiManager: Read event on socket" << socketfd;
                handleSocketAction(socketfd, CURL_CSELECT_IN);
            });
        }
        socketInfo->readNotifier->setEnabled(true);
    } else {
        if (socketInfo->readNotifier) {
            socketInfo->readNotifier->setEnabled(false);
        }
    }

    // CURL_POLL_OUT 或 CURL_POLL_INOUT: 启用写入
    if (what == CURL_POLL_OUT || what == CURL_POLL_INOUT) {
        if (!socketInfo->writeNotifier) {
            socketInfo->writeNotifier = new QSocketNotifier(socketfd, QSocketNotifier::Write, this);

            connect(socketInfo->writeNotifier, &QSocketNotifier::activated,
                    this, [this, socketfd]() {
                qDebug() << "QCCurlMultiManager: Write event on socket" << socketfd;
                handleSocketAction(socketfd, CURL_CSELECT_OUT);
            });
        }
        socketInfo->writeNotifier->setEnabled(true);
    } else {
        if (socketInfo->writeNotifier) {
            socketInfo->writeNotifier->setEnabled(false);
        }
    }

    return 0;
}

// ============================================================================
// libcurl 静态回调
// ============================================================================

int QCCurlMultiManager::curlSocketCallback(CURL *easy, curl_socket_t socketfd, int what,
                                           void *userp, void *socketp)
{
    Q_UNUSED(easy);

    auto *manager = static_cast<QCCurlMultiManager*>(userp);
    if (!manager) {
        qCritical() << "QCCurlMultiManager::curlSocketCallback: Invalid manager pointer";
        return -1;
    }

    auto *socketInfo = static_cast<SocketInfo*>(socketp);

    return manager->manageSocketNotifiers(socketfd, what, socketInfo);
}

int QCCurlMultiManager::curlTimerCallback(CURLM *multi, long timeout_ms, void *userp)
{
    Q_UNUSED(multi);

    auto *manager = static_cast<QCCurlMultiManager*>(userp);
    if (!manager) {
        qCritical() << "QCCurlMultiManager::curlTimerCallback: Invalid manager pointer";
        return -1;
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
