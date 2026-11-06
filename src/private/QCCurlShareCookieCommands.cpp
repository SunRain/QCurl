#include "private/QCCookiePolicyCode_p.h"
#include "private/QCCookieStoreCodec_p.h"
#include "private/QCCurlShareCookieStoreState_p.h"

namespace QCurl::Internal {

CookieCommandResult QCCurlShareCookieStore::State::importCookies(
    const QCNetworkAccessManager *manager,
    const Config &config,
    const QList<QCCookie> &cookies,
    const QUrl &originUrl)
{
    CookieCommandResult command;
    if (!manager || !config.shareCookies()) {
        static_cast<void>(prepareOperation(manager, config, command));
        return command;
    }
    QList<QByteArray> lines;
    command.store = prepareCookieImport(cookies, originUrl, &lines);
    if (!command.store.isSuccess()) {
        return command;
    }
    auto operation = prepareOperation(manager, config, command);
    if (!operation) {
        return command;
    }
    auto result = setupOperation(*operation);
    QList<QByteArray> previous;
    if (result.isSuccess()) {
        result = snapshot(*operation, previous);
    }
    if (result.isSuccess()) {
        result = applyLines(*operation, lines, previous);
    }
    if (result.isSuccess()) {
        result = persist(*operation);
    }
    return finishOperation(std::move(operation), std::move(result));
}

CookieCommandResult QCCurlShareCookieStore::State::clearAllCookies(
    const QCNetworkAccessManager *manager, const Config &config)
{
    CookieCommandResult command;
    auto operation = prepareOperation(manager, config, command);
    if (!operation) {
        return command;
    }
    auto result = setupOperation(*operation);
    if (result.isSuccess()) {
        const auto option = operation->m_adapter.setEasy(operation->m_handle->handle(),
                                                         QCURL_CURL_OPTION(CURLOPT_COOKIELIST),
                                                         CookieOptionStage::Clear,
                                                         "ALL");
        result = option.isSuccess() ? persist(*operation)
                                    : optionFailure(CookieStoreStatus::RejectedBeforeMutation,
                                                    cookiepolicy::kClearFailed,
                                                    option);
    }
    return finishOperation(std::move(operation), std::move(result));
}

CookieStoreResult QCCurlShareCookieStore::State::readCookies(Operation &operation,
                                                             const QUrl &filterUrl)
{
    curl_slist *list  = nullptr;
    const auto option = operation.m_adapter.getCookieList(operation.m_handle->handle(),
                                                          CookieOptionStage::Export,
                                                          &list);
    if (!option.isSuccess()) {
        curl_slist_free_all(list);
        return optionFailure(CookieStoreStatus::RejectedBeforeMutation,
                             cookiepolicy::kExportFailed,
                             option);
    }
    QList<QCCookie> cookies;
    for (const auto *entry = list; entry; entry = entry->next) {
        if (!entry->data) {
            continue;
        }
        const auto cookie = parseCurlCookieLine(QByteArray(entry->data));
        if (!cookie) {
            curl_slist_free_all(list);
            return failure(CookieStoreStatus::RejectedBeforeMutation,
                           cookiepolicy::kExportParseFailed,
                           QStringLiteral("cookie store 包含无法解析的记录"));
        }
        if (cookieMatchesUrl(*cookie, filterUrl)) {
            cookies.append(*cookie);
        }
    }
    curl_slist_free_all(list);
    return applied(std::move(cookies));
}

CookieCommandResult QCCurlShareCookieStore::State::exportCookies(
    const QCNetworkAccessManager *manager, const Config &config, const QUrl &filterUrl)
{
    CookieCommandResult command;
    auto operation = prepareOperation(manager, config, command);
    if (!operation) {
        return command;
    }
    auto result = setupOperation(*operation);
    if (result.isSuccess()) {
        result = readCookies(*operation, filterUrl);
    }
    return finishOperation(std::move(operation), std::move(result));
}

} // namespace QCurl::Internal
