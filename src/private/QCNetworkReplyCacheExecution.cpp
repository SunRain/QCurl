/**
 * @file
 * @brief 在缓存检查点应用响应并安全排队发布，不解释缓存策略。
 */

#include "QCNetworkAccessManager.h"
#include "QCNetworkReply.h"
#include "QCNetworkReply_p.h"
#include "private/QCNetworkReplyCache_p.h"
#include "private/QCNetworkReplyExecution_p.h"

#include <QPointer>
#include <QTimer>

namespace QCurl {
namespace {

bool managerUsesCookies(const QCNetworkAccessManager *manager)
{
    return manager
           && (manager->shareHandleConfig().shareCookies() || !manager->cookieFilePath().isEmpty());
}

void scheduleOnlyCacheMiss(QCNetworkReply *reply)
{
    const QPointer<QCNetworkReply> observer(reply);
    QTimer::singleShot(0, reply, [observer]() {
        if (observer) {
            observer->abortWithError(NetworkError::InvalidRequest,
                                     QStringLiteral("Cache miss with OnlyCache policy"));
        }
    });
}

} // namespace

void Internal::QCNetworkReplyExecution::prepareCacheCollection(QCNetworkReply *reply,
                                                               QCNetworkAccessManager *manager)
{
    auto *d          = reply->d_func();
    const auto limit = d->cache.collectionLimit(d->request,
                                                d->httpMethod,
                                                managerUsesCookies(manager),
                                                manager ? manager->cache() : nullptr);
    d->transferState->response.beginCacheCollection(limit);
}

bool Internal::QCNetworkReplyExecution::completeFromCache(QCNetworkReply *reply,
                                                          QCNetworkAccessManager *manager)
{
    auto *d             = reply->d_func();
    const auto decision = d->cache.lookupForExecution(d->request, manager && manager->cache());
    if (decision.readMode
        && reply->loadFromCache(*decision.readMode == QCNetworkCacheReadMode::AllowStale)) {
        return true;
    }
    if (decision.requireHit) {
        d->setError(NetworkError::InvalidRequest,
                    QStringLiteral("Cache miss with OnlyCache policy"));
        scheduleOnlyCacheMiss(reply);
        return true;
    }
    return false;
}

bool Internal::QCNetworkReplyExecution::tryPreferNetworkCacheFallback(QCNetworkReply *reply)
{
    if (!reply) {
        return false;
    }
    auto *d              = reply->d_func();
    const auto &response = d->transferState->response;
    const ReplyCacheNetworkFacts facts{!response.rawHeaderData().isEmpty(),
                                       d->transferState->lifecycle.bytesDownloaded(),
                                       response.bytesAvailable(),
                                       response.retainedCacheBodyBytes()};
    if (!d->cache.allowFallback(facts)) {
        return false;
    }
    d->transferState->lifecycle.recordError(NetworkError::NoError, {});
    return reply->loadFromCache(true);
}

Internal::SignalEmissionResult Internal::QCNetworkReplyExecution::restoreRevalidatedCacheResponse(
    QCNetworkReply *reply)
{
    auto *d           = reply->d_func();
    const auto holder = d->transferState;
    const auto replay = d->cache.revalidate(d->httpMethod,
                                            holder->response.status(),
                                            holder->response.rawHeaders(),
                                            holder->lifecycle.durationMs());
    if (!replay) {
        return SignalEmissionResult::Alive;
    }
    holder->response.applyResponseHeaders(replay->status, replay->rawHeaders);
    if (replay->body) {
        holder->response.applyCachedBody(*replay->body, true);
        holder->lifecycle.observeReplayedBody(replay->body->size());
        if (!replay->body->isEmpty()) {
            return emitReplySignal(QPointer<QCNetworkReply>(reply),
                                   [](QCNetworkReply *observer) { Q_EMIT observer->readyRead(); });
        }
    }
    return SignalEmissionResult::Alive;
}

void Internal::QCNetworkReplyExecution::storeCacheResponse(QCNetworkReply *reply)
{
    auto *d              = reply->d_func();
    auto *manager        = qobject_cast<QCNetworkAccessManager *>(reply->parent());
    const auto holder    = d->transferState;
    const auto &response = holder->response;
    const ReplyCacheStoreFacts facts{holder->lifecycle.error(),
                                     response.status(),
                                     response.rawHeaders(),
                                     response.parsedResponseHeaders(),
                                     response.cacheCandidateComplete(),
                                     response.cacheCandidate(),
                                     holder->lifecycle.durationMs()};
    d->cache.store(d->request,
                   d->httpMethod,
                   managerUsesCookies(manager),
                   manager ? manager->cache() : nullptr,
                   facts);
}

bool QCNetworkReply::loadFromCache(bool ignoreExpiry)
{
    Q_D(QCNetworkReply);
    auto *manager     = qobject_cast<QCNetworkAccessManager *>(parent());
    const auto replay = d->cache.lookup(d->request,
                                        d->httpMethod,
                                        managerUsesCookies(manager),
                                        manager ? manager->cache() : nullptr,
                                        ignoreExpiry ? QCNetworkCacheReadMode::AllowStale
                                                     : QCNetworkCacheReadMode::FreshOnly);
    if (!replay) {
        return false;
    }
    d->transferState->lifecycle.acceptCachedResult();
    d->transferState->response.applyResponseHeaders(replay->status, replay->rawHeaders);
    d->transferState->response.applyCachedBody(*replay->body, false);

    const QPointer<QCNetworkReply> observer(this);
    const bool hasBody = !replay->body->isEmpty();
    QTimer::singleShot(0, this, [observer, hasBody]() {
        if (!observer) {
            return;
        }
        auto *d = observer->d_func();
        if (d->transferState->lifecycle.isTerminal()) {
            return;
        }
        if (hasBody && Internal::emitReplySignal(observer, [](QCNetworkReply *reply) {
                           Q_EMIT reply->readyRead();
                       }) == Internal::SignalEmissionResult::Destroyed) {
            return;
        }
        Q_UNUSED(d->setState(ReplyState::Finished));
    });
    return true;
}

} // namespace QCurl
