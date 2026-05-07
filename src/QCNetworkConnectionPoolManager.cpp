// SPDX-License-Identifier: MIT
// Copyright (c) 2025 QCurl Project

#include "QCNetworkConnectionPoolManager.h"
#include "QCNetworkConnectionPoolManager_p.h"

#include "QCCurlMultiManager.h"

#include <curl/curl.h>

#include <QDebug>
#include <QHash>
#include <QMutex>
#include <QMutexLocker>

namespace QCurl {

/// 管理器内部状态；配置和统计计数必须通过 mutex 访问。
class QCNetworkConnectionPoolManagerPrivate
{
public:
    mutable QMutex mutex;
    QCNetworkConnectionPoolConfig config;
    qint64 totalRequests = 0;
    qint64 reusedConnections = 0;
    QHash<QString, int> activeConnectionsPerHost;
};

QCNetworkConnectionPoolManager *QCNetworkConnectionPoolManager::instance()
{
    static QCNetworkConnectionPoolManager instance;
    return &instance;
}

QCNetworkConnectionPoolManager::QCNetworkConnectionPoolManager()
    : d_ptr(new QCNetworkConnectionPoolManagerPrivate)
{
    qDebug() << "QCNetworkConnectionPoolManager: Initialized with default config";
    qDebug() << "  - maxConnectionsPerHost:" << d_ptr->config.maxConnectionsPerHost();
    qDebug() << "  - maxTotalConnections:" << d_ptr->config.maxTotalConnections();
    qDebug() << "  - HTTP/2 multiplexing:"
             << (d_ptr->config.multiplexingEnabled() ? "enabled" : "disabled");
}

QCNetworkConnectionPoolManager::~QCNetworkConnectionPoolManager()
{
    qDebug() << "QCNetworkConnectionPoolManager: Destroyed";
    qDebug() << "  - Total requests:" << d_ptr->totalRequests;
    qDebug() << "  - Reused connections:" << d_ptr->reusedConnections;
    if (d_ptr->totalRequests > 0) {
        qDebug() << "  - Reuse rate:"
                 << (d_ptr->reusedConnections * 100.0 / d_ptr->totalRequests) << "%";
    }
}

void QCNetworkConnectionPoolManager::setConfig(const QCNetworkConnectionPoolConfig &config)
{
    if (!config.isValid()) {
        qWarning() << "QCNetworkConnectionPoolManager::setConfig: Invalid config, ignored";
        return;
    }

    bool multiLimitsChanged = false;

    {
        QMutexLocker locker(&d_ptr->mutex);

        if (d_ptr->config.maxConnectionsPerHost() != config.maxConnectionsPerHost()
            || d_ptr->config.maxTotalConnections() != config.maxTotalConnections()
            || d_ptr->config.multiplexingEnabled() != config.multiplexingEnabled()) {
            qDebug() << "QCNetworkConnectionPoolManager: Config changed";
            qDebug() << "  - maxConnectionsPerHost:" << config.maxConnectionsPerHost();
            qDebug() << "  - maxTotalConnections:" << config.maxTotalConnections();
            qDebug() << "  - HTTP/2 multiplexing:"
                     << (config.multiplexingEnabled() ? "enabled" : "disabled");
        }

        multiLimitsChanged = (d_ptr->config.multiMaxTotalConnections()
                              != config.multiMaxTotalConnections())
                             || (d_ptr->config.multiMaxHostConnections()
                                 != config.multiMaxHostConnections())
                             || (d_ptr->config.multiMaxConcurrentStreams()
                                 != config.multiMaxConcurrentStreams())
                             || (d_ptr->config.multiMaxConnects() != config.multiMaxConnects());

        d_ptr->config = config;
    }

    if (multiLimitsChanged) {
        QCCurlMultiManager::instance()->applyLimitsConfig(config);
    }
}

QCNetworkConnectionPoolConfig QCNetworkConnectionPoolManager::config() const
{
    QMutexLocker locker(&d_ptr->mutex);
    return d_ptr->config;
}

void Internal::QCNetworkConnectionPoolManagerInternal::configureCurlHandle(void *handle,
                                                                           const QString &host)
{
    if (!handle) {
        return;
    }

    auto *curlHandle = static_cast<CURL *>(handle);
    auto *manager    = QCNetworkConnectionPoolManager::instance();

    QMutexLocker locker(&manager->d_ptr->mutex);
    // 复制配置后释放锁，避免 curl_easy_setopt 调用扩大临界区。
    QCNetworkConnectionPoolConfig cfg = manager->d_ptr->config;
    locker.unlock();

    curl_easy_setopt(curlHandle, CURLOPT_MAXCONNECTS, cfg.maxTotalConnections());

    curl_easy_setopt(curlHandle, CURLOPT_FRESH_CONNECT, 0L);

    curl_easy_setopt(curlHandle, CURLOPT_FORBID_REUSE, 0L);

    curl_easy_setopt(curlHandle, CURLOPT_TCP_KEEPALIVE, 1L);

    curl_easy_setopt(curlHandle, CURLOPT_TCP_KEEPIDLE, static_cast<long>(cfg.maxIdleTime()));

    curl_easy_setopt(curlHandle, CURLOPT_TCP_KEEPINTVL, 30L);

    if (cfg.dnsCacheEnabled()) {
        curl_easy_setopt(curlHandle,
                         CURLOPT_DNS_CACHE_TIMEOUT,
                         static_cast<long>(cfg.dnsCacheTimeout()));
    } else {
        curl_easy_setopt(curlHandle, CURLOPT_DNS_CACHE_TIMEOUT, 0L);
    }

    if (cfg.multiplexingEnabled()) {
        curl_easy_setopt(curlHandle, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_2_0);
    }

    if (cfg.pipeliningEnabled()) {
        curl_easy_setopt(curlHandle, CURLOPT_PIPEWAIT, 1L);

        qDebug() << "QCNetworkConnectionPoolManager: HTTP/1.1 pipelining enabled for" << host;
    }

#if LIBCURL_VERSION_NUM >= 0x075000
    if (cfg.maxConnectionLifetime() > 0) {
        curl_easy_setopt(curlHandle,
                         CURLOPT_MAXLIFETIME_CONN,
                         static_cast<long>(cfg.maxConnectionLifetime()));
    }
#endif

    locker.relock();
    manager->d_ptr->activeConnectionsPerHost[host]++;
}

void Internal::QCNetworkConnectionPoolManagerInternal::recordRequestCompleted(void *handle,
                                                                              bool wasReused)
{
    if (!handle) {
        return;
    }

    auto *curlHandle = static_cast<CURL *>(handle);
    auto *manager    = QCNetworkConnectionPoolManager::instance();

    // CURLINFO_NUM_CONNECTS: 本次 transfer 为完成请求新建的连接数量（通常：新建=1，复用=0）。
    long numConnects  = 0;
    const CURLcode rc = curl_easy_getinfo(curlHandle, CURLINFO_NUM_CONNECTS, &numConnects);

    bool actuallyReused = wasReused;
    if (!actuallyReused && rc == CURLE_OK) {
        actuallyReused = (numConnects == 0);
    }

    QMutexLocker locker(&manager->d_ptr->mutex);
    manager->d_ptr->totalRequests++;

    if (actuallyReused) {
        manager->d_ptr->reusedConnections++;
    }
}

QCNetworkConnectionPoolStatistics QCNetworkConnectionPoolManager::statistics() const
{
    QMutexLocker locker(&d_ptr->mutex);

    int activeConnections = 0;
    for (int count : d_ptr->activeConnectionsPerHost) {
        activeConnections += count;
    }

    const int idleConnections = qMax(0, d_ptr->config.maxTotalConnections() - activeConnections);
    return QCNetworkConnectionPoolStatistics(d_ptr->totalRequests,
                                             d_ptr->reusedConnections,
                                             activeConnections,
                                             idleConnections);
}

void QCNetworkConnectionPoolManager::resetStatistics()
{
    QMutexLocker locker(&d_ptr->mutex);

    qDebug() << "QCNetworkConnectionPoolManager: Resetting statistics";

    d_ptr->totalRequests = 0;
    d_ptr->reusedConnections = 0;
    d_ptr->activeConnectionsPerHost.clear();
}

void QCNetworkConnectionPoolManager::closeIdleConnections()
{
    qDebug() << "QCNetworkConnectionPoolManager: Closing idle connections";

    // libcurl 没有直接的 API 关闭连接池中的空闲连接
    // 这里只是提供接口，实际上连接会在超时后自动关闭

    // 可以通过触发 curl_multi_cleanup + curl_multi_init 来清空连接池
    // 但这需要访问 QCCurlMultiManager，超出了本管理器的职责范围

    qWarning() << "QCNetworkConnectionPoolManager::closeIdleConnections: "
               << "Idle connections will be closed automatically after timeout ("
               << d_ptr->config.maxIdleTime() << "seconds)";
}

} // namespace QCurl
