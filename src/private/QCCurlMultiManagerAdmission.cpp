#include "QCCurlMultiManager.h"
#include "QCNetworkAccessManager.h"
#include "QCNetworkConnectionPoolManager.h"
#include "QCNetworkConnectionPoolManager_p.h"
#include "QCNetworkReply_p.h"
#include "private/QCCurlMultiDomain_p.h"
#include "private/QCCurlMultiTransferRecord_p.h"
#include "private/QCCurlOptionAdapter_p.h"

#include <QDebug>
#include <QThread>

namespace QCurl {

namespace {

[[nodiscard]] bool isTerminalReplyState(ReplyState state)
{
    return state == ReplyState::Cancelled || state == ReplyState::Error
           || state == ReplyState::Finished;
}

[[nodiscard]] QString managerReadinessError(bool isReady,
                                            CURLM *multiHandle,
                                            const QString &initializationError)
{
    if (isReady && multiHandle) {
        return {};
    }

    return initializationError.isEmpty() ? QStringLiteral("curl multi manager 未就绪")
                                         : initializationError;
}

[[nodiscard]] QSharedPointer<QCCurlMultiTransferRecord> createBoundTransferRecord(
    QCNetworkReplyPrivate *replyPrivate, QCNetworkReply *reply, QString *errorMessage)
{
    auto record = QSharedPointer<QCCurlMultiTransferRecord>::create(
        std::move(replyPrivate->curlManager));
    record->bindObserver(replyPrivate->transferState, reply);
    if (record->bindCallbacks(replyPrivate->readCallbackConfigured,
                              replyPrivate->debugCallbackConfigured,
                              errorMessage)) {
        return record;
    }

    replyPrivate->curlManager = record->takeHandle();
    record->clearObserver();
    return {};
}

} // namespace

void QCCurlMultiManager::addReply(QCNetworkReply *reply)
{
    if (QThread::currentThread() != thread()) {
        qWarning() << "QCCurlMultiManager::addReply: owner thread required";
        return;
    }
    Q_ASSERT_X(QThread::currentThread() == thread(),
               "QCCurlMultiManager::addReply",
               "manager owner thread required");

    if (!reply) {
        qWarning() << "QCCurlMultiManager::addReply: reply is null";
        return;
    }

    QPointer<QCNetworkReply> safeReply(reply);
    const AddReplyResult result = tryAddReplyOnOwnerThread(reply);
    if (!result.success) {
        queueAddReplyFailure(safeReply, result.errorMessage);
    }
}

QCCurlMultiManager::AddReplyResult QCCurlMultiManager::tryAddReplyOnOwnerThread(QCNetworkReply *reply)
{
    Q_ASSERT(QThread::currentThread() == thread());

    if (m_isShuttingDown.load(std::memory_order_relaxed)) {
        return {false, QStringLiteral("curl multi manager 正在关闭")};
    }
    if (const QString reason = managerReadinessError(m_isReady,
                                                     m_baseDomain ? m_baseDomain->multi : nullptr,
                                                     m_initializationError);
        !reason.isEmpty()) {
        return {false, reason};
    }

    QMutexLocker locker(&m_mutex);
    QCNetworkReplyPrivate *replyPrivate = reply->d_func();
    if (isTerminalReplyState(replyPrivate->state)) {
        return {true, QString()};
    }

    CURL *easy = validatedEasyHandle(reply);
    if (!easy) {
        return {false, QStringLiteral("reply 的 curl easy handle 无效")};
    }
    if (m_activeTransfers.contains(easy)) {
        qWarning() << "QCCurlMultiManager::addReply: easy handle already registered";
        return {false, QStringLiteral("curl easy handle 已注册")};
    }

    QString domainError;
    auto domain = replyPrivate->transferState->transportDomain;
    if (!domain) {
        domain = selectDomain(replyPrivate->request, &domainError);
    }
    if (!domain || !domain->multi) {
        return {false, domainError};
    }
    replyPrivate->transferState->transportDomain = domain;
    domain->idleSince.reset();

    QString callbackError;
    auto record = createBoundTransferRecord(replyPrivate, reply, &callbackError);
    if (!record) {
        return {false, callbackError};
    }

    return admitReplyRecordLocked(reply, domain, std::move(record));
}

QCCurlMultiManager::AddReplyResult QCCurlMultiManager::rejectReplyAdmissionLocked(
    QCNetworkReply *reply,
    QSharedPointer<QCCurlMultiTransferRecord> record,
    ShareContext *context,
    const QString &error)
{
    // limits/add 失败也可能遇到 share 解绑失败；此时不能把 handle 交还普通析构路径。
    if (!detachShareBindingLocked(record->handle(),
                                  "QCCurlMultiManager::rejectReplyAdmissionLocked")) {
        CURL *easy = record->handle();
        quarantineUnregisteredTransferLocked(std::move(record), easy, context);
    } else {
        reply->d_func()->curlManager = record->takeHandle();
        record->clearObserver();
    }
    return {false, error};
}

QCCurlMultiManager::AddReplyResult QCCurlMultiManager::admitReplyRecordLocked(
    QCNetworkReply *reply, const Domain &domain, QSharedPointer<QCCurlMultiTransferRecord> record)
{
    CURL *easy = record->handle();
    record->bindDomain(domain);
    ShareContext *context  = prepareShareForReplyLocked(reply, easy);
    const auto shareResult = applyShareToEasyLocked(reply, easy, context);
    if (shareResult.state == ShareApplyState::NonCallable) {
        quarantineUnregisteredTransferLocked(std::move(record), easy, context);
        return {false, shareResult.errorMessage};
    }
    if (shareResult.state == ShareApplyState::Rejected) {
        return rejectReplyAdmissionLocked(reply,
                                          std::move(record),
                                          context,
                                          shareResult.errorMessage);
    }

    QString error;
    const auto config = QCNetworkConnectionPoolManager::instance()->config();
    if (!applyLimitsConfig(domain.get(), config, &error)
        || !Internal::QCNetworkConnectionPoolManagerInternal::configureCurlHandle(easy,
                                                                                  config,
                                                                                  &error)
        || !addEasyToMultiLocked(domain.get(), easy, &error)) {
        return rejectReplyAdmissionLocked(reply, std::move(record), context, error);
    }
    commitReplyAdmissionLocked(reply, domain, record);
    return {true, QString()};
}

void QCCurlMultiManager::commitReplyAdmissionLocked(
    QCNetworkReply *reply,
    const Domain &domain,
    const QSharedPointer<QCCurlMultiTransferRecord> &record)
{
    auto *replyPrivate                = reply->d_func();
    CURL *easy                        = record->handle();
    replyPrivate->multiTransferRecord = record.data();
    if (!replyPrivate->poolRequestActive) {
        replyPrivate->poolRequestActive = true;
        Internal::QCNetworkConnectionPoolManagerInternal::recordRequestStarted();
    }
    m_activeTransfers.insert(easy, record);
    m_runningRequests.fetch_add(1, std::memory_order_relaxed);
#ifdef QCURL_ENABLE_TEST_HOOKS
    reply->setProperty("_qcurl_transfer_record_owns_state",
                       record->ownsTransferState(replyPrivate->transferState));
#endif
    // scheduler 的 start handoff 依赖 multi loop 及时被唤醒，否则 execute() 后可能延迟首轮 poll。
    wakeupDomain(domain);

    qDebug() << "QCCurlMultiManager::addReply: Added reply" << reply
             << "Total running:" << m_runningRequests.load();
}

void QCCurlMultiManager::queueAddReplyFailure(const QPointer<QCNetworkReply> &reply,
                                              const QString &message)
{
    if (!reply) {
        return;
    }

    const QString resolvedMessage = message.isEmpty() ? QStringLiteral("curl multi 注册失败")
                                                      : message;
    QMetaObject::invokeMethod(
        reply.data(),
        [reply, resolvedMessage]() {
            if (reply) {
                reply->abortWithError(NetworkError::InvalidRequest, resolvedMessage);
            }
        },
        Qt::QueuedConnection);
}

void QCCurlMultiManager::removeReply(QCNetworkReply *reply)
{
    if (QThread::currentThread() != thread()) {
        qWarning() << "QCCurlMultiManager::removeReply: owner thread required";
        return;
    }
    Q_ASSERT_X(QThread::currentThread() == thread(),
               "QCCurlMultiManager::removeReply",
               "manager owner thread required");

    if (!reply) {
        return;
    }

    if (m_isShuttingDown.load(std::memory_order_relaxed)) {
        return;
    }

    removeTransferRecord(reply->d_func()->multiTransferRecord);
}

CURL *QCCurlMultiManager::validatedEasyHandle(QCNetworkReply *reply) const
{
    CURL *easy = reply->d_func()->curlManager.handle();
    if (!easy) {
        qCritical() << "QCCurlMultiManager::addReply: reply has invalid curl handle";
    }
    return easy;
}

bool QCCurlMultiManager::addEasyToMultiLocked(QCCurlMultiDomain *domain, CURL *easy, QString *error)
{
    const auto result = Internal::CurlOptions::addMultiHandle(domain->multi, easy);
    if (result == CURLM_OK) {
        return true;
    }
    if (error) {
        *error = QStringLiteral("curl_multi_add_handle 失败：%1")
                     .arg(QString::fromLatin1(curl_multi_strerror(result)));
    }
    return false;
}

} // namespace QCurl
