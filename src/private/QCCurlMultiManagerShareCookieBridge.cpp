#include "QCCurlMultiManager.h"
#include "QCNetworkAccessManager.h"
#include "private/QCCurlMultiTransferRecord_p.h"
#include "private/QCCurlShareCookieStore_p.h"

#include <QDebug>
#include <QMutexLocker>

namespace QCurl {

bool QCCurlMultiManager::consumeShareResultLocked(const Internal::ShareCallResult &result)
{
    for (const auto &warning : result.warnings) {
        qWarning().noquote() << "share 配置应用失败：" << warning;
    }
    if (result.state == Internal::ShareCallState::NonCallable) {
        const auto operation = result.operation.toLatin1();
        poisonLocked(operation.constData(), CURLM_INTERNAL_ERROR);
        return false;
    }
    return result.state == Internal::ShareCallState::Callable;
}

void QCCurlMultiManager::openShareScopeLocked(const QCNetworkAccessManager *manager)
{
    if (!m_shareCookieStore->openScope(manager)) {
        return;
    }
    // 同 owner thread 同步关闭旧身份；捕获地址仅作键，从不解引用已销毁对象。
    connect(manager, &QObject::destroyed, this, [this, manager]() {
        QMutexLocker locker(&m_mutex);
        static_cast<void>(consumeShareResultLocked(m_shareCookieStore->closeScope(manager)));
    });
}

void QCCurlMultiManager::synchronizeShareConfig(const QCNetworkAccessManager *manager)
{
    Q_ASSERT(QThread::currentThread() == thread());
    if (!manager || !m_isReady || m_isShuttingDown.load(std::memory_order_relaxed)) {
        return;
    }
    QMutexLocker locker(&m_mutex);
    auto result = m_shareCookieStore->synchronizeScope(manager, manager->shareHandleConfig());
    if (result.warnings.isEmpty() && !result.message.isEmpty()
        && result.state == Internal::ShareCallState::Callable) {
        result.warnings.append(result.message);
    }
    static_cast<void>(consumeShareResultLocked(result));
}

Internal::ShareCallResult QCCurlMultiManager::bindShareToHttpLocked(
    const QCNetworkAccessManager *manager, const QCCurlMultiTransferRecord &record)
{
    if (manager) {
        openShareScopeLocked(manager);
    }
    const auto config = manager ? manager->shareHandleConfig()
                                : QCNetworkAccessManager::ShareHandleConfig{};
    const auto &state = *record.transferState();
    auto result       = m_shareCookieStore->bindHttp(manager,
                                                     config,
                                                     record.handle(),
                                                     state.configuration.usesCookieFile());
    if (result.state == Internal::ShareCallState::NonCallable) {
        static_cast<void>(consumeShareResultLocked(result));
    }
    return result;
}

bool QCCurlMultiManager::detachShareBindingLocked(CURL *easy, const char *operation)
{
    return consumeShareResultLocked(m_shareCookieStore->unbindHttp(easy, operation));
}

bool QCCurlMultiManager::prepareCookieCommandLocked(const QCNetworkAccessManager *manager,
                                                    Internal::CookieCommandResult &result)
{
    if (QThread::currentThread() != thread() || !m_isReady
        || m_isShuttingDown.load(std::memory_order_relaxed)
        || (manager && manager->thread() != thread())) {
        result.lifecycle.state   = Internal::ShareCallState::Rejected;
        result.lifecycle.message = m_initializationError.isEmpty()
                                       ? QStringLiteral("Cookie 操作需要可用的 owner-thread multi")
                                       : m_initializationError;
        return false;
    }
    if (manager) {
        openShareScopeLocked(manager);
    }
    return true;
}

Internal::CookieCommandResult QCCurlMultiManager::publishCookieCommandLocked(
    Internal::CookieCommandResult result)
{
    if (consumeShareResultLocked(result.lifecycle) && !result.store.isSuccess()) {
        qWarning().noquote() << "QCurl cookie store:" << result.store.policyCode
                             << result.store.message;
    }
    return result;
}

Internal::CookieCommandResult QCCurlMultiManager::importCookiesForManager(
    const QCNetworkAccessManager *manager, const QList<QCCookie> &cookies, const QUrl &originUrl)
{
    QMutexLocker locker(&m_mutex);
    Internal::CookieCommandResult result;
    if (prepareCookieCommandLocked(manager, result)) {
        const auto config = manager ? manager->shareHandleConfig()
                                    : QCNetworkAccessManager::ShareHandleConfig{};
        result            = m_shareCookieStore->importCookies(manager, config, cookies, originUrl);
    }
    return publishCookieCommandLocked(std::move(result));
}

Internal::CookieCommandResult QCCurlMultiManager::exportCookiesForManager(
    const QCNetworkAccessManager *manager, const QUrl &filterUrl)
{
    QMutexLocker locker(&m_mutex);
    Internal::CookieCommandResult result;
    if (prepareCookieCommandLocked(manager, result)) {
        const auto config = manager ? manager->shareHandleConfig()
                                    : QCNetworkAccessManager::ShareHandleConfig{};
        result            = m_shareCookieStore->exportCookies(manager, config, filterUrl);
    }
    return publishCookieCommandLocked(std::move(result));
}

Internal::CookieCommandResult QCCurlMultiManager::clearAllCookiesForManager(
    const QCNetworkAccessManager *manager)
{
    QMutexLocker locker(&m_mutex);
    Internal::CookieCommandResult result;
    if (prepareCookieCommandLocked(manager, result)) {
        const auto config = manager ? manager->shareHandleConfig()
                                    : QCNetworkAccessManager::ShareHandleConfig{};
        result            = m_shareCookieStore->clearAllCookies(manager, config);
    }
    return publishCookieCommandLocked(std::move(result));
}

} // namespace QCurl
