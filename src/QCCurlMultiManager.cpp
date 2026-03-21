#include "QCCurlMultiManager.h"

#include "QCNetworkAccessManager.h"
#include "QCNetworkConnectionPoolConfig.h"
#include "QCNetworkReply.h"
#include "QCNetworkReply_p.h"
#include "QCNetworkRetryPolicy.h"

#include <QDateTime>
#include <QDebug>
#include <QList>
#include <QMutexLocker>
#include <QNetworkCookie>
#include <QStringList>
#include <QThread>
#include <QTimeZone>
#include <QTimer>

#include <ctime>
#include <limits>
#include <memory>

namespace QCurl {

namespace {

bool isCapabilityRelatedCurlError(CURLcode code)
{
    return code == CURLE_UNKNOWN_OPTION || code == CURLE_NOT_BUILT_IN;
}

std::optional<std::chrono::milliseconds> parseRetryAfterDelay(const QMap<QString, QString> &headers)
{
    for (auto it = headers.cbegin(); it != headers.cend(); ++it) {
        if (it.key().compare(QStringLiteral("Retry-After"), Qt::CaseInsensitive) != 0) {
            continue;
        }

        const QString raw = it.value().trimmed();
        if (raw.isEmpty()) {
            return std::nullopt;
        }

        bool ok              = false;
        const qint64 seconds = raw.toLongLong(&ok);
        if (ok) {
            if (seconds < 0) {
                return std::nullopt;
            }
            return std::chrono::milliseconds(seconds * 1000);
        }

        const QByteArray dateBytes = raw.toUtf8();
        const time_t parsed        = curl_getdate(dateBytes.constData(), nullptr);
        if (parsed < 0) {
            return std::nullopt;
        }

        const time_t now = std::time(nullptr);
        if (parsed <= now) {
            return std::chrono::milliseconds(0);
        }

        const qint64 deltaSeconds = static_cast<qint64>(parsed) - static_cast<qint64>(now);
        if (deltaSeconds > (std::numeric_limits<qint64>::max() / 1000)) {
            return std::chrono::milliseconds(std::numeric_limits<qint64>::max());
        }

        return std::chrono::milliseconds(deltaSeconds * 1000);
    }

    return std::nullopt;
}

bool domainMatchesHost(const QString &cookieDomain, const QString &host)
{
    if (cookieDomain.isEmpty() || host.isEmpty()) {
        return false;
    }

    QString normalized = cookieDomain;
    if (normalized.startsWith(QStringLiteral("#HttpOnly_"))) {
        normalized = normalized.mid(QStringLiteral("#HttpOnly_").size());
    }
    const bool includeSubdomains = normalized.startsWith('.');
    if (includeSubdomains) {
        normalized = normalized.mid(1);
    }

    if (normalized.isEmpty()) {
        return false;
    }

    if (host.compare(normalized, Qt::CaseInsensitive) == 0) {
        return true;
    }

    if (!includeSubdomains) {
        return false;
    }

    if (host.size() <= normalized.size()) {
        return false;
    }

    if (!host.endsWith(normalized, Qt::CaseInsensitive)) {
        return false;
    }

    const int idx = host.size() - normalized.size() - 1;
    return idx >= 0 && host.at(idx) == QChar('.');
}

bool pathMatchesUrl(const QString &cookiePath, const QString &urlPath)
{
    const QString p = cookiePath.isEmpty() ? QStringLiteral("/") : cookiePath;
    const QString u = urlPath.isEmpty() ? QStringLiteral("/") : urlPath;
    if (!u.startsWith(p)) {
        return false;
    }

    if (u.size() == p.size()) {
        return true;
    }

    if (p.endsWith('/')) {
        return true;
    }

    return u.at(p.size()) == QChar('/');
}

std::optional<QNetworkCookie> parseCurlCookieLine(const QByteArray &line)
{
    if (line.isEmpty()) {
        return std::nullopt;
    }

    const QList<QByteArray> parts = line.split('\t');
    if (parts.size() < 7) {
        return std::nullopt;
    }

    QByteArray domainBytes                  = parts.at(0);
    const QByteArray includeSubdomainsBytes = parts.at(1);
    const bool includeSubdomains            = includeSubdomainsBytes.trimmed().toUpper() == "TRUE";
    bool httpOnly                           = false;
    static const QByteArray kHttpOnlyPrefix = "#HttpOnly_";
    if (domainBytes.startsWith(kHttpOnlyPrefix)) {
        httpOnly    = true;
        domainBytes = domainBytes.mid(kHttpOnlyPrefix.size());
    }

    const QByteArray pathBytes    = parts.at(2);
    const QByteArray secureBytes  = parts.at(3);
    const QByteArray expiresBytes = parts.at(4);
    const QByteArray nameBytes    = parts.at(5);
    const QByteArray valueBytes   = parts.at(6);

    QNetworkCookie cookie(nameBytes, valueBytes);
    QString domain = QString::fromUtf8(domainBytes);
    if (includeSubdomains) {
        if (!domain.startsWith('.')) {
            domain.prepend('.');
        }
    } else {
        if (domain.startsWith('.')) {
            domain.remove(0, 1);
        }
    }
    cookie.setDomain(domain);
    cookie.setPath(QString::fromUtf8(pathBytes));
    cookie.setSecure(secureBytes.trimmed().toUpper() == "TRUE");
    cookie.setHttpOnly(httpOnly);

    bool ok            = false;
    const qint64 epoch = expiresBytes.trimmed().toLongLong(&ok);
    if (ok && epoch > 0) {
        cookie.setExpirationDate(QDateTime::fromSecsSinceEpoch(epoch, QTimeZone::utc()));
    }
    return cookie;
}

} // namespace

void QCCurlMultiManager::shareLockCallback(CURL *,
                                           curl_lock_data data,
                                           curl_lock_access,
                                           void *userptr)
{
    auto *context = static_cast<ShareContext *>(userptr);
    if (!context) {
        return;
    }

    switch (data) {
        case CURL_LOCK_DATA_DNS:
            context->dnsMutex.lock();
            return;
        case CURL_LOCK_DATA_COOKIE:
            context->cookieMutex.lock();
            return;
        case CURL_LOCK_DATA_SSL_SESSION:
            context->sslMutex.lock();
            return;
        default:
            context->otherMutex.lock();
            return;
    }
}

void QCCurlMultiManager::shareUnlockCallback(CURL *, curl_lock_data data, void *userptr)
{
    auto *context = static_cast<ShareContext *>(userptr);
    if (!context) {
        return;
    }

    switch (data) {
        case CURL_LOCK_DATA_DNS:
            context->dnsMutex.unlock();
            return;
        case CURL_LOCK_DATA_COOKIE:
            context->cookieMutex.unlock();
            return;
        case CURL_LOCK_DATA_SSL_SESSION:
            context->sslMutex.unlock();
            return;
        default:
            context->otherMutex.unlock();
            return;
    }
}

QCCurlMultiManager::ShareConfig QCCurlMultiManager::toShareConfig(
    const QCNetworkAccessManager *manager)
{
    ShareConfig out;
    if (!manager) {
        return out;
    }

    const auto config = manager->shareHandleConfig();
    out.dnsCache      = config.shareDnsCache;
    out.cookies       = config.shareCookies;
    out.sslSession    = config.shareSslSession;
    return out;
}

QString QCCurlMultiManager::shareConfigSummary(const ShareConfig &config)
{
    if (!config.enabled()) {
        return QStringLiteral("关闭");
    }

    QStringList parts;
    if (config.dnsCache) {
        parts.append(QStringLiteral("DNS"));
    }
    if (config.cookies) {
        parts.append(QStringLiteral("Cookie"));
    }
    if (config.sslSession) {
        parts.append(QStringLiteral("SSL session"));
    }
    return parts.join(QStringLiteral(","));
}

// ==================
// QCCurlMultiManager 实现
// ==================

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

