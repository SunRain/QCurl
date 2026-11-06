#include "QCCurlMultiManager.h"
#include "QCNetworkAccessManager.h"
#include "QCNetworkConnectionPoolManager.h"
#include "QCNetworkConnectionPoolManager_p.h"
#include "QCNetworkReply.h"
#include "private/QCCurlMultiDomain_p.h"
#include "private/QCCurlMultiTransferRecord_p.h"
#include "private/QCCurlOptionAdapter_p.h"

#include <QMutexLocker>
#include <QThread>

namespace QCurl {
namespace {

[[nodiscard]] bool isTerminalReplyState(ReplyState state)
{
    return state == ReplyState::Cancelled || state == ReplyState::Error
           || state == ReplyState::Finished;
}

} // namespace

QCCurlMultiManager::HttpAdmissionResult QCCurlMultiManager::admitHttpReply(
    QCCurlHandleManager &&handle,
    const HttpAdmission &admission,
    TransferCompletionHandler completion)
{
    HttpAdmissionResult result;
    result.returnedHandle.emplace(std::move(handle));
    if (QThread::currentThread() != thread()) {
        result.errorMessage = QStringLiteral("HTTP admission 必须在 multi owner thread 执行");
        return result;
    }
    if (m_isShuttingDown.load(std::memory_order_relaxed) || !m_isReady || !m_baseDomain) {
        result.errorMessage = m_initializationError.isEmpty()
                                  ? QStringLiteral("curl multi manager 未就绪或正在关闭")
                                  : m_initializationError;
        return result;
    }
    if (!admission.observer || admission.observer->thread() != thread()
        || (admission.manager && admission.manager->thread() != thread()) || !admission.state
        || !completion || !result.returnedHandle->isValid()) {
        result.errorMessage = QStringLiteral(
            "HTTP admission 的 observer、线程、state 或 handle 无效");
        return result;
    }
    if (admission.state->activeEasyHandle || isTerminalReplyState(admission.state->state)) {
        result.errorMessage = QStringLiteral("HTTP transfer 已注册或 reply 已进入终态");
        return result;
    }

    QMutexLocker locker(&m_mutex);
    auto record = QSharedPointer<QCCurlMultiTransferRecord>::create(
        std::move(result.returnedHandle.value()));
    record->bindObserver(admission.state, admission.observer);
    record->bindCompletionHandler(std::move(completion));
    return admitHttpRecordLocked(admission.manager, std::move(record));
}

QCCurlMultiManager::HttpAdmissionResult QCCurlMultiManager::admitHttpRecordLocked(
    const QCNetworkAccessManager *manager, QSharedPointer<QCCurlMultiTransferRecord> record)
{
    HttpAdmissionResult result;
    auto &state = *record->transferState();
    auto domain = state.transportDomain;
    if (!domain) {
        domain = selectDomain(state.curlPlan.normalized.request, &result.errorMessage);
    }
    if (!domain || !domain->multi || domain->owner != this) {
        if (result.errorMessage.isEmpty()) {
            result.errorMessage = QStringLiteral("HTTP transfer 的 transport domain 不可用");
        }
        result.returnedHandle.emplace(record->takeHandle());
        return result;
    }
    state.transportDomain = domain;
    domain->idleSince.reset();
    record->bindDomain(domain);
    if (!record->bindCallbacks(&result.errorMessage)) {
        return rejectHttpAdmissionLocked(std::move(record), std::move(result));
    }

    const auto share = bindShareToHttpLocked(manager, *record);
    result.warnings.append(share.warnings);
    if (share.state != Internal::ShareCallState::Callable) {
        result.errorMessage = share.message;
        return rejectHttpAdmissionLocked(std::move(record), std::move(result));
    }
    const auto config = QCNetworkConnectionPoolManager::instance()->config();
    if (!applyLimitsConfig(domain.get(), config, &result.errorMessage)
        || !Internal::QCNetworkConnectionPoolManagerInternal::configureCurlHandle(
            record->handle(), config, &result.errorMessage)
        || !addEasyToMultiLocked(domain.get(), record->handle(), &result.errorMessage)) {
        return rejectHttpAdmissionLocked(std::move(record), std::move(result));
    }

    result.token = commitTransferRecordLocked(record);
    result.state = HttpAdmissionState::Accepted;
    result.error = NetworkError::NoError;
    return result;
}

QCCurlMultiManager::HttpAdmissionResult QCCurlMultiManager::rejectHttpAdmissionLocked(
    QSharedPointer<QCCurlMultiTransferRecord> record, HttpAdmissionResult result)
{
    QString callbackError;
    const char *operation = "QCCurlMultiManager::rejectHttpAdmissionLocked";
    if (!m_isPoisoned.load(std::memory_order_relaxed)
        && detachShareBindingLocked(record->handle(), operation)
        && record->unbindCallbacks(&callbackError)) {
        result.returnedHandle.emplace(record->takeHandle());
        record->clearObserver();
        return result;
    }

    // 任何回滚不确定性都保留整个对象图，不把仍有原生借用的 handle 交给 reply。
    if (!m_isPoisoned.load(std::memory_order_relaxed)) {
        poisonLocked(operation, CURLM_INTERNAL_ERROR);
    }
    result.state        = HttpAdmissionState::Quarantined;
    result.errorMessage = callbackError.isEmpty() ? m_initializationError : callbackError;
    quarantineUnregisteredTransferLocked(std::move(record));
    return result;
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
