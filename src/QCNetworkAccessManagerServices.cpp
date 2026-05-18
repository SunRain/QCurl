#include "QCNetworkAccessManager.h"

#include "QCNetworkAccessManager_p.h"
#include "QCNetworkCache.h"
#include "QCNetworkLogger.h"
#include "QCNetworkMiddleware.h"

namespace QCurl {
namespace {

QList<QCNetworkMiddleware *> sanitizeMiddlewares(const QList<QCNetworkMiddleware *> &middlewares)
{
    QList<QCNetworkMiddleware *> alive;
    alive.reserve(middlewares.size());
    for (auto *middleware : middlewares) {
        if (middleware) {
            alive.append(middleware);
        }
    }
    return alive;
}

QCNetworkMiddleware *middlewareFromEntry(const QCNetworkAccessManagerPrivate::MiddlewareEntry &entry)
{
    return entry.middleware;
}

bool middlewareEntryMatches(const QCNetworkAccessManagerPrivate::MiddlewareEntry &entry,
                            QCNetworkMiddleware *middleware)
{
    return entry.middleware == middleware;
}

} // namespace

void QCNetworkAccessManager::setCache(QCNetworkCache *cache)
{
    Q_D(QCNetworkAccessManager);
    d->cache = cache;
}

QCNetworkCache *QCNetworkAccessManager::cache() const
{
    Q_D(const QCNetworkAccessManager);
    return d->cache;
}

void QCNetworkAccessManager::setLogger(QCNetworkLogger *logger)
{
    Q_D(QCNetworkAccessManager);
    d->logger = logger;
}

QCNetworkLogger *QCNetworkAccessManager::logger() const
{
    Q_D(const QCNetworkAccessManager);
    return d->logger;
}

void QCNetworkAccessManager::setDebugTraceEnabled(bool enabled)
{
    Q_D(QCNetworkAccessManager);
    d->debugTraceEnabled = enabled;
}

bool QCNetworkAccessManager::debugTraceEnabled() const noexcept
{
    Q_D(const QCNetworkAccessManager);
    return d->debugTraceEnabled;
}

void QCNetworkAccessManager::addMiddleware(QCNetworkMiddleware *middleware)
{
    Q_D(QCNetworkAccessManager);
    if (!middleware) {
        return;
    }

    for (const auto &entry : d->middlewares) {
        if (middlewareEntryMatches(entry, middleware)) {
            return;
        }
    }

    d->middlewares.append(QCNetworkAccessManagerPrivate::MiddlewareEntry{middleware});
    middleware->registerManager(this);
}

void QCNetworkAccessManager::removeMiddleware(QCNetworkMiddleware *middleware)
{
    Q_D(QCNetworkAccessManager);
    if (!middleware) {
        return;
    }

    for (qsizetype i = d->middlewares.size() - 1; i >= 0; --i) {
        if (middlewareEntryMatches(d->middlewares.at(i), middleware)) {
            d->middlewares.removeAt(i);
        }
    }
    middleware->unregisterManager(this);
}

void QCNetworkAccessManager::clearMiddlewares()
{
    Q_D(QCNetworkAccessManager);
    for (const auto &entry : d->middlewares) {
        if (auto *middleware = middlewareFromEntry(entry)) {
            middleware->unregisterManager(this);
        }
    }
    d->middlewares.clear();
}

QList<QCNetworkMiddleware *> QCNetworkAccessManager::middlewares() const
{
    Q_D(const QCNetworkAccessManager);
    QList<QCNetworkMiddleware *> result;
    result.reserve(d->middlewares.size());
    for (const auto &entry : d->middlewares) {
        if (auto *middleware = middlewareFromEntry(entry)) {
            result.append(middleware);
        }
    }
    return sanitizeMiddlewares(result);
}

} // namespace QCurl