    m_isShuttingDown.store(true, std::memory_order_relaxed);

    // 先禁用 libcurl 回调，避免在清理过程中重入导致死锁
    if (m_multiHandle) {
        curl_multi_setopt(m_multiHandle, CURLMOPT_SOCKETDATA, nullptr);
        curl_multi_setopt(m_multiHandle, CURLMOPT_SOCKETFUNCTION, nullptr);
        curl_multi_setopt(m_multiHandle, CURLMOPT_TIMERDATA, nullptr);
        curl_multi_setopt(m_multiHandle, CURLMOPT_TIMERFUNCTION, nullptr);
    }

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

    if (!activeHandles.isEmpty()) {
        qWarning() << "QCCurlMultiManager: Destroying with" << activeHandles.size()
                   << "active requests";
    }
    for (CURL *easy : activeHandles) {
        if (!easy || !m_multiHandle) {
            continue;
        }
        curl_multi_remove_handle(m_multiHandle, easy);
    }

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

    for (const auto &contextHolder : shareContexts) {
        ShareContext *context = contextHolder.data();
        if (!context) {
            continue;
        }
        if (context->share) {
            curl_share_cleanup(context->share);
            context->share = nullptr;
        }
    }

    // 清理 multi handle
    if (m_multiHandle) {
        curl_multi_cleanup(m_multiHandle);
        m_multiHandle = nullptr;
    }

    qDebug() << "QCCurlMultiManager: Destruction complete";
}

QCCurlMultiManager::ShareContext *QCCurlMultiManager::getOrCreateShareContextLocked(
    const QCNetworkAccessManager *manager)
{
    if (!manager) {
        return nullptr;
    }

    auto it = m_shareContexts.find(manager);
    if (it != m_shareContexts.end()) {
        return it.value().data();
    }

    auto context             = QSharedPointer<ShareContext>::create();
    context->scopeKey        = manager;
    ShareContext *contextPtr = context.data();
    m_shareContexts.insert(manager, context);

    QObject::connect(const_cast<QCNetworkAccessManager *>(manager),
                     &QObject::destroyed,
                     this,
                     [this, manager]() {
                         QMutexLocker locker(&m_mutex);
                         onAccessManagerDestroyedLocked(manager);
                     });

    return contextPtr;
}

void QCCurlMultiManager::onAccessManagerDestroyedLocked(const QCNetworkAccessManager *manager)
{
    auto it = m_shareContexts.find(manager);
    if (it == m_shareContexts.end()) {
        return;
    }

    ShareContext *context = it.value().data();
    if (!context) {
        m_shareContexts.erase(it);
        return;
    }

    context->scopeDestroyed = true;
    context->pendingDelete  = true;

    if (context->activeUsers == 0) {
        if (context->share) {
            curl_share_cleanup(context->share);
            context->share = nullptr;
        }
        m_shareContexts.erase(it);
        return;
    }

    context->pending = ShareConfig{};
}

