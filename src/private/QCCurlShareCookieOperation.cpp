#include "private/QCCookiePolicyCode_p.h"
#include "private/QCCurlShareCookieStoreState_p.h"

namespace QCurl::Internal {

CookieStoreResult QCCurlShareCookieStore::State::failure(CookieStoreStatus status,
                                                         const QString &policy,
                                                         const QString &message)
{
    CookieStoreResult result;
    result.status     = status;
    result.policyCode = policy;
    result.message    = message;
    return result;
}

CookieStoreResult QCCurlShareCookieStore::State::optionFailure(CookieStoreStatus status,
                                                               const QString &policy,
                                                               const RequiredOptionResult &option)
{
    auto result       = failure(status, policy, option.message);
    result.curlCode   = option.curlCode;
    result.shareCode  = option.shareCode;
    result.optionName = option.optionName;
    return result;
}

CookieStoreResult QCCurlShareCookieStore::State::applied(QList<QCCookie> cookies)
{
    CookieStoreResult result;
    result.status     = CookieStoreStatus::Applied;
    result.policyCode = cookiepolicy::kApplied;
    result.cookies    = std::move(cookies);
    return result;
}

std::unique_ptr<QCCurlShareCookieStore::State::Operation>
QCCurlShareCookieStore::State::prepareOperation(const QCNetworkAccessManager *manager,
                                                const Config &desired,
                                                CookieCommandResult &result)
{
    result.lifecycle = admission();
    if (result.lifecycle.state != ShareCallState::Callable) {
        return {};
    }
    if (!manager || !desired.shareCookies()) {
        result.store = failure(CookieStoreStatus::RejectedBeforeMutation,
                               manager ? cookiepolicy::kShareDisabled
                                       : cookiepolicy::kInvalidManager,
                               manager ? QStringLiteral("cookie share 未启用")
                                       : QStringLiteral("manager 为空"));
        return {};
    }
    auto context = contextFor(manager);
    if (!context) {
        result.store = failure(CookieStoreStatus::RejectedBeforeMutation,
                               cookiepolicy::kShareContextUnavailable,
                               QStringLiteral("share context 不可用"));
        return {};
    }
    if (context->m_cookieStorePoisoned) {
        result.store = failure(CookieStoreStatus::StorePoisoned,
                               cookiepolicy::kStorePoisoned,
                               QStringLiteral("cookie store 已进入不可恢复状态"));
        return {};
    }
    if (!context->m_share || !sameConfig(context->m_applied, desired)) {
        if (hasUsers(*context)) {
            result.store = failure(CookieStoreStatus::RejectedBeforeMutation,
                                   cookiepolicy::kShareBusy,
                                   QStringLiteral("share handle 正在使用中，无法切换配置"));
            return {};
        }
        result.lifecycle = apply(*context, desired);
        if (result.lifecycle.state != ShareCallState::Callable || context->m_lastInitFailed) {
            result.store = context->m_lastInitResult;
            return {};
        }
    }
    auto operation       = std::make_unique<Operation>();
    operation->m_context = context;
    ++context->m_operations;
    observe("operation-enter");
    operation->m_handle.emplace();
    return operation;
}

CookieStoreResult QCCurlShareCookieStore::State::setupOperation(Operation &operation)
{
    CURL *easy = operation.m_handle->handle();
    if (!easy) {
        return failure(CookieStoreStatus::RejectedBeforeMutation,
                       cookiepolicy::kHandleInitFailed,
                       QStringLiteral("curl easy handle 初始化失败"));
    }
    auto option = operation.m_adapter.setEasy(easy,
                                              QCURL_CURL_OPTION(CURLOPT_SHARE),
                                              CookieOptionStage::Setup,
                                              operation.m_context->m_share);
    if (option.isSuccess()) {
        operation.m_bound = true;
        option            = operation.m_adapter.setEasy(easy,
                                                        QCURL_CURL_OPTION(CURLOPT_COOKIEFILE),
                                                        CookieOptionStage::Setup,
                                                        "");
    }
    return option.isSuccess() ? applied()
                              : optionFailure(CookieStoreStatus::RejectedBeforeMutation,
                                              cookiepolicy::kRequiredOptionFailed,
                                              option);
}

CookieCommandResult QCCurlShareCookieStore::State::finishOperation(
    std::unique_ptr<Operation> operation, CookieStoreResult result)
{
    CookieCommandResult command;
    command.store = std::move(result);
    if (m_phase != Phase::NonCallable && operation->m_bound) {
        const auto option = operation->m_adapter.setEasy(operation->m_handle->handle(),
                                                         QCURL_CURL_OPTION(CURLOPT_SHARE),
                                                         CookieOptionStage::Rollback,
                                                         nullptr);
        if (!option.isSuccess()) {
            command.lifecycle = failOwnership("cookie-operation-unbind",
                                              option.curlCode,
                                              option.shareCode);
        } else {
            operation->m_bound = false;
        }
    }
    if (m_phase == Phase::NonCallable) {
        // retained operation 由现有 multi quarantine 保活，不能展开临时 easy 的普通析构。
        m_retainedOperations.push_back(std::move(operation));
        command.lifecycle = m_ownershipFailure;
        return command;
    }
    const auto context = operation->m_context;
    operation->m_handle.reset();
    --context->m_operations;
    observe("operation-exit");
    command.lifecycle = finalize(context);
    return command;
}

CookieStoreResult QCCurlShareCookieStore::State::snapshot(Operation &operation,
                                                          QList<QByteArray> &lines)
{
    curl_slist *list  = nullptr;
    const auto option = operation.m_adapter.getCookieList(operation.m_handle->handle(),
                                                          CookieOptionStage::Snapshot,
                                                          &list);
    if (!option.isSuccess()) {
        curl_slist_free_all(list);
        return optionFailure(CookieStoreStatus::RejectedBeforeMutation,
                             cookiepolicy::kSnapshotFailed,
                             option);
    }
    for (const auto *entry = list; entry; entry = entry->next) {
        if (entry->data) {
            lines.append(QByteArray(entry->data));
        }
    }
    curl_slist_free_all(list);
    return applied();
}

RequiredOptionResult QCCurlShareCookieStore::State::restore(Operation &operation,
                                                            const QList<QByteArray> &lines)
{
    auto option = operation.m_adapter.setEasy(operation.m_handle->handle(),
                                              QCURL_CURL_OPTION(CURLOPT_COOKIELIST),
                                              CookieOptionStage::Rollback,
                                              "ALL");
    for (const auto &line : lines) {
        if (!option.isSuccess()) {
            return option;
        }
        option = operation.m_adapter.setEasy(operation.m_handle->handle(),
                                             QCURL_CURL_OPTION(CURLOPT_COOKIELIST),
                                             CookieOptionStage::Rollback,
                                             line.constData());
    }
    return option.isSuccess() ? operation.m_adapter.setEasy(operation.m_handle->handle(),
                                                            QCURL_CURL_OPTION(CURLOPT_COOKIELIST),
                                                            CookieOptionStage::Rollback,
                                                            "FLUSH")
                              : option;
}

CookieStoreResult QCCurlShareCookieStore::State::applyLines(Operation &operation,
                                                            const QList<QByteArray> &lines,
                                                            const QList<QByteArray> &previous)
{
    for (const auto &line : lines) {
        const auto option = operation.m_adapter.setEasy(operation.m_handle->handle(),
                                                        QCURL_CURL_OPTION(CURLOPT_COOKIELIST),
                                                        CookieOptionStage::Apply,
                                                        line.constData());
        if (option.isSuccess()) {
            continue;
        }
        const auto rollback = restore(operation, previous);
        if (!rollback.isSuccess()) {
            operation.m_context->m_cookieStorePoisoned = true;
            return optionFailure(CookieStoreStatus::StorePoisoned,
                                 cookiepolicy::kRollbackFailed,
                                 rollback);
        }
        return optionFailure(CookieStoreStatus::RolledBack,
                             cookiepolicy::kApplyFailedRolledBack,
                             option);
    }
    return applied();
}

CookieStoreResult QCCurlShareCookieStore::State::persist(Operation &operation)
{
    const auto option = operation.m_adapter.setEasy(operation.m_handle->handle(),
                                                    QCURL_CURL_OPTION(CURLOPT_COOKIELIST),
                                                    CookieOptionStage::Persist,
                                                    "FLUSH");
    return option.isSuccess() ? applied()
                              : optionFailure(CookieStoreStatus::PersistenceFailed,
                                              cookiepolicy::kPersistenceFailed,
                                              option);
}

} // namespace QCurl::Internal
