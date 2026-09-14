#include "QCCurlMultiManager.h"
#include "QCNetworkAccessManager.h"
#include "QCNetworkAccessManager_p.h"

#include <QAbstractEventDispatcher>
#include <QFutureInterface>
#include <QMetaObject>
#include <QMutexLocker>
#include <QThread>

#include <atomic>
#include <memory>
#include <utility>

namespace {

bool hasEventDispatcher(QThread *thread)
{
    return (thread != nullptr) && (QAbstractEventDispatcher::instance(thread) != nullptr);
}

QCurl::QCCookieOperationResult cookieOperationFailure(const QString &error)
{
    return QCurl::QCCookieOperationResult::failure(QCurl::QCCookieAsyncError::BusinessError,
                                                   error,
                                                   QStringLiteral("cookie.multi_unavailable"));
}

QCurl::QCCookieExportResult cookieExportFailure(const QString &error)
{
    return QCurl::QCCookieExportResult::failure(QCurl::QCCookieAsyncError::BusinessError,
                                                error,
                                                QStringLiteral("cookie.multi_unavailable"));
}

QCurl::QCCookieOperationResult operationResult(const QCurl::Internal::CookieStoreResult &storeResult)
{
    return storeResult.isSuccess()
               ? QCurl::QCCookieOperationResult::success()
               : QCurl::QCCookieOperationResult::failure(QCurl::QCCookieAsyncError::BusinessError,
                                                         storeResult.message,
                                                         storeResult.policyCode);
}

QCurl::QCCookieExportResult exportResult(const QCurl::Internal::CookieStoreResult &storeResult)
{
    return storeResult.isSuccess()
               ? QCurl::QCCookieExportResult::success(storeResult.cookies)
               : QCurl::QCCookieExportResult::failure(QCurl::QCCookieAsyncError::BusinessError,
                                                      storeResult.message,
                                                      storeResult.policyCode);
}

template<typename Result>
class CookieCompletionState
{
public:
    CookieCompletionState()
    {
        m_interface.reportStarted();
    }

    [[nodiscard]] QFuture<Result> future() { return m_interface.future(); }
    [[nodiscard]] bool isCanceled() const { return m_interface.isCanceled(); }

    bool complete(Result result)
    {
        bool expected = false;
        if (!m_completed.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
            return false;
        }
        {
            // 取消只阻止尚未执行的命令，不丢弃结构化终态。Qt 6.10 的 reportResult()
            // 会拒绝已取消的 Future，因此在其互斥锁内直接发布唯一结果，不清除取消标志。
            QMutexLocker locker(&m_interface.mutex());
            auto &store     = m_interface.resultStoreBase();
            const int index = store.template emplaceResult<Result>(0, std::move(result));
            Q_ASSERT(index == 0);
            m_interface.reportResultsReady(index, index + 1);
        }
        m_interface.reportFinished();
        return true;
    }

private:
    QFutureInterface<Result> m_interface;
    std::atomic_bool m_completed{false};
};

template<typename Result>
QFuture<Result> finishedFuture(Result result)
{
    auto state = std::make_shared<CookieCompletionState<Result>>();
    state->complete(std::move(result));
    return state->future();
}

template<typename Result, typename Command, typename Failure>
QFuture<Result> runCookieCommandAsync(QCurl::QCNetworkAccessManager *manager,
                                      Command command,
                                      Failure failure)
{
    if (!manager || !hasEventDispatcher(manager->thread())) {
        return finishedFuture(failure(QCurl::QCCookieAsyncError::DispatchFailed,
                                      QStringLiteral("QCNetworkAccessManager: owner 线程缺少 Qt "
                                                     "事件循环，无法执行 cookie async bridge")));
    }

    auto completion          = std::make_shared<CookieCompletionState<Result>>();
    const auto future        = completion->future();
    auto destroyedConnection = std::make_shared<QMetaObject::Connection>();
    *destroyedConnection     = QObject::connect(
        manager,
        &QObject::destroyed,
        manager,
        [completion, failure]() {
            completion->complete(
                failure(QCurl::QCCookieAsyncError::ManagerDestroyed,
                        QStringLiteral("QCNetworkAccessManager 已销毁，cookie 操作未执行")));
        },
        Qt::DirectConnection);
    const bool invoked = QMetaObject::invokeMethod(
        manager,
        [completion, destroyedConnection, command = std::move(command), failure]() mutable {
            if (completion->isCanceled()) {
                completion->complete(failure(QCurl::QCCookieAsyncError::Cancelled,
                                             QStringLiteral("cookie 操作已取消")));
            } else {
                completion->complete(command());
            }
            QObject::disconnect(*destroyedConnection);
        },
        Qt::QueuedConnection);
    if (!invoked) {
        completion->complete(
            failure(QCurl::QCCookieAsyncError::DispatchFailed,
                    QStringLiteral(
                        "QCNetworkAccessManager: 无法提交 cookie async bridge 到 owner 线程")));
        QObject::disconnect(*destroyedConnection);
    }
    return future;
}

} // namespace

