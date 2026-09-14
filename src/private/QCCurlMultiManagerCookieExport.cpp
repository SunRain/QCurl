#include "QCCurlHandleManager.h"
#include "QCCurlMultiManager.h"
#include "QCNetworkAccessManager.h"
#include "private/QCCookieStoreCodec_p.h"
#include "private/QCCurlRequiredOptionAdapter_p.h"

#include <QDebug>
#include <QMutexLocker>

namespace QCurl {

namespace {

Internal::CookieStoreResult exportFailure(const QString &policyCode, const QString &message)
{
    Internal::CookieStoreResult result;
    result.status     = Internal::CookieStoreStatus::RejectedBeforeMutation;
    result.policyCode = policyCode;
    result.message    = message;
    return result;
}

Internal::CookieStoreResult exportOptionFailure(const QString &policyCode,
                                                const Internal::RequiredOptionResult &option)
{
    Internal::CookieStoreResult result = exportFailure(policyCode, option.message);
    result.curlCode                    = option.curlCode;
    result.shareCode                   = option.shareCode;
    result.optionName                  = option.optionName;
    return result;
}

Internal::CookieStoreResult reported(Internal::CookieStoreResult result)
{
    if (!result.isSuccess()) {
        qWarning().noquote() << "QCurl cookie store:" << result.policyCode << result.message;
    }
    return result;
}

Internal::RequiredOptionResult configureExportHandle(CURL *easy,
                                                     CURLSH *share,
                                                     Internal::CookieOptionAdapter *adapter)
{
    Internal::RequiredOptionResult option = adapter->setEasy(easy,
                                                             QCURL_CURL_OPTION(CURLOPT_SHARE),
                                                             Internal::CookieOptionStage::Setup,
                                                             share);
    if (option.isSuccess()) {
        option = adapter->setEasy(easy,
                                  QCURL_CURL_OPTION(CURLOPT_COOKIEFILE),
                                  Internal::CookieOptionStage::Setup,
                                  "");
    }
    return option;
}

Internal::CookieStoreResult readExportedCookies(CURL *easy,
                                                const QUrl &filterUrl,
                                                Internal::CookieOptionAdapter *adapter)
{
    curl_slist *list = nullptr;
    const Internal::RequiredOptionResult option
        = adapter->getCookieList(easy, Internal::CookieOptionStage::Export, &list);
    if (!option.isSuccess()) {
        curl_slist_free_all(list);
        return exportOptionFailure(QStringLiteral("cookie.export_failed"), option);
    }

    QList<QCCookie> cookies;
    for (const curl_slist *entry = list; entry; entry = entry->next) {
        if (!entry->data) {
            continue;
        }
        const auto cookie = Internal::parseCurlCookieLine(QByteArray(entry->data));
        if (!cookie.has_value()) {
            curl_slist_free_all(list);
            return exportFailure(QStringLiteral("cookie.export_parse_failed"),
                                 QStringLiteral("cookie store 包含无法解析的记录"));
        }
        if (Internal::cookieMatchesUrl(*cookie, filterUrl)) {
            cookies.append(*cookie);
        }
    }
    curl_slist_free_all(list);
    Internal::CookieStoreResult result;
    result.status     = Internal::CookieStoreStatus::Applied;
    result.policyCode = QStringLiteral("cookie.applied");
    result.cookies    = std::move(cookies);
    return result;
}

} // namespace

Internal::CookieStoreResult QCCurlMultiManager::exportCookiesForManager(
    const QCNetworkAccessManager *manager, const QUrl &filterUrl)
{
    if (!manager) {
        return reported(exportFailure(QStringLiteral("cookie.invalid_manager"),
                                      QStringLiteral("manager 为空")));
    }
    const ShareConfig desired = toShareConfig(manager);
    if (!desired.cookies) {
        return reported(exportFailure(QStringLiteral("cookie.share_disabled"),
                                      QStringLiteral("cookie share 未启用")));
    }

    QMutexLocker locker(&m_mutex);
    Internal::CookieStoreResult result;
    ShareContext *context = prepareCookieContextLocked(manager, desired, &result);
    if (!context) {
        return reported(std::move(result));
    }
    QCCurlHandleManager handle;
    CURL *easy = handle.handle();
    if (!easy) {
        return reported(exportFailure(QStringLiteral("cookie.handle_init_failed"),
                                      QStringLiteral("curl easy handle 初始化失败")));
    }

    Internal::CookieOptionAdapter adapter;
    const Internal::RequiredOptionResult option = configureExportHandle(easy,
                                                                        context->share,
                                                                        &adapter);
    if (!option.isSuccess()) {
        return reported(
            exportOptionFailure(QStringLiteral("cookie.required_option_failed"), option));
    }
    return reported(readExportedCookies(easy, filterUrl, &adapter));
}

} // namespace QCurl