bool QCCurlMultiManager::applyShareConfigIfIdleLocked(ShareContext *context,
                                                      const ShareConfig &desired,
                                                      QString *error)
{
    if (!context || context->activeUsers != 0) {
        if (error) {
            *error = QStringLiteral("share handle 正在使用中，无法切换配置");
        }
        return false;
    }

    context->pending.reset();

    if (!desired.enabled()) {
        if (context->share) {
            curl_share_cleanup(context->share);
            context->share = nullptr;
        }
        context->applied         = ShareConfig{};
        context->lastInitAttempt = desired;
        context->lastInitFailed  = false;
        context->lastInitError.clear();
        return true;
    }

    if (!context->share && context->lastInitFailed && context->lastInitAttempt == desired) {
        if (error) {
            *error = context->lastInitError;
        }
        return false;
    }

    if (context->share) {
        curl_share_cleanup(context->share);
        context->share = nullptr;
    }

    context->applied         = ShareConfig{};
    context->lastInitAttempt = desired;
    context->lastInitFailed  = false;
    context->lastInitError.clear();

    CURLSH *share = curl_share_init();
    if (!share) {
        context->lastInitFailed = true;
        context->lastInitError  = QStringLiteral("curl_share_init 失败");
        if (error) {
            *error = context->lastInitError;
        }
        return false;
    }

    auto failHard = [context, share, error](const QString &message) {
        curl_share_cleanup(share);
        context->lastInitFailed = true;
        context->lastInitError  = message;
        if (error) {
            *error = message;
        }
    };

    CURLSHcode rc = curl_share_setopt(share, CURLSHOPT_USERDATA, context);
    if (rc != CURLSHE_OK) {
        failHard(QStringLiteral("设置 CURLSHOPT_USERDATA 失败（%1）")
                     .arg(QString::fromUtf8(curl_share_strerror(rc))));
        return false;
    }

    rc = curl_share_setopt(share, CURLSHOPT_LOCKFUNC, shareLockCallback);
    if (rc != CURLSHE_OK) {
        failHard(QStringLiteral("设置 CURLSHOPT_LOCKFUNC 失败（%1）")
                     .arg(QString::fromUtf8(curl_share_strerror(rc))));
        return false;
    }

    rc = curl_share_setopt(share, CURLSHOPT_UNLOCKFUNC, shareUnlockCallback);
    if (rc != CURLSHE_OK) {
        failHard(QStringLiteral("设置 CURLSHOPT_UNLOCKFUNC 失败（%1）")
                     .arg(QString::fromUtf8(curl_share_strerror(rc))));
        return false;
    }

    if (desired.dnsCache) {
        rc = curl_share_setopt(share, CURLSHOPT_SHARE, CURL_LOCK_DATA_DNS);
        if (rc == CURLSHE_OK) {
            context->applied.dnsCache = true;
        }
    }

    if (desired.cookies) {
        rc = curl_share_setopt(share, CURLSHOPT_SHARE, CURL_LOCK_DATA_COOKIE);
        if (rc == CURLSHE_OK) {
            context->applied.cookies = true;
        }
    }

    if (desired.sslSession) {
        rc = curl_share_setopt(share, CURLSHOPT_SHARE, CURL_LOCK_DATA_SSL_SESSION);
        if (rc == CURLSHE_OK) {
            context->applied.sslSession = true;
        }
    }

    if (!context->applied.enabled()) {
        failHard(
            QStringLiteral("curl_share_setopt(CURLSHOPT_SHARE) 全部失败，share handle 不可用"));
        return false;
    }

    context->share = share;
    return true;
}

void QCCurlMultiManager::releaseShareForEasyHandleLocked(CURL *easy)
{
    auto it = m_easyToShareContext.find(easy);
    if (it == m_easyToShareContext.end()) {
        return;
    }

    ShareContext *context = it.value();
    m_easyToShareContext.erase(it);

    if (context && context->activeUsers > 0) {
        context->activeUsers -= 1;
    }

    if (context) {
        maybeFinalizeShareContextLocked(context);
    }
}

void QCCurlMultiManager::cleanupEasyHandleLocked(CURL *easy,
                                                 bool removeFromMulti,
                                                 bool decrementRunningCount,
                                                 const char *context)
{
    if (!easy) {
        return;
    }

    if (removeFromMulti && m_multiHandle) {
        const CURLMcode ret = curl_multi_remove_handle(m_multiHandle, easy);
        if (ret != CURLM_OK && ret != CURLM_BAD_EASY_HANDLE) {
            qWarning() << context << ": curl_multi_remove_handle failed:" << ret;
        }
    }

    const bool removedReply = m_activeReplies.remove(easy) > 0;
    if (removedReply && decrementRunningCount) {
        m_runningRequests.fetch_sub(1, std::memory_order_relaxed);
    }

    releaseShareForEasyHandleLocked(easy);
}