namespace QCurl {

QString QCNetworkAccessManager::cookieFilePath() const
{
    Q_D(const QCNetworkAccessManager);
    return d->cookieFilePath;
}

QCNetworkAccessManager::CookieFileModeFlag QCNetworkAccessManager::cookieFileMode() const
{
    Q_D(const QCNetworkAccessManager);
    return d->cookieModeFlag;
}

void QCNetworkAccessManager::setCookieFilePath(const QString &cookieFilePath,
                                               CookieFileModeFlag flag)
{
    Q_D(QCNetworkAccessManager);
    d->cookieFilePath = cookieFilePath;
    d->cookieModeFlag = flag;
}

QCCookieOperationResult QCNetworkAccessManagerPrivate::importCookiesOnOwnerThread(
    const QList<QCCookie> &cookies, const QUrl &originUrl)
{
    auto *multi = QCCurlMultiManager::instance();
    if (!multi) {
        return cookieOperationFailure(QStringLiteral("QCCurlMultiManager 不可用"));
    }
    return operationResult(multi->importCookiesForManager(q_func(), cookies, originUrl));
}

QCCookieExportResult QCNetworkAccessManagerPrivate::exportCookiesOnOwnerThread(
    const QUrl &filterUrl) const
{
    auto *multi = QCCurlMultiManager::instance();
    if (!multi) {
        return cookieExportFailure(QStringLiteral("QCCurlMultiManager 不可用"));
    }
    auto *manager = const_cast<QCNetworkAccessManager *>(q_func());
    return exportResult(multi->exportCookiesForManager(manager, filterUrl));
}

QCCookieOperationResult QCNetworkAccessManagerPrivate::clearAllCookiesOnOwnerThread()
{
    auto *multi = QCCurlMultiManager::instance();
    if (!multi) {
        return cookieOperationFailure(QStringLiteral("QCCurlMultiManager 不可用"));
    }
    return operationResult(multi->clearAllCookiesForManager(q_func()));
}

QFuture<QCCookieOperationResult> QCNetworkAccessManagerPrivate::runCookieOperationAsync(
    QCNetworkAccessManager *manager, std::function<QCCookieOperationResult()> command)
{
    return runCookieCommandAsync<QCCookieOperationResult>(
        manager, std::move(command), [](QCCookieAsyncError code, const QString &message) {
            return QCCookieOperationResult::failure(code, message);
        });
}

QFuture<QCCookieExportResult> QCNetworkAccessManagerPrivate::runCookieExportAsync(
    QCNetworkAccessManager *manager, std::function<QCCookieExportResult()> command)
{
    return runCookieCommandAsync<QCCookieExportResult>(
        manager, std::move(command), [](QCCookieAsyncError code, const QString &message) {
            return QCCookieExportResult::failure(code, message);
        });
}

bool QCNetworkAccessManager::importCookies(const QList<QCCookie> &cookies,
                                           const QUrl &originUrl,
                                           QString *error)
{
    if (QThread::currentThread() != thread()) {
        if (error) {
            *error = QStringLiteral("QCNetworkAccessManager::importCookies: 只允许在 owner "
                                    "thread 调用；跨线程请使用 importCookiesAsync()");
        }
        return false;
    }

    Q_D(QCNetworkAccessManager);
    const auto result = d->importCookiesOnOwnerThread(cookies, originUrl);
    if (error) {
        *error = result.error();
    }
    return result.isSuccess();
}

std::optional<QList<QCCookie>> QCNetworkAccessManager::exportCookies(const QUrl &filterUrl,
                                                                     QString *error) const
{
    if (QThread::currentThread() != thread()) {
        if (error) {
            *error = QStringLiteral("QCNetworkAccessManager::exportCookies: 只允许在 owner "
                                    "thread 调用；跨线程请使用 exportCookiesAsync()");
        }
        return std::nullopt;
    }

    Q_D(const QCNetworkAccessManager);
    const auto result = d->exportCookiesOnOwnerThread(filterUrl);
    if (error) {
        *error = result.error();
    }
    if (!result.isSuccess()) {
        return std::nullopt;
    }
    return result.cookies();
}

bool QCNetworkAccessManager::clearAllCookies(QString *error)
{
    if (QThread::currentThread() != thread()) {
        if (error) {
            *error = QStringLiteral("QCNetworkAccessManager::clearAllCookies: 只允许在 owner "
                                    "thread 调用；跨线程请使用 clearAllCookiesAsync()");
        }
        return false;
    }

    Q_D(QCNetworkAccessManager);
    const auto result = d->clearAllCookiesOnOwnerThread();
    if (error) {
        *error = result.error();
    }
    return result.isSuccess();
}

QFuture<QCCookieOperationResult> QCNetworkAccessManager::importCookiesAsync(
    const QList<QCCookie> &cookies, const QUrl &originUrl)
{
    Q_D(QCNetworkAccessManager);
    return d->runCookieOperationAsync(this, [d, cookies, originUrl]() {
        return d->importCookiesOnOwnerThread(cookies, originUrl);
    });
}

QFuture<QCCookieExportResult> QCNetworkAccessManager::exportCookiesAsync(const QUrl &filterUrl) const
{
    Q_D(const QCNetworkAccessManager);
    auto *manager  = const_cast<QCNetworkAccessManager *>(this);
    auto *mutableD = const_cast<QCNetworkAccessManagerPrivate *>(d);
    return mutableD->runCookieExportAsync(manager, [d, filterUrl]() {
        return d->exportCookiesOnOwnerThread(filterUrl);
    });
}

QFuture<QCCookieOperationResult> QCNetworkAccessManager::clearAllCookiesAsync()
{
    Q_D(QCNetworkAccessManager);
    return d->runCookieOperationAsync(this, [d]() { return d->clearAllCookiesOnOwnerThread(); });
}

void QCNetworkAccessManager::setShareHandleConfig(const ShareHandleConfig &config)
{
    Q_D(QCNetworkAccessManager);
    d->shareHandleConfig = config;
}

QCNetworkAccessManager::ShareHandleConfig QCNetworkAccessManager::shareHandleConfig() const noexcept
{
    Q_D(const QCNetworkAccessManager);
    return d->shareHandleConfig;
}

void QCNetworkAccessManager::setHstsAltSvcCacheConfig(const HstsAltSvcCacheConfig &config)
{
    Q_D(QCNetworkAccessManager);
    d->hstsAltSvcCacheConfig = config;
}

QCNetworkAccessManager::HstsAltSvcCacheConfig QCNetworkAccessManager::hstsAltSvcCacheConfig()
    const noexcept
{
    Q_D(const QCNetworkAccessManager);
    return d->hstsAltSvcCacheConfig;
}

} // namespace QCurl
