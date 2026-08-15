#include "QCNetworkAccessManager.h"
#include "QCNetworkCache.h"
#include "QCNetworkCachePolicy.h"
#include "QCNetworkReply.h"
#include "QCNetworkReply_p.h"
#include "private/QCNetworkCacheIntegration_p.h"
#include "private/QCNetworkReplyCache_p.h"
#include "private/QCNetworkReplyExecution_p.h"

#include <QPointer>
#include <QTimer>

namespace QCurl::Internal {
namespace {

[[nodiscard]] bool managerUsesCookies(const QCNetworkAccessManager *manager)
{
    return manager
           && (manager->shareHandleConfig().shareCookies() || !manager->cookieFilePath().isEmpty());
}

[[nodiscard]] QCNetworkCacheRequestKey replyCacheKey(const QCNetworkReplyPrivate *reply,
                                                     const QCNetworkAccessManager *manager)
{
    return reply->cacheRequestKeyInitialized ? reply->cacheRequestKey
                                             : buildCacheRequestKey(reply->request,
                                                                    reply->httpMethod,
                                                                    managerUsesCookies(manager));
}

[[nodiscard]] QByteArray responseHeaderBlock(
    int statusCode, const QList<QCNetworkCacheMetadata::RawHeaderPair> &headers)
{
    QByteArray block = QByteArrayLiteral("HTTP/1.1 ") + QByteArray::number(statusCode)
                       + QByteArrayLiteral(" Revalidated\r\n");
    for (const auto &[name, value] : headers) {
        block += name + QByteArrayLiteral(": ") + value + QByteArrayLiteral("\r\n");
    }
    return block + QByteArrayLiteral("\r\n");
}

[[nodiscard]] bool cachedHeaderMatchesIfPresent(const QMap<QByteArray, QByteArray> &cached,
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

[[nodiscard]] bool headMetadataMatchesCachedEntity(const QMap<QByteArray, QByteArray> &cachedHeaders,
                                                   const QMap<QByteArray, QByteArray> &headHeaders)
{
    return cachedHeaderMatchesIfPresent(cachedHeaders, headHeaders, QByteArrayLiteral("etag"))
           && cachedHeaderMatchesIfPresent(cachedHeaders,
                                           headHeaders,
                                           QByteArrayLiteral("last-modified"))
           && cachedHeaderMatchesIfPresent(cachedHeaders,
                                           headHeaders,
                                           QByteArrayLiteral("content-length"));
}

void storeGetResponse(QCNetworkReplyPrivate *reply,
                      QCNetworkCache *cache,
                      const QCNetworkCacheRequestKey &key)
{
    if (reply->httpStatusCode != 200 || !responseHeadersAreCacheable(reply->finalHeaderList)
        || (key.hasAuthenticationContext() && key.cachePartitionKey().isEmpty())) {
        return;
    }

    QCNetworkCacheMetadata metadata = buildCacheMetadata(key,
                                                         reply->httpStatusCode,
                                                         reply->finalHeaderList,
                                                         reply->durationMs);
    cache->insert(key, reply->cacheBodyBuffer, metadata);
}

void updateGetMetadataFromHead(QCNetworkReplyPrivate *reply,
                               QCNetworkCache *cache,
                               const QCNetworkCacheRequestKey &key,
                               const QMap<QByteArray, QByteArray> &headers)
{
    if (reply->httpStatusCode != 200 || !responseHeadersAreCacheable(reply->finalHeaderList)) {
        return;
    }

    QCNetworkCacheLookupResult existing = reply->staleCacheEntry;
    if (!existing.hit()) {
        existing = cache->lookup(key, QCNetworkCacheReadMode::AllowStale);
    }
    if (!existing.hit()) {
        return;
    }

    const auto existingHeaders = existing.metadata().headers();
    if (!headMetadataMatchesCachedEntity(existingHeaders, headers)) {
        return;
    }

    const auto mergedRawHeaders     = mergeRevalidatedRawHeaders(existing.metadata().rawHeaders(),
                                                                 reply->finalHeaderList);
    QCNetworkCacheMetadata metadata = buildCacheMetadata(key,
                                                         200,
                                                         mergedRawHeaders,
                                                         reply->durationMs);
    cache->insert(key, existing.body(), metadata);
}

void scheduleOnlyCacheMiss(QCNetworkReply *reply, QCNetworkReplyPrivate *replyPrivate)
{
    QPointer<QCNetworkReply> safeReply(reply);
    replyPrivate->setError(NetworkError::InvalidRequest,
                           QStringLiteral("Cache miss with OnlyCache policy"));
    QTimer::singleShot(0, reply, [safeReply, replyPrivate]() {
        if (safeReply) {
            Q_UNUSED(replyPrivate->setState(ReplyState::Error));
        }
    });
}

} // namespace

bool QCNetworkReplyExecution::completeFromCache(QCNetworkReply *reply,
                                                QCNetworkAccessManager *manager)
{
    auto *d               = reply->d_func();
    QCNetworkCache *cache = manager ? manager->cache() : nullptr;
    if (!cache) {
        return false;
    }

    const bool requiresRevalidation = requestRequiresCacheRevalidation(d->request);
    switch (d->request.cachePolicy()) {
        case QCNetworkCachePolicy::OnlyNetwork:
            return false;
        case QCNetworkCachePolicy::OnlyCache:
            if (requiresRevalidation || !reply->loadFromCache(true)) {
                scheduleOnlyCacheMiss(reply, d);
            }
            return true;
        case QCNetworkCachePolicy::PreferCache:
            return !requiresRevalidation && reply->loadFromCache(false);
        case QCNetworkCachePolicy::AlwaysCache:
            return !requiresRevalidation && reply->loadFromCache(true);
        case QCNetworkCachePolicy::PreferNetwork:
            d->fallbackToCache = true;
            return false;
    }
    return false;
}

bool QCNetworkReplyExecution::tryPreferNetworkCacheFallback(QCNetworkReply *reply)
{
    if (!reply) {
        return false;
    }

    auto *d = reply->d_func();
    if (!d->fallbackToCache || !d->headerData.isEmpty() || d->bytesDownloaded != 0
        || !d->bodyBuffer.isEmpty() || !d->cacheBodyBuffer.isEmpty()) {
        return false;
    }

    d->fallbackToCache = false;
    d->errorCode       = NetworkError::NoError;
    d->errorMessage.clear();
    return reply->loadFromCache(true);
}

SignalEmissionResult restoreRevalidatedCacheResponse(QCNetworkReplyPrivate *reply)
{
    if (!reply || !reply->cacheRevalidation || reply->httpStatusCode != 304
        || !reply->staleCacheEntry.hit()) {
        return SignalEmissionResult::Alive;
    }

    const QPointer<QCNetworkReply> observer(reply->qObject());
    if (!observer) {
        return SignalEmissionResult::Destroyed;
    }

    const QCNetworkCacheMetadata cachedMetadata = reply->staleCacheEntry.metadata();
    const auto mergedRawHeaders     = mergeRevalidatedRawHeaders(cachedMetadata.rawHeaders(),
                                                                 reply->finalHeaderList);
    QCNetworkCacheMetadata metadata = buildCacheMetadata(reply->cacheRequestKey,
                                                         cachedMetadata.statusCode(),
                                                         mergedRawHeaders,
                                                         reply->durationMs);
    reply->staleCacheEntry.setMetadata(metadata);
    reply->headerData     = responseHeaderBlock(metadata.statusCode(), mergedRawHeaders);
    reply->httpStatusCode = metadata.statusCode();
    reply->parseHeaders();

    if (reply->httpMethod == HttpMethod::Get) {
        reply->bodyBuffer.clear();
        reply->bodyBuffer.append(reply->staleCacheEntry.body());
        reply->cacheBodyBuffer = reply->staleCacheEntry.body();
        reply->bytesDownloaded = reply->staleCacheEntry.body().size();
        if (!reply->staleCacheEntry.body().isEmpty()) {
            return emitReplySignal(observer, [](QCNetworkReply *q) { Q_EMIT q->readyRead(); });
        }
    }
    return SignalEmissionResult::Alive;
}

void storeReplyInCache(QCNetworkReplyPrivate *reply)
{
    if (!reply || reply->errorCode != NetworkError::NoError) {
        return;
    }

    QCNetworkReply *q     = reply->qObject();
    auto *manager         = q ? qobject_cast<QCNetworkAccessManager *>(q->parent()) : nullptr;
    QCNetworkCache *cache = manager ? manager->cache() : nullptr;
    if (!cache || reply->request.cachePolicy() == QCNetworkCachePolicy::OnlyNetwork
        || requestForbidsCacheStorage(reply->request)) {
        return;
    }

    const auto key     = replyCacheKey(reply, manager);
    const auto headers = reply->parsedResponseHeaders();
    if (reply->httpMethod == HttpMethod::Get) {
        storeGetResponse(reply, cache, key);
    } else if (reply->httpMethod == HttpMethod::Head) {
        updateGetMetadataFromHead(reply, cache, key, headers);
    }
}

} // namespace QCurl::Internal
