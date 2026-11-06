/**
 * @file
 * @brief 实现完整 GET 候选存储和 HEAD 元数据更新。
 */

#include "QCNetworkCachePolicy.h"
#include "QCNetworkHttpHeaders.h"
#include "private/QCNetworkCacheIntegration_p.h"
#include "private/QCNetworkReplyCache_p.h"

namespace QCurl::Internal {
namespace {

bool cachedHeaderMatchesIfPresent(const QMap<QByteArray, QByteArray> &cached,
                                  const QMap<QByteArray, QByteArray> &current,
                                  const QByteArray &name)
{
    const auto headerValue = [&name](const QMap<QByteArray, QByteArray> &headers) {
        for (auto it = headers.cbegin(); it != headers.cend(); ++it) {
            if (QByteArrayView(it.key()).compare(name, Qt::CaseInsensitive) == 0) {
                return it.value().trimmed();
            }
        }
        return QByteArray();
    };
    const QByteArray currentValue = headerValue(current);
    if (currentValue.isEmpty()) {
        return true;
    }
    const QByteArray cachedValue = headerValue(cached);
    return !cachedValue.isEmpty() && cachedValue == currentValue;
}

bool headMetadataMatchesCachedEntity(const QMap<QByteArray, QByteArray> &cached,
                                     const QMap<QByteArray, QByteArray> &head)
{
    return cachedHeaderMatchesIfPresent(cached, head, httpheaders::kETag.toLower())
           && cachedHeaderMatchesIfPresent(cached, head, httpheaders::kLastModified.toLower())
           && cachedHeaderMatchesIfPresent(cached, head, httpheaders::kContentLength.toLower());
}

} // namespace

void QCReplyCache::storeGet(QCNetworkCache *cache,
                            const QCNetworkCacheRequestKey &key,
                            const ReplyCacheStoreFacts &facts) const
{
    if (!facts.candidateComplete || facts.status != 200
        || facts.candidate.size() > cache->maxCacheSize()
        || !responseHeadersAreCacheable(facts.rawHeaders)
        || (key.hasAuthenticationContext() && key.cachePartitionKey().isEmpty())) {
        return;
    }
    const auto metadata = buildCacheMetadata(key, facts.status, facts.rawHeaders, facts.durationMs);
    cache->insert(key, facts.candidate, metadata);
}

void QCReplyCache::updateGetFromHead(QCNetworkCache *cache,
                                     const QCNetworkCacheRequestKey &key,
                                     const ReplyCacheStoreFacts &facts) const
{
    if (facts.status != 200 || !responseHeadersAreCacheable(facts.rawHeaders)) {
        return;
    }
    auto existing = m_staleEntry;
    if (!existing.hit()) {
        existing = cache->lookup(key, QCNetworkCacheReadMode::AllowStale);
    }
    if (!existing.hit()
        || !headMetadataMatchesCachedEntity(existing.metadata().headers(), facts.parsedHeaders)) {
        return;
    }
    const auto merged   = mergeRevalidatedRawHeaders(existing.metadata().rawHeaders(),
                                                     facts.rawHeaders);
    const auto metadata = buildCacheMetadata(key, 200, merged, facts.durationMs);
    cache->insert(key, existing.body(), metadata);
}

void QCReplyCache::store(const QCNetworkRequest &request,
                         HttpMethod method,
                         bool managerUsesCookies,
                         QCNetworkCache *cache,
                         const ReplyCacheStoreFacts &facts) const
{
    if (!cache || facts.error != NetworkError::NoError
        || request.cachePolicy() == QCNetworkCachePolicy::OnlyNetwork
        || requestForbidsCacheStorage(request)) {
        return;
    }
    const auto key = requestKey(request, method, managerUsesCookies);
    if (method == HttpMethod::Get) {
        storeGet(cache, key, facts);
    } else if (method == HttpMethod::Head) {
        updateGetFromHead(cache, key, facts);
    }
}

} // namespace QCurl::Internal
