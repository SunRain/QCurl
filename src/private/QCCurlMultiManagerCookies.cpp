#include "QCCurlHandleManager.h"
#include "QCCurlMultiManager.h"
#include "QCNetworkAccessManager.h"
#include "private/QCCookieStoreCodec_p.h"
#include "private/QCCurlRequiredOptionAdapter_p.h"

#include <QDebug>
#include <QMutexLocker>

namespace QCurl {

namespace {

using Internal::CookieOptionAdapter;
using Internal::CookieOptionStage;
using Internal::CookieStoreResult;
using Internal::CookieStoreStatus;
using Internal::RequiredOptionResult;

CookieStoreResult failure(CookieStoreStatus status,
                          const QString &policyCode,
                          const QString &message)
{
    CookieStoreResult result;
    result.status     = status;
    result.policyCode = policyCode;
    result.message    = message;
    return result;
}

CookieStoreResult optionFailure(CookieStoreStatus status,
                                const QString &policyCode,
                                const RequiredOptionResult &option)
{
    CookieStoreResult result;
    result.status     = status;
    result.curlCode   = option.curlCode;
    result.shareCode  = option.shareCode;
    result.optionName = option.optionName;
    result.policyCode = policyCode;
    result.message    = option.message;
    return result;
}

CookieStoreResult reported(CookieStoreResult result)
{
    if (!result.isSuccess()) {
        qWarning().noquote() << "QCurl cookie store:" << result.policyCode << result.message;
    }
    return result;
}

CookieStoreResult applied(QList<QCCookie> cookies = {})
{
    CookieStoreResult result;
    result.status     = CookieStoreStatus::Applied;
    result.policyCode = QStringLiteral("cookie.applied");
    result.cookies    = std::move(cookies);
    return result;
}

CookieStoreResult configureCookieHandle(CURL *easy, CURLSH *share, CookieOptionAdapter *adapter)
{
    RequiredOptionResult option = adapter->setEasy(easy,
                                                   QCURL_CURL_OPTION(CURLOPT_SHARE),
                                                   CookieOptionStage::Setup,
                                                   share);
    if (!option.isSuccess()) {
        return optionFailure(CookieStoreStatus::RejectedBeforeMutation,
                             QStringLiteral("cookie.required_option_failed"),
                             option);
    }
    option = adapter->setEasy(easy,
                              QCURL_CURL_OPTION(CURLOPT_COOKIEFILE),
                              CookieOptionStage::Setup,
                              "");
    return option.isSuccess() ? applied()
                              : optionFailure(CookieStoreStatus::RejectedBeforeMutation,
                                              QStringLiteral("cookie.required_option_failed"),
                                              option);
}

CookieStoreResult snapshotCookies(CURL *easy,
                                  CookieOptionAdapter *adapter,
                                  QList<QByteArray> *snapshot)
{
    curl_slist *list                  = nullptr;
    const RequiredOptionResult option = adapter->getCookieList(easy,
                                                               CookieOptionStage::Snapshot,
                                                               &list);
    if (!option.isSuccess()) {
        curl_slist_free_all(list);
        return optionFailure(CookieStoreStatus::RejectedBeforeMutation,
                             QStringLiteral("cookie.snapshot_failed"),
                             option);
    }
    for (const curl_slist *entry = list; entry; entry = entry->next) {
        if (entry->data) {
            snapshot->append(QByteArray(entry->data));
        }
    }
    curl_slist_free_all(list);
    return applied();
}

RequiredOptionResult restoreSnapshot(CURL *easy,
                                     const QList<QByteArray> &snapshot,
                                     CookieOptionAdapter *adapter)
{
    RequiredOptionResult option = adapter->setEasy(easy,
                                                   QCURL_CURL_OPTION(CURLOPT_COOKIELIST),
                                                   CookieOptionStage::Rollback,
                                                   "ALL");
    if (!option.isSuccess()) {
        return option;
    }
    for (const QByteArray &line : snapshot) {
        option = adapter->setEasy(easy,
                                  QCURL_CURL_OPTION(CURLOPT_COOKIELIST),
                                  CookieOptionStage::Rollback,
                                  line.constData());
        if (!option.isSuccess()) {
            return option;
        }
    }
    return adapter->setEasy(easy,
                            QCURL_CURL_OPTION(CURLOPT_COOKIELIST),
                            CookieOptionStage::Rollback,
                            "FLUSH");
}

CookieStoreResult applyCookieLines(CURL *easy,
                                   const QList<QByteArray> &lines,
                                   const QList<QByteArray> &snapshot,
                                   CookieOptionAdapter *adapter,
                                   bool *storePoisoned)
{
    for (const QByteArray &line : lines) {
        const RequiredOptionResult option = adapter->setEasy(easy,
                                                             QCURL_CURL_OPTION(CURLOPT_COOKIELIST),
                                                             CookieOptionStage::Apply,
                                                             line.constData());
        if (option.isSuccess()) {
            continue;
        }
        const RequiredOptionResult rollback = restoreSnapshot(easy, snapshot, adapter);
        if (!rollback.isSuccess()) {
            *storePoisoned = true;
            return optionFailure(CookieStoreStatus::StorePoisoned,
                                 QStringLiteral("cookie.rollback_failed"),
                                 rollback);
        }
        return optionFailure(CookieStoreStatus::RolledBack,
                             QStringLiteral("cookie.apply_failed_rolled_back"),
                             option);
    }
    return applied();
}

CookieStoreResult persistCookies(CURL *easy, CookieOptionAdapter *adapter)
{
    const RequiredOptionResult option = adapter->setEasy(easy,
                                                         QCURL_CURL_OPTION(CURLOPT_COOKIELIST),
                                                         CookieOptionStage::Persist,
                                                         "FLUSH");
    return option.isSuccess() ? applied()
                              : optionFailure(CookieStoreStatus::PersistenceFailed,
                                              QStringLiteral("cookie.persistence_failed"),
                                              option);
}

} // namespace

Internal::CookieStoreResult QCCurlMultiManager::importCookiesForManager(
    const QCNetworkAccessManager *manager, const QList<QCCookie> &cookies, const QUrl &originUrl)
{
    if (!manager) {
        return reported(failure(CookieStoreStatus::RejectedBeforeMutation,
                                QStringLiteral("cookie.invalid_manager"),
                                QStringLiteral("manager 为空")));
    }
    const ShareConfig desired = toShareConfig(manager);
    if (!desired.cookies) {
        return reported(failure(CookieStoreStatus::RejectedBeforeMutation,
                                QStringLiteral("cookie.share_disabled"),
                                QStringLiteral("cookie share 未启用")));
    }

    QList<QByteArray> lines;
    CookieStoreResult result = Internal::prepareCookieImport(cookies, originUrl, &lines);
    if (!result.isSuccess()) {
        return reported(std::move(result));
    }

    QMutexLocker locker(&m_mutex);
    ShareContext *context = prepareCookieContextLocked(manager, desired, &result);
    if (!context) {
        return reported(std::move(result));
    }
    QCCurlHandleManager handle;
    CURL *easy = handle.handle();
    if (!easy) {
        return reported(failure(CookieStoreStatus::RejectedBeforeMutation,
                                QStringLiteral("cookie.handle_init_failed"),
                                QStringLiteral("curl easy handle 初始化失败")));
    }

    CookieOptionAdapter adapter;
    result = configureCookieHandle(easy, context->share, &adapter);
    if (!result.isSuccess()) {
        return reported(std::move(result));
    }
    QList<QByteArray> snapshot;
    result = snapshotCookies(easy, &adapter, &snapshot);
    if (!result.isSuccess()) {
        return reported(std::move(result));
    }
    result = applyCookieLines(easy, lines, snapshot, &adapter, &context->cookieStorePoisoned);
    if (!result.isSuccess()) {
        return reported(std::move(result));
    }
    return reported(persistCookies(easy, &adapter));
}

Internal::CookieStoreResult QCCurlMultiManager::clearAllCookiesForManager(
    const QCNetworkAccessManager *manager)
{
    if (!manager) {
        return reported(failure(CookieStoreStatus::RejectedBeforeMutation,
                                QStringLiteral("cookie.invalid_manager"),
                                QStringLiteral("manager 为空")));
    }
    const ShareConfig desired = toShareConfig(manager);
    if (!desired.cookies) {
        return reported(failure(CookieStoreStatus::RejectedBeforeMutation,
                                QStringLiteral("cookie.share_disabled"),
                                QStringLiteral("cookie share 未启用")));
    }

    QMutexLocker locker(&m_mutex);
    CookieStoreResult result;
    ShareContext *context = prepareCookieContextLocked(manager, desired, &result);
    if (!context) {
        return reported(std::move(result));
    }
    QCCurlHandleManager handle;
    CURL *easy = handle.handle();
    if (!easy) {
        return reported(failure(CookieStoreStatus::RejectedBeforeMutation,
                                QStringLiteral("cookie.handle_init_failed"),
                                QStringLiteral("curl easy handle 初始化失败")));
    }

    CookieOptionAdapter adapter;
    result = configureCookieHandle(easy, context->share, &adapter);
    if (!result.isSuccess()) {
        return reported(std::move(result));
    }
    const RequiredOptionResult clear = adapter.setEasy(easy,
                                                       QCURL_CURL_OPTION(CURLOPT_COOKIELIST),
                                                       CookieOptionStage::Clear,
                                                       "ALL");
    if (!clear.isSuccess()) {
        return reported(optionFailure(CookieStoreStatus::RejectedBeforeMutation,
                                      QStringLiteral("cookie.clear_failed"),
                                      clear));
    }
    return reported(persistCookies(easy, &adapter));
}

} // namespace QCurl
