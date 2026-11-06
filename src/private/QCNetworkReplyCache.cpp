/**
 * @file
 * @brief 实现请求级缓存上下文、执行策略与候选上限。
 */

#include "QCNetworkCachePolicy.h"
#include "private/QCNetworkCacheIntegration_p.h"
#include "private/QCNetworkReplyCache_p.h"

namespace QCurl::Internal {

ReplyCachePreparation QCReplyCache::prepareRequest(const QCNetworkRequest &request,
                                                   HttpMethod method,
                                                   bool managerUsesCookies,
                                                   QCNetworkCache *cache)
{
    ReplyCachePreparation prepared{request, {}};
    auto &context            = prepared.cache;
    context.m_key            = buildCacheRequestKey(request, method, managerUsesCookies);
    context.m_keyInitialized = true;
    if (cache && request.cachePolicy() == QCNetworkCachePolicy::PreferCache
        && (method == HttpMethod::Get || method == HttpMethod::Head)) {
        context.m_staleEntry = cache->lookup(context.m_key, QCNetworkCacheReadMode::AllowStale);
        if (context.m_staleEntry.status() == QCNetworkCacheLookupStatus::StaleHit
            && cacheMetadataHasValidator(context.m_staleEntry.metadata())) {
            prepared.request = requestWithCacheValidators(request, context.m_staleEntry.metadata());
            context.m_revalidation = true;
        }
    }
    return prepared;
}

QCNetworkCacheRequestKey QCReplyCache::requestKey(const QCNetworkRequest &request,
                                                  HttpMethod method,
                                                  bool managerUsesCookies) const
{
    return m_keyInitialized ? m_key : buildCacheRequestKey(request, method, managerUsesCookies);
}

ReplyCacheExecutionDecision QCReplyCache::lookupForExecution(const QCNetworkRequest &request,
                                                             bool cacheAvailable)
{
    if (!cacheAvailable) {
        return {};
    }
    const bool requiresRevalidation = requestRequiresCacheRevalidation(request);
    switch (request.cachePolicy()) {
        case QCNetworkCachePolicy::OnlyNetwork:
            return {};
        case QCNetworkCachePolicy::OnlyCache:
            return {requiresRevalidation ? std::nullopt
                                         : std::optional(QCNetworkCacheReadMode::AllowStale),
                    true};
        case QCNetworkCachePolicy::PreferCache:
            return {requiresRevalidation ? std::nullopt
                                         : std::optional(QCNetworkCacheReadMode::FreshOnly),
                    false};
        case QCNetworkCachePolicy::AlwaysCache:
            return {requiresRevalidation ? std::nullopt
                                         : std::optional(QCNetworkCacheReadMode::AllowStale),
                    false};
        case QCNetworkCachePolicy::PreferNetwork:
            m_fallback = true;
            return {};
    }
    return {};
}

qint64 QCReplyCache::collectionLimit(const QCNetworkRequest &request,
                                     HttpMethod method,
                                     bool managerUsesCookies,
                                     const QCNetworkCache *cache) const
{
    if (!cache || method != HttpMethod::Get
        || request.cachePolicy() == QCNetworkCachePolicy::OnlyNetwork
        || requestForbidsCacheStorage(request)) {
        return -1;
    }
    const auto key = requestKey(request, method, managerUsesCookies);
    return key.hasAuthenticationContext() && key.cachePartitionKey().isEmpty()
               ? -1
               : qMax<qint64>(0, cache->maxCacheSize());
}

bool QCReplyCache::allowFallback(const ReplyCacheNetworkFacts &facts)
{
    if (!m_fallback || facts.hasHeaders || facts.downloadedBytes != 0 || facts.availableBytes != 0
        || facts.retainedCandidateBytes != 0) {
        return false;
    }
    m_fallback = false;
    return true;
}

} // namespace QCurl::Internal
