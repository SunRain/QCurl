// SPDX-License-Identifier: MIT
// Copyright (c) 2025 QCurl Project

#include "QCNetworkConnectionPoolManager.h"

#include "QCNetworkConnectionPoolManager_p.h"
#include "private/QCCurlOptionAdapter_p.h"

#include <QDebug>
#include <QMutex>
#include <QMutexLocker>

#include <chrono>
#include <curl/curl.h>

namespace QCurl {

namespace {

constexpr std::chrono::seconds kTcpKeepAliveInterval{30};

} // namespace

/// 管理器内部状态；配置和统计计数必须通过 mutex 访问。
class QCNetworkConnectionPoolManagerPrivate
{
public:
    mutable QMutex mutex;                 ///< 保护配置快照和全部统计字段。
    QCNetworkConnectionPoolConfig config; ///< 当前应用于新请求的连接池配置。
    qint64 totalRequests     = 0;         ///< 已完成统计归档的请求数。
    qint64 reusedConnections = 0;         ///< 确认复用已有连接的请求数。
    int activeRequests       = 0;
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
    qDebug() << "  - HTTP/2 multiplexing:"
             << (d_ptr->config.multiplexingEnabled() ? "enabled" : "disabled");
}

QCNetworkConnectionPoolManager::~QCNetworkConnectionPoolManager()
{
    qDebug() << "QCNetworkConnectionPoolManager: Destroyed";
    qDebug() << "  - Total requests:" << d_ptr->totalRequests;
    qDebug() << "  - Reused connections:" << d_ptr->reusedConnections;
    if (d_ptr->totalRequests > 0) {
        qDebug() << "  - Reuse rate:" << (d_ptr->reusedConnections * 100.0 / d_ptr->totalRequests)
                 << "%";
    }
}

QCNetworkConnectionPoolManager::UpdateResult QCNetworkConnectionPoolManager::setConfig(
    const QCNetworkConnectionPoolConfig &config)
{
    if (!config.isValid()) {
        return UpdateResult::InvalidArgument;
    }

    QMutexLocker locker(&d_ptr->mutex);
    d_ptr->config = config;
    return UpdateResult::Applied;
}

QCNetworkConnectionPoolConfig QCNetworkConnectionPoolManager::config() const
{
    QMutexLocker locker(&d_ptr->mutex);
    return d_ptr->config;
}

bool Internal::QCNetworkConnectionPoolManagerInternal::configureCurlHandle(
    CURL *handle, const QCNetworkConnectionPoolConfig &cfg, QString *error)
{
    if (!handle) {
        *error = QStringLiteral("连接池配置缺少 easy handle");
        return false;
    }
    const auto set = [handle, error](CURLoption option, const char *name, long value) {
        const auto code = Internal::CurlOptions::setWithTestHook(handle, option, name, value);
        if (code == CURLE_OK) {
            return true;
        }
        *error = QStringLiteral("连接池设置 %1 失败：%2")
                     .arg(QString::fromLatin1(name), QString::fromLatin1(curl_easy_strerror(code)));
        return false;
    };
    return set(CURLOPT_TCP_KEEPALIVE, "CURLOPT_TCP_KEEPALIVE", 1L)
           && set(CURLOPT_TCP_KEEPIDLE, "CURLOPT_TCP_KEEPIDLE", 60L)
           && set(CURLOPT_TCP_KEEPINTVL, "CURLOPT_TCP_KEEPINTVL", kTcpKeepAliveInterval.count())
           && set(CURLOPT_MAXAGE_CONN, "CURLOPT_MAXAGE_CONN", cfg.maxIdleTime())
           && set(CURLOPT_MAXLIFETIME_CONN, "CURLOPT_MAXLIFETIME_CONN", cfg.maxConnectionLifetime())
           && set(CURLOPT_DNS_CACHE_TIMEOUT,
                  "CURLOPT_DNS_CACHE_TIMEOUT",
                  cfg.dnsCacheEnabled() ? cfg.dnsCacheTimeout() : 0L);
}

void Internal::QCNetworkConnectionPoolManagerInternal::recordRequestStarted()
{
    auto *manager = QCNetworkConnectionPoolManager::instance();
    QMutexLocker locker(&manager->d_ptr->mutex);
    ++manager->d_ptr->activeRequests;
}

void Internal::QCNetworkConnectionPoolManagerInternal::recordRequestCompleted(CURL *handle)
{
    long numConnects  = -1;
    long status       = 0;
    const bool reused = handle
                        && curl_easy_getinfo(handle, CURLINFO_NUM_CONNECTS, &numConnects)
                               == CURLE_OK
                        && curl_easy_getinfo(handle, CURLINFO_RESPONSE_CODE, &status) == CURLE_OK
                        && numConnects == 0 && status > 0;
    auto *manager     = QCNetworkConnectionPoolManager::instance();
    QMutexLocker locker(&manager->d_ptr->mutex);
    Q_ASSERT(manager->d_ptr->activeRequests > 0);
    --manager->d_ptr->activeRequests;
    manager->d_ptr->totalRequests++;
    if (reused) {
        manager->d_ptr->reusedConnections++;
    }
}

QCNetworkConnectionPoolStatistics QCNetworkConnectionPoolManager::statistics() const
{
    QMutexLocker locker(&d_ptr->mutex);

    return QCNetworkConnectionPoolStatistics(d_ptr->totalRequests,
                                             d_ptr->reusedConnections,
                                             d_ptr->activeRequests);
}

void QCNetworkConnectionPoolManager::resetStatistics()
{
    QMutexLocker locker(&d_ptr->mutex);

    qDebug() << "QCNetworkConnectionPoolManager: Resetting statistics";

    d_ptr->totalRequests     = 0;
    d_ptr->reusedConnections = 0;
}

} // namespace QCurl
