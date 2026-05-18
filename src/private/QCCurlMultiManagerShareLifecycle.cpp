#include "QCCurlMultiManager.h"

#include "QCNetworkAccessManager.h"
#include "QCNetworkReply.h"
#include "QCNetworkReply_p.h"

namespace QCurl {

QCCurlMultiManager::ShareConfig QCCurlMultiManager::toShareConfig(
    const QCNetworkAccessManager *manager)
{
    ShareConfig out;
    if (!manager) {
        return out;
    }

    const auto config = manager->shareHandleConfig();
    out.dnsCache      = config.shareDnsCache();
    out.cookies       = config.shareCookies();
    out.sslSession    = config.shareSslSession();
    return out;
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

QCCurlMultiManager::ShareContext *QCCurlMultiManager::prepareShareForReplyLocked(
    QCNetworkReply *reply, CURL *)
{
    auto *accessManager                  = qobject_cast<QCNetworkAccessManager *>(reply->parent());
    const ShareConfig desiredShareConfig = toShareConfig(accessManager);
    if (!accessManager || !desiredShareConfig.enabled()) {
        return nullptr;
    }

    ShareContext *shareContext = getOrCreateShareContextLocked(accessManager);
    if (!shareContext || shareContext->pendingDelete) {
        reply->d_func()->capabilityWarnings.append(
            QStringLiteral("share handle 作用域已销毁，已降级为不共享缓存"));
        return shareContext;
    }

    if (desiredShareConfig == shareContext->applied) {
        return shareContext;
    }

    if (shareContext->activeUsers > 0) {
        shareContext->pending = desiredShareConfig;
        reply->d_func()->capabilityWarnings.append(
            QStringLiteral("share handle 配置变更延迟生效：当前仍按 %1 生效，"
                           "待在途请求结束后切换为 %2")
                .arg(shareConfigSummary(shareContext->applied))
                .arg(shareConfigSummary(desiredShareConfig)));
        return shareContext;
    }

    QString initError;
    if (!applyShareConfigIfIdleLocked(shareContext, desiredShareConfig, &initError)) {
        const QString reason = initError.isEmpty() ? shareContext->lastInitError : initError;
        reply->d_func()->capabilityWarnings.append(
            QStringLiteral("share handle 不可用（%1），已降级为不共享缓存")
                .arg(reason.isEmpty() ? QStringLiteral("unknown") : reason));
    } else if (shareContext->applied != desiredShareConfig) {
        reply->d_func()->capabilityWarnings.append(
            QStringLiteral("share handle 降级：期望 %1，但实际仅启用 %2")
                .arg(shareConfigSummary(desiredShareConfig))
                .arg(shareConfigSummary(shareContext->applied)));
    }
    return shareContext;
}

void QCCurlMultiManager::applyShareToEasyLocked(QCNetworkReply *reply,
                                                CURL *easy,
                                                ShareContext *shareContext)
{
    if (!shareContext || !shareContext->share || !shareContext->applied.enabled()) {
        resetShareOnEasyIfNeeded(easy);
        return;
    }

    const CURLcode rc = curl_easy_setopt(easy, CURLOPT_SHARE, shareContext->share);
    if (rc != CURLE_OK) {
        reply->d_func()->capabilityWarnings.append(
            QStringLiteral("设置 CURLOPT_SHARE 失败（%1），已降级为不共享缓存")
                .arg(QString::fromUtf8(curl_easy_strerror(rc))));
        curl_easy_setopt(easy, CURLOPT_SHARE, nullptr);
        m_easyShareOptionSet.insert(easy, false);
        return;
    }

    shareContext->activeUsers += 1;
    m_easyToShareContext.insert(easy, shareContext);
    m_easyShareOptionSet.insert(easy, true);

    auto *d                 = reply->d_func();
    const bool hasCookieJar = (d->cookieMode != 0) && !d->cookieFilePath.isEmpty();
    if (shareContext->applied.cookies && !hasCookieJar) {
        curl_easy_setopt(easy, CURLOPT_COOKIEFILE, "");
    }
}

void QCCurlMultiManager::resetShareOnEasyIfNeeded(CURL *easy)
{
    auto it = m_easyShareOptionSet.find(easy);
    if (it == m_easyShareOptionSet.end() || !it.value()) {
        return;
    }

    curl_easy_setopt(easy, CURLOPT_SHARE, nullptr);
    it.value() = false;
}

} // namespace QCurl
