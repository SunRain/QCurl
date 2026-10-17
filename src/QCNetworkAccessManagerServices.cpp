#include "QCNetworkAccessManager.h"
#include "QCNetworkAccessManager_p.h"
#include "QCNetworkCache.h"
#include "QCNetworkLogger.h"
#include "QCNetworkMiddleware.h"

#include <utility>

namespace QCurl {

void QCNetworkAccessManager::setCache(QCNetworkCache *cache)
{
    Q_D(QCNetworkAccessManager);
    d->cache = cache;
}

QCNetworkCache *QCNetworkAccessManager::cache() const
{
    Q_D(const QCNetworkAccessManager);
    return d->cache.data();
}

void QCNetworkAccessManager::setLogger(const QCNetworkLoggerHandle &logger)
{
    Q_D(QCNetworkAccessManager);
    d->logger = logger;
}

QCNetworkLoggerHandle QCNetworkAccessManager::logger() const
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
        if (entry->middleware == middleware) {
            return;
        }
    }

    d->middlewares.append(std::make_shared<QCNetworkAccessManagerPrivate::MiddlewareEntry>(
        QCNetworkAccessManagerPrivate::MiddlewareEntry{middleware}));
    middleware->registerManager(this);
}

void QCNetworkAccessManager::removeMiddleware(QCNetworkMiddleware *middleware)
{
    Q_D(QCNetworkAccessManager);
    if (!middleware) {
        return;
    }

    for (qsizetype i = d->middlewares.size() - 1; i >= 0; --i) {
        if (d->middlewares.at(i)->middleware == middleware) {
            d->middlewares.at(i)->middleware = nullptr;
            d->middlewares.removeAt(i);
        }
    }
    middleware->unregisterManager(this);
}

void QCNetworkAccessManager::clearMiddlewares()
{
    Q_D(QCNetworkAccessManager);
    for (const auto &entry : d->middlewares) {
        if (auto *middleware = std::exchange(entry->middleware, nullptr)) {
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
        if (auto *middleware = entry->middleware) {
            result.append(middleware);
        }
    }
    return result;
}

} // namespace QCurl