void QCCurlMultiManager::maybeFinalizeShareContextLocked(ShareContext *context)
{
    if (!context || context->activeUsers != 0) {
        return;
    }

    if (context->pending.has_value()) {
        QString error;
        static_cast<void>(applyShareConfigIfIdleLocked(context, context->pending.value(), &error));
        context->pending.reset();
    }

    if (!context->pendingDelete) {
        return;
    }

    if (context->share) {
        curl_share_cleanup(context->share);
        context->share = nullptr;
    }
    m_shareContexts.remove(context->scopeKey);
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

    // multi engine 只允许在所属线程串行访问；跨线程调用应投递到本线程执行
    if (QThread::currentThread() != thread()) {
        QPointer<QCNetworkReply> safeReply(reply);
        QMetaObject::invokeMethod(
            this,
            [this, safeReply]() {
                if (safeReply) {
                    addReply(safeReply.data());
                }
            },
            Qt::QueuedConnection);
        return;
    }

    QMutexLocker locker(&m_mutex);

    // 获取 curl easy handle
    CURL *easy = reply->d_func()->curlManager.handle();
    if (!easy) {
        qCritical() << "QCCurlMultiManager::addReply: reply has invalid curl handle";
        return;
    }

    // ==================
    // Share handle（M6+，可选）：默认关闭；显式开启时才设置 CURLOPT_SHARE
    // ==================

    auto *accessManager                  = qobject_cast<QCNetworkAccessManager *>(reply->parent());
    const ShareConfig desiredShareConfig = toShareConfig(accessManager);

    ShareContext *shareContext = nullptr;
    if (accessManager && desiredShareConfig.enabled()) {
        shareContext = getOrCreateShareContextLocked(accessManager);
        if (shareContext && !shareContext->pendingDelete) {
            if (desiredShareConfig != shareContext->applied) {
                if (shareContext->activeUsers > 0) {
                    shareContext->pending = desiredShareConfig;
                    reply->d_func()->capabilityWarnings.append(
                        QStringLiteral("share handle 配置变更延迟生效：当前仍按 %1 "
                                       "生效，待在途请求结束后切换为 %2")
                            .arg(shareConfigSummary(shareContext->applied))
                            .arg(shareConfigSummary(desiredShareConfig)));
                } else {
                    QString initError;
                    if (!applyShareConfigIfIdleLocked(shareContext, desiredShareConfig, &initError)) {
                        const QString reason = initError.isEmpty() ? shareContext->lastInitError
                                                                   : initError;
                        reply->d_func()->capabilityWarnings.append(
                            QStringLiteral("share handle 不可用（%1），已降级为不共享缓存")
                                .arg(reason.isEmpty() ? QStringLiteral("unknown") : reason));
                    } else if (shareContext->applied != desiredShareConfig) {
                        reply->d_func()->capabilityWarnings.append(
                            QStringLiteral("share handle 降级：期望 %1，但实际仅启用 %2")
                                .arg(shareConfigSummary(desiredShareConfig))
                                .arg(shareConfigSummary(shareContext->applied)));
                    }
                }
            }
        } else {
            reply->d_func()->capabilityWarnings.append(
                QStringLiteral("share handle 作用域已销毁，已降级为不共享缓存"));
        }
    }

    const bool canApplyShare = shareContext && shareContext->share
                               && shareContext->applied.enabled();
    if (canApplyShare) {
        const CURLcode rc = curl_easy_setopt(easy, CURLOPT_SHARE, shareContext->share);
        if (rc == CURLE_OK) {
            shareContext->activeUsers += 1;
            m_easyToShareContext.insert(easy, shareContext);
            m_easyShareOptionSet.insert(easy, true);

            if (shareContext->applied.cookies) {
                auto *d                 = reply->d_func();
                const bool hasCookieJar = (d->cookieMode != 0) && !d->cookieFilePath.isEmpty();
                if (!hasCookieJar) {
                    curl_easy_setopt(easy, CURLOPT_COOKIEFILE, "");
                }
            }
        } else {
            reply->d_func()->capabilityWarnings.append(
                QStringLiteral("设置 CURLOPT_SHARE 失败（%1），已降级为不共享缓存")
                    .arg(QString::fromUtf8(curl_easy_strerror(rc))));
            curl_easy_setopt(easy, CURLOPT_SHARE, nullptr);
            m_easyShareOptionSet.insert(easy, false);
        }
    } else {
        auto it = m_easyShareOptionSet.find(easy);
        if (it != m_easyShareOptionSet.end() && it.value()) {
            curl_easy_setopt(easy, CURLOPT_SHARE, nullptr);
            it.value() = false;
        }
    }

    // 检查是否已存在
    if (m_activeReplies.contains(easy)) {
        qWarning() << "QCCurlMultiManager::addReply: easy handle already registered";
        return;
    }

    // 添加到活动列表（使用 QPointer 自动检测对象销毁）
    m_activeReplies.insert(easy, QPointer<QCNetworkReply>(reply));

    // 连接完成信号：
    // - reply 在重试时会多次调用 execute() 并进入 addReply()
    // - 若每次 addReply() 都 connect，会导致 requestFinished 处理重复执行（attemptCount 乱序/多次重试/假挂起）
    // 由于 Lambda 不支持 Qt::UniqueConnection，这里用 reply property 做幂等保护。
    static constexpr const char kRequestFinishedConnectedProperty[]
        = "_qcurl_requestFinishedConnected";
    if (!reply->property(kRequestFinishedConnectedProperty).toBool()) {
        reply->setProperty(kRequestFinishedConnectedProperty, true);

        connect(this,
                &QCCurlMultiManager::requestFinished,
                reply,
                [reply](QCNetworkReply *finishedReply, int curlCode) {
                    if (reply == finishedReply) {
                        // 处理完成逻辑（设置错误、发射信号）
                        auto *d = reply->d_func();

                        // 已取消/已错误：保持既有可观测语义，不允许完成回调覆盖状态
                        if (d->state == ReplyState::Cancelled || d->state == ReplyState::Error) {
                            return;
                        }

                        // ==================
                        // 检查 HTTP 状态码（即使 CURLcode 成功）
                        // ==================
                        CURL *handle  = d->curlManager.handle();
                        long httpCode = 0;
                        if (handle) {
                            curl_easy_getinfo(handle, CURLINFO_RESPONSE_CODE, &httpCode);
                        }
                        d->httpStatusCode = static_cast<int>(httpCode);

                        // 确定最终错误：优先使用 HTTP 错误，否则使用 curl 错误
                        NetworkError error = NetworkError::NoError;
                        QString errorMsg;

                        if (curlCode != CURLE_OK) {
                            // libcurl 层面的错误
                            error    = fromCurlCode(static_cast<CURLcode>(curlCode));
                            errorMsg = QString::fromUtf8(
                                curl_easy_strerror(static_cast<CURLcode>(curlCode)));
                        } else if (httpCode >= 400) {
                            // HTTP 错误（4xx, 5xx）
                            error    = fromHttpCode(httpCode);
                            errorMsg = QStringLiteral("HTTP error %1").arg(httpCode);
                        }

#if defined(CURLE_SEND_FAIL_REWIND)
                        if (curlCode == CURLE_SEND_FAIL_REWIND && d->uploadDevice
                            && !d->hasUploadErrorOverride) {
                            error    = NetworkError::InvalidRequest;
                            errorMsg = QStringLiteral(
                                           "uploadDevice: 无法重发 body（seek/rewind 失败：%1）")
                                           .arg(QString::fromUtf8(curl_easy_strerror(
                                               static_cast<CURLcode>(curlCode))));
                        }
#endif

                        if (d->hasUploadErrorOverride) {
                            error    = d->uploadErrorOverrideCode;
                            errorMsg = d->uploadErrorOverrideMessage;
                        }

                        // 如果没有错误，标记为完成
                        if (error == NetworkError::NoError) {
                            d->setState(ReplyState::Finished);
                            return;
                        }

                        // ==================
                        // 异步重试逻辑
                        // ==================

                        // 获取重试策略
                        QCNetworkRetryPolicy policy = d->request.retryPolicy();

                        const bool httpGetOnlyBlocked = policy.retryHttpStatusErrorsForGetOnly
                                                        && isHttpError(error)
                                                        && (d->httpMethod != HttpMethod::Get);

                        // 检查是否应该重试
                        if (!httpGetOnlyBlocked && policy.shouldRetry(error, d->attemptCount)) {
                            // 增加重试计数
                            d->attemptCount++;

                            // 发射重试尝试信号
                            emit reply->retryAttempt(d->attemptCount, error);

                            // 计算延迟时间（注意：attemptCount 已经++，所以使用 attemptCount-1）
                            std::optional<std::chrono::milliseconds> retryAfter;
                            if (error == NetworkError::HttpTooManyRequests) {
                                d->parseHeaders();
                                retryAfter = parseRetryAfterDelay(d->headerMap);
                            }

                            auto delay = policy.delayForAttempt(d->attemptCount - 1, retryAfter);

                            qDebug()
                                << "QCCurlMultiManager: Retry scheduled for reply" << reply
                                << "Attempt" << d->attemptCount << "after" << delay.count() << "ms"
                                << "Error:" << errorMsg;

                            // 使用 QPointer 防止在延迟期间 reply 被销毁
                            QPointer<QCNetworkReply> safeReply(reply);

                            // 延迟后重新执行
                            QTimer::singleShot(delay.count(), reply, [safeReply, d]() {
                                if (!safeReply) {
                                    qWarning()
                                        << "QCCurlMultiManager: Reply destroyed during retry delay";
                                    return;
                                }

                                // ⚠️ v2.1.0: 检查是否在重试延迟期间被取消
                                if (d->state == ReplyState::Cancelled) {
                                    qDebug() << "QCCurlMultiManager: Retry cancelled for reply"
                                             << safeReply.data();
                                    return; // 不继续重试
                                }

                                // 重置状态和缓冲区（准备重试）
                                d->state     = ReplyState::Idle;
                                d->errorCode = NetworkError::NoError;
                                d->errorMessage.clear();
                                d->bodyBuffer.clear();
                                d->headerData.clear();
                                d->headerMap.clear();
                                d->bytesDownloaded = 0;
                                d->bytesUploaded   = 0;
                                d->downloadTotal   = -1;
                                d->uploadTotal     = -1;

                                qDebug() << "QCCurlMultiManager: Retrying request for reply"
                                         << safeReply.data();

                                // 重新执行请求
                                safeReply->execute();
                            });

                            return; // ⚠️ 关键：不调用 setState(Error)，避免发射错误信号
                        }

                        // 超过最大重试次数或错误不可重试，标记为错误
                        qDebug() << "QCCurlMultiManager: Request failed after" << d->attemptCount
                                 << "attempts. Error:" << errorMsg;

                        d->setError(error, errorMsg);
                        d->setState(ReplyState::Error);
                    }
                });
    }

    // 添加到 multi handle
    CURLMcode ret = curl_multi_add_handle(m_multiHandle, easy);
    if (ret != CURLM_OK) {
        qCritical() << "QCCurlMultiManager::addReply: curl_multi_add_handle failed:" << ret;
        cleanupEasyHandleLocked(easy, false, false, "QCCurlMultiManager::addReply");
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

bool QCCurlMultiManager::importCookiesForManager(const QCNetworkAccessManager *manager,
                                                 const QList<QNetworkCookie> &cookies,
                                                 const QUrl &originUrl,
                                                 QString *error)
{
    if (!manager) {
        if (error) {
            *error = QStringLiteral("manager 为空");
        }
        return false;
    }

    const ShareConfig desired = toShareConfig(manager);
    if (!desired.cookies) {
        if (error) {
            *error = QStringLiteral("importCookies 需要启用 ShareHandleConfig.shareCookies");
        }
        return false;
    }

    QMutexLocker locker(&m_mutex);
    ShareContext *context = getOrCreateShareContextLocked(manager);
    if (!context) {
        if (error) {
            *error = QStringLiteral("share context 不可用");
        }
        return false;
    }

    if (!context->share || (context->applied != desired)) {
        if (context->activeUsers != 0) {
            if (error) {
                *error = QStringLiteral("share handle 正在使用中，无法初始化/切换配置");
            }
            return false;
        }
        QString err;
        if (!applyShareConfigIfIdleLocked(context, desired, &err)) {
            if (error) {
                *error = err;
            }
            return false;
        }
    }

    if (!context->share || !context->applied.cookies) {
        if (error) {
            *error = QStringLiteral("share cookie store 未启用");
        }
        return false;
    }

    CURL *easy = curl_easy_init();
    if (!easy) {
        if (error) {
            *error = QStringLiteral("curl_easy_init 失败");
        }
        return false;
    }

    std::unique_ptr<CURL, decltype(&curl_easy_cleanup)> easyGuard(easy, &curl_easy_cleanup);
    curl_easy_setopt(easy, CURLOPT_SHARE, context->share);
    curl_easy_setopt(easy, CURLOPT_COOKIEFILE, "");

    for (const QNetworkCookie &raw : cookies) {
        QNetworkCookie c = raw;
        if (c.domain().isEmpty() && !originUrl.host().isEmpty()) {
            c.setDomain(originUrl.host());
        }
        if (c.path().isEmpty()) {
            c.setPath(QStringLiteral("/"));
        }

        if (c.domain().isEmpty()) {
            continue;
        }

        const QByteArray domainBytes = c.domain().toUtf8();
        const QByteArray pathBytes   = c.path().toUtf8();

        const bool includeSubdomains            = domainBytes.startsWith('.');
        const QByteArray includeSubdomainsBytes = includeSubdomains ? QByteArray("TRUE")
                                                                    : QByteArray("FALSE");
        const QByteArray secureBytes = c.isSecure() ? QByteArray("TRUE") : QByteArray("FALSE");

        qint64 expiresEpoch = 0;
        if (c.expirationDate().isValid()) {
            expiresEpoch = c.expirationDate().toSecsSinceEpoch();
            if (expiresEpoch < 0) {
                expiresEpoch = 0;
            }
        }

        QByteArray cookieLineDomain = domainBytes;
        if (c.isHttpOnly()) {
            cookieLineDomain = QByteArray("#HttpOnly_") + cookieLineDomain;
        }

        const QByteArray cookieLine = cookieLineDomain + '\t' + includeSubdomainsBytes + '\t'
                                      + pathBytes + '\t' + secureBytes + '\t'
                                      + QByteArray::number(expiresEpoch) + '\t' + c.name() + '\t'
                                      + c.value();

        const CURLcode rc = curl_easy_setopt(easy, CURLOPT_COOKIELIST, cookieLine.constData());
        if (rc != CURLE_OK) {
            if (error) {
                *error = QStringLiteral("导入 cookie 失败（%1）")
                                .arg(QString::fromUtf8(curl_easy_strerror(rc)));
            }
            return false;
        }
    }

    static_cast<void>(curl_easy_setopt(easy, CURLOPT_COOKIELIST, "FLUSH"));
    return true;
}

QList<QNetworkCookie> QCCurlMultiManager::exportCookiesForManager(
    const QCNetworkAccessManager *manager, const QUrl &filterUrl, QString *error)
{
    if (!manager) {
        if (error) {
            *error = QStringLiteral("manager 为空");
        }
        return {};
    }

    const ShareConfig desired = toShareConfig(manager);
    if (!desired.cookies) {
        if (error) {
            *error = QStringLiteral("exportCookies 需要启用 ShareHandleConfig.shareCookies");
        }
        return {};
    }

    QMutexLocker locker(&m_mutex);
    ShareContext *context = getOrCreateShareContextLocked(manager);
    if (!context) {
        if (error) {
            *error = QStringLiteral("share context 不可用");
        }
        return {};
    }

    if (!context->share || (context->applied != desired)) {
        if (context->activeUsers != 0) {
            if (error) {
                *error = QStringLiteral("share handle 正在使用中，无法初始化/切换配置");
            }
            return {};
        }
        QString err;
        if (!applyShareConfigIfIdleLocked(context, desired, &err)) {
            if (error) {
                *error = err;
            }
            return {};
        }
    }

    if (!context->share || !context->applied.cookies) {
        if (error) {
            *error = QStringLiteral("share cookie store 未启用");
        }
        return {};
    }

    CURL *easy = curl_easy_init();
    if (!easy) {
        if (error) {
            *error = QStringLiteral("curl_easy_init 失败");
        }
        return {};
    }

    std::unique_ptr<CURL, decltype(&curl_easy_cleanup)> easyGuard(easy, &curl_easy_cleanup);
    curl_easy_setopt(easy, CURLOPT_SHARE, context->share);
    curl_easy_setopt(easy, CURLOPT_COOKIEFILE, "");

    struct curl_slist *cookieList = nullptr;
    const CURLcode getInfoRc      = curl_easy_getinfo(easy, CURLINFO_COOKIELIST, &cookieList);
    if (getInfoRc != CURLE_OK) {
        if (error) {
            *error = QStringLiteral("读取 cookie 列表失败（%1）")
                         .arg(QString::fromUtf8(curl_easy_strerror(getInfoRc)));
        }
        return {};
    }

    QList<QNetworkCookie> out;
    const QString host    = filterUrl.host();
    const QString urlPath = filterUrl.path();
    for (auto *it = cookieList; it; it = it->next) {
        if (!it->data) {
            continue;
        }
        const QByteArray line = QByteArray(it->data);
        auto parsed           = parseCurlCookieLine(line);
        if (!parsed.has_value()) {
            continue;
        }
        if (!host.isEmpty()) {
            if (!domainMatchesHost(parsed->domain(), host)) {
                continue;
            }
            if (!pathMatchesUrl(parsed->path(), urlPath)) {
                continue;
            }
        }
        out.append(*parsed);
    }

    curl_slist_free_all(cookieList);
    return out;
}

bool QCCurlMultiManager::clearAllCookiesForManager(const QCNetworkAccessManager *manager,
                                                   QString *error)
{
    if (!manager) {
        if (error) {
            *error = QStringLiteral("manager 为空");
        }
        return false;
    }

    const ShareConfig desired = toShareConfig(manager);
    if (!desired.cookies) {
        if (error) {
            *error = QStringLiteral("clearAllCookies 需要启用 ShareHandleConfig.shareCookies");
        }
        return false;
    }

    QMutexLocker locker(&m_mutex);
    ShareContext *context = getOrCreateShareContextLocked(manager);
    if (!context) {
        if (error) {
            *error = QStringLiteral("share context 不可用");
        }
        return false;
    }

    if (!context->share || (context->applied != desired)) {
        if (context->activeUsers != 0) {
            if (error) {
                *error = QStringLiteral("share handle 正在使用中，无法初始化/切换配置");
            }
            return false;
        }
        QString err;
        if (!applyShareConfigIfIdleLocked(context, desired, &err)) {
            if (error) {
                *error = err;
            }
            return false;
        }
    }

    if (!context->share || !context->applied.cookies) {
        if (error) {
            *error = QStringLiteral("share cookie store 未启用");
        }
        return false;
    }

    CURL *easy = curl_easy_init();
    if (!easy) {
        if (error) {
            *error = QStringLiteral("curl_easy_init 失败");
        }
        return false;
    }

    std::unique_ptr<CURL, decltype(&curl_easy_cleanup)> easyGuard(easy, &curl_easy_cleanup);
    curl_easy_setopt(easy, CURLOPT_SHARE, context->share);
    curl_easy_setopt(easy, CURLOPT_COOKIEFILE, "");

    const CURLcode rc = curl_easy_setopt(easy, CURLOPT_COOKIELIST, "ALL");
    if (rc != CURLE_OK) {
        if (error) {
            if (isCapabilityRelatedCurlError(rc)) {
                *error = QStringLiteral("libcurl 不支持 CURLOPT_COOKIELIST（%1）")
                                .arg(QString::fromUtf8(curl_easy_strerror(rc)));
            } else {
                *error = QStringLiteral("清空 cookies 失败（%1）")
                                .arg(QString::fromUtf8(curl_easy_strerror(rc)));
            }
        }
        return false;
    }
    static_cast<void>(curl_easy_setopt(easy, CURLOPT_COOKIELIST, "FLUSH"));
    return true;
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

void QCCurlMultiManager::applyLimitsConfig(const QCNetworkConnectionPoolConfig &config)
{
    if (m_isShuttingDown.load(std::memory_order_relaxed)) {
        return;
    }

    if (QThread::currentThread() != thread()) {
        QMetaObject::invokeMethod(
            this, [this, config]() { applyLimitsConfig(config); }, Qt::QueuedConnection);
        return;
    }

    if (!m_multiHandle) {
        return;
    }

    const std::optional<long> newMaxTotal    = config.multiMaxTotalConnections;
    const std::optional<long> newMaxHost     = config.multiMaxHostConnections;
    const std::optional<long> newMaxStreams  = config.multiMaxConcurrentStreams;
    const std::optional<long> newMaxConnects = config.multiMaxConnects;

    const bool clearRequested = (!newMaxTotal.has_value() && m_multiMaxTotalConnections.has_value())
                                || (!newMaxHost.has_value() && m_multiMaxHostConnections.has_value())
                                || (!newMaxStreams.has_value()
                                    && m_multiMaxConcurrentStreams.has_value())
                                || (!newMaxConnects.has_value() && m_multiMaxConnects.has_value());

    auto canRecreateMultiHandle = [this]() -> bool {
        QMutexLocker locker(&m_mutex);
        return m_activeReplies.isEmpty() && m_socketMap.isEmpty()
               && (m_runningRequests.load(std::memory_order_relaxed) == 0);
    };

    auto recreateMultiHandle = [this]() -> bool {
        if (m_socketTimer) {
            m_socketTimer->stop();
        }

        if (m_multiHandle) {
            curl_multi_setopt(m_multiHandle, CURLMOPT_SOCKETDATA, nullptr);
            curl_multi_setopt(m_multiHandle, CURLMOPT_SOCKETFUNCTION, nullptr);
            curl_multi_setopt(m_multiHandle, CURLMOPT_TIMERDATA, nullptr);
            curl_multi_setopt(m_multiHandle, CURLMOPT_TIMERFUNCTION, nullptr);
            curl_multi_cleanup(m_multiHandle);
            m_multiHandle = nullptr;
        }

        m_multiHandle = curl_multi_init();
        if (!m_multiHandle) {
            qCritical() << "QCCurlMultiManager::applyLimitsConfig: Failed to reinitialize curl "
                           "multi handle";
            return false;
        }

        CURLMcode ret = curl_multi_setopt(m_multiHandle, CURLMOPT_SOCKETDATA, this);
        if (ret != CURLM_OK) {
            qCritical()
                << "QCCurlMultiManager::applyLimitsConfig: Failed to set CURLMOPT_SOCKETDATA:"
                << ret;
        }

        ret = curl_multi_setopt(m_multiHandle, CURLMOPT_SOCKETFUNCTION, curlSocketCallback);
        if (ret != CURLM_OK) {
            qCritical()
                << "QCCurlMultiManager::applyLimitsConfig: Failed to set CURLMOPT_SOCKETFUNCTION:"
                << ret;
        }

        ret = curl_multi_setopt(m_multiHandle, CURLMOPT_TIMERDATA, this);
        if (ret != CURLM_OK) {
            qCritical()
                << "QCCurlMultiManager::applyLimitsConfig: Failed to set CURLMOPT_TIMERDATA:"
                << ret;
        }

        ret = curl_multi_setopt(m_multiHandle, CURLMOPT_TIMERFUNCTION, curlTimerCallback);
        if (ret != CURLM_OK) {
            qCritical()
                << "QCCurlMultiManager::applyLimitsConfig: Failed to set CURLMOPT_TIMERFUNCTION:"
                << ret;
        }

        return true;
    };

    if (clearRequested) {
        if (!canRecreateMultiHandle()) {
            qWarning()
                << "QCCurlMultiManager::applyLimitsConfig: Some multi limits were cleared, but "
                   "active requests exist; "
                   "cannot reset multi handle safely (limits keep previous values until restart)";
        } else {
            if (!recreateMultiHandle()) {
                qWarning() << "QCCurlMultiManager::applyLimitsConfig: Failed to reset multi "
                              "handle; keeping previous limits";
            } else {
                m_multiMaxTotalConnections.reset();
                m_multiMaxHostConnections.reset();
                m_multiMaxConcurrentStreams.reset();
                m_multiMaxConnects.reset();
            }
        }
    }

    auto setMultiLongOption = [this](CURLMoption option,
                                     const char *optionName,
                                     long value,
                                     std::optional<long> &stateSlot) {
        const CURLMcode rc = curl_multi_setopt(m_multiHandle, option, value);
        if (rc == CURLM_OK) {
            stateSlot = value;
            return;
        }

        if (rc == CURLM_UNKNOWN_OPTION) {
            qWarning() << "QCCurlMultiManager capability warning: libcurl 不支持" << optionName
                       << "(" << curl_multi_strerror(rc) << ")";
            return;
        }

        qWarning() << "QCCurlMultiManager: Failed to set" << optionName << "("
                   << curl_multi_strerror(rc) << ")";
    };

    if (newMaxTotal.has_value()) {
        setMultiLongOption(CURLMOPT_MAX_TOTAL_CONNECTIONS,
                           "CURLMOPT_MAX_TOTAL_CONNECTIONS",
                           newMaxTotal.value(),
                           m_multiMaxTotalConnections);
    }

    if (newMaxHost.has_value()) {
        setMultiLongOption(CURLMOPT_MAX_HOST_CONNECTIONS,
                           "CURLMOPT_MAX_HOST_CONNECTIONS",
                           newMaxHost.value(),
                           m_multiMaxHostConnections);
    }

    if (newMaxStreams.has_value()) {
        setMultiLongOption(CURLMOPT_MAX_CONCURRENT_STREAMS,
                           "CURLMOPT_MAX_CONCURRENT_STREAMS",
                           newMaxStreams.value(),
                           m_multiMaxConcurrentStreams);
    }

    if (newMaxConnects.has_value()) {
        setMultiLongOption(CURLMOPT_MAXCONNECTS,
                           "CURLMOPT_MAXCONNECTS",
                           newMaxConnects.value(),
                           m_multiMaxConnects);
    }
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
            cleanupEasyHandleLocked(easy, true, true, "QCCurlMultiManager::checkMultiInfo");
            continue;
        }

        QCNetworkReply *reply = replyPtr.data();

        // 获取响应码和重定向 URL（调试信息）
        long responseCode = 0;
        curl_easy_getinfo(easy, CURLINFO_RESPONSE_CODE, &responseCode);

        char *redirectUrl = nullptr;
        curl_easy_getinfo(easy, CURLINFO_REDIRECT_URL, &redirectUrl);

        qDebug() << "QCCurlMultiManager::checkMultiInfo: Request finished"
                 << "Reply:" << reply << "CURLcode:" << message->data.result
                 << "HTTP code:" << responseCode
                 << "Redirect:" << (redirectUrl ? redirectUrl : "none");

        cleanupEasyHandleLocked(easy, true, true, "QCCurlMultiManager::checkMultiInfo");

        // 解锁后发射信号（避免死锁）
        locker.unlock();

        // 发射完成信号（Reply 对象通过连接处理）
        emit requestFinished(reply, message->data.result);

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

    // 创建 SocketInfo（如果不存在）
    if (!socketInfo) {
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
    }

    // CURL_POLL_IN 或 CURL_POLL_INOUT: 启用读取
    if (what == CURL_POLL_IN || what == CURL_POLL_INOUT) {
        if (!socketInfo->readNotifier) {
            socketInfo->readNotifier = new QSocketNotifier(socketfd, QSocketNotifier::Read, this);

            connect(socketInfo->readNotifier, &QSocketNotifier::activated, this, [this, socketfd]() {
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

            connect(socketInfo->writeNotifier, &QSocketNotifier::activated, this, [this, socketfd]() {
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

// ==================
// libcurl 静态回调
// ==================

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
