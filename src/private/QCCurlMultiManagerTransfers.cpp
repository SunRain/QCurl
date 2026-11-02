/**
 * @file
 * @brief 登记 manager 持有的 multi 传输并处理移除请求。
 */

#include "QCCurlMultiManager.h"
#include "QCNetworkConnectionPoolManager.h"
#include "private/QCCurlMultiDomain_p.h"
#include "private/QCCurlMultiTransferRecord_p.h"
#include "private/QCNetworkReplyCallbacks_p.h"

#include <QDebug>
#include <QMetaObject>
#include <QMutexLocker>
#include <QThread>

#include <utility>

namespace QCurl {

namespace {

void setTransferError(QString *error, const QString &message)
{
    if (error) {
        *error = message;
    }
}

[[nodiscard]] bool validateTransferInput(const QCCurlHandleManager &handle,
                                         bool hasCompletion,
                                         bool shuttingDown,
                                         bool managerReady,
                                         const QString &initializationError,
                                         const QString &kind,
                                         const QCCurlMultiManager *manager,
                                         QString *error)
{
    if (QThread::currentThread() != manager->thread()) {
        setTransferError(error,
                         QStringLiteral("%1 multi 传输必须在 manager 所在线程注册").arg(kind));
        return false;
    }
    if (shuttingDown || !managerReady) {
        setTransferError(error,
                         initializationError.isEmpty() ? QStringLiteral("curl multi manager 未就绪")
                                                       : initializationError);
        return false;
    }
    if (!handle.isValid()) {
        setTransferError(error, QStringLiteral("%1 multi 传输的 easy handle 无效").arg(kind));
        return false;
    }
    if (!hasCompletion) {
        setTransferError(error, QStringLiteral("%1 multi 传输缺少完成回调").arg(kind));
        return false;
    }
    return true;
}

} // namespace

#ifdef QCURL_ENABLE_TEST_HOOKS
bool QCCurlMultiManager::addTransferForTest(TransferToken *token, QString *error)
{
    QCCurlHandleManager handle;
    return addTransfer(
        std::move(handle),
        [this](QCCurlHandleManager &&completedHandle, CURLcode, long) {
            ++m_testCompletionCount;
            m_testCompletionHadHandle = completedHandle.isValid();
        },
        token,
        error);
}

void QCCurlMultiManager::processUnknownDoneForTest()
{
    CURLMsg message{};
    message.msg         = CURLMSG_DONE;
    message.easy_handle = reinterpret_cast<CURL *>(quintptr(1));
    message.data.result = CURLE_OK;
    static_cast<void>(takeFinishedTransferLocked(&message));
}
#endif

bool QCCurlMultiManager::addTransfer(QCCurlHandleManager &&handle,
                                     TransferCompletionHandler completion,
                                     TransferToken *tokenOut,
                                     QString *error)
{
    if (tokenOut) {
        *tokenOut = 0;
    }
    if (!validateTransferInput(handle,
                               static_cast<bool>(completion),
                               m_isShuttingDown.load(std::memory_order_relaxed),
                               m_isReady && m_baseDomain && m_baseDomain->multi,
                               m_initializationError,
                               QStringLiteral("generic"),
                               this,
                               error)) {
        return false;
    }

    auto transfer = QSharedPointer<QCCurlMultiTransferRecord>::create(std::move(handle));
    transfer->bindCompletionHandler(std::move(completion));
    return registerTransferRecord(transfer, tokenOut, error);
}

bool QCCurlMultiManager::addPersistentTransfer(QCCurlHandleManager &&handle,
                                               TransferPersistentCompletionHandler completion,
                                               TransferToken *tokenOut,
                                               QString *error)
{
    if (tokenOut) {
        *tokenOut = 0;
    }
    if (!validateTransferInput(handle,
                               static_cast<bool>(completion),
                               m_isShuttingDown.load(std::memory_order_relaxed),
                               m_isReady && m_baseDomain && m_baseDomain->multi,
                               m_initializationError,
                               QStringLiteral("persistent"),
                               this,
                               error)) {
        return false;
    }

    auto transfer = QSharedPointer<QCCurlMultiTransferRecord>::create(std::move(handle));
    transfer->bindPersistentCompletionHandler(std::move(completion));
    return registerTransferRecord(transfer, tokenOut, error);
}

bool registerPersistentTransfer(QCCurlPersistentTransferConfigurator configure,
                                QCCurlPersistentTransferCompletionHandler completion,
                                QCCurlTransferToken *token,
                                QString *error)
{
    QCCurlHandleManager handle;
    if (!handle.isValid()) {
        setTransferError(error,
                         handle.initializationError().isEmpty()
                             ? QStringLiteral("persistent multi 传输的 easy handle 初始化失败")
                             : handle.initializationError());
        return false;
    }
    if (!configure) {
        setTransferError(error, QStringLiteral("persistent multi 传输缺少配置回调"));
        return false;
    }

    QString configureError;
    if (!configure(handle.handle(), &configureError)) {
        setTransferError(error,
                         configureError.isEmpty()
                             ? QStringLiteral("persistent easy handle 配置失败")
                             : configureError);
        return false;
    }
    return QCCurlMultiManager::instance()->addPersistentTransfer(std::move(handle),
                                                                 std::move(completion),
                                                                 token,
                                                                 error);
}

void removePersistentTransfer(QCCurlTransferToken token)
{
    QCCurlMultiManager::instance()->removeTransfer(token);
}

bool QCCurlMultiManager::registerTransferRecord(
    const QSharedPointer<QCCurlMultiTransferRecord> &transfer,
    TransferToken *tokenOut,
    QString *error)
{
    QMutexLocker locker(&m_mutex);
    QString bindError;
    if (!transfer->bindPrivate(&bindError)) {
        setTransferError(error, bindError);
        return false;
    }

    CURL *easy         = transfer->handle();
    const QString kind = transfer->hasPersistentCompletionHandler() ? QStringLiteral("persistent")
                                                                    : QStringLiteral("generic");
    if (m_activeTransfers.contains(easy)) {
        setTransferError(error, QStringLiteral("%1 multi 传输的 easy handle 已注册").arg(kind));
        return false;
    }

    transfer->bindDomain(m_baseDomain);
    QString addError;
    if (!applyLimitsConfig(QCNetworkConnectionPoolManager::instance()->config(), &addError)) {
        setTransferError(error, addError);
        return false;
    }
    if (!addEasyToMultiLocked(transfer->domain().get(), easy, &addError)) {
        setTransferError(error, addError);
        return false;
    }

    const TransferToken token = commitTransferRecordLocked(transfer);
    if (tokenOut) {
        *tokenOut = token;
    }
    return true;
}

QCCurlMultiManager::TransferToken QCCurlMultiManager::commitTransferRecordLocked(
    const QSharedPointer<QCCurlMultiTransferRecord> &record)
{
    const TransferToken token = m_nextTransferToken++;
    if (m_nextTransferToken == 0) {
        m_nextTransferToken = 1;
    }
    record->setToken(token);
    m_activeTransfers.insert(record->handle(), record);
    record->publishActiveHandle();
    m_runningRequests.fetch_add(1, std::memory_order_relaxed);
    wakeupDomain(record->domain());
    return token;
}

void QCCurlMultiManager::removeTransfer(TransferToken token)
{
    if (token == 0 || m_isShuttingDown.load(std::memory_order_relaxed)
        || m_isPoisoned.load(std::memory_order_relaxed)) {
        return;
    }
    if (QThread::currentThread() != thread() || Internal::isInReplyCurlCallback()) {
        // 跨线程只投递值；回调内也延迟到安全点，不跨边界暴露 record 地址。
        QMetaObject::invokeMethod(
            this, [this, token]() { removeTransfer(token); }, Qt::QueuedConnection);
        return;
    }

    std::optional<FinishedTransfer> detached;
    {
        QMutexLocker locker(&m_mutex);
        for (auto it = m_activeTransfers.cbegin(); it != m_activeTransfers.cend(); ++it) {
            if (it.value()->token() == token) {
                detached = detachTransferRecordLocked(it.value().data(),
                                                      CURLE_ABORTED_BY_CALLBACK,
                                                      "QCCurlMultiManager::removeTransfer");
                break;
            }
        }
    }
    if (detached) {
        dispatchFinishedTransfer(std::move(detached.value()));
    }
}

} // namespace QCurl
