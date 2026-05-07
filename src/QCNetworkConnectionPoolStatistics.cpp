// SPDX-License-Identifier: MIT
// Copyright (c) 2025 QCurl Project

#include "QCNetworkConnectionPoolManager.h"

#include <QSharedData>

namespace QCurl {

/// 连接池统计快照的共享存储；数值在 statistics() 调用时固定。
class QCNetworkConnectionPoolStatisticsData : public QSharedData
{
public:
    qint64 totalRequests = 0;
    qint64 reusedConnections = 0;
    double reuseRate = 0.0;
    int activeConnections = 0;
    int idleConnections = 0;
};

QCNetworkConnectionPoolStatistics::QCNetworkConnectionPoolStatistics()
    : d(new QCNetworkConnectionPoolStatisticsData)
{}

QCNetworkConnectionPoolStatistics::QCNetworkConnectionPoolStatistics(
    const QCNetworkConnectionPoolStatistics &other) = default;

QCNetworkConnectionPoolStatistics::QCNetworkConnectionPoolStatistics(
    QCNetworkConnectionPoolStatistics &&other) noexcept = default;

QCNetworkConnectionPoolStatistics::~QCNetworkConnectionPoolStatistics() = default;

QCNetworkConnectionPoolStatistics &QCNetworkConnectionPoolStatistics::operator=(
    const QCNetworkConnectionPoolStatistics &other) = default;

QCNetworkConnectionPoolStatistics &QCNetworkConnectionPoolStatistics::operator=(
    QCNetworkConnectionPoolStatistics &&other) noexcept = default;

QCNetworkConnectionPoolStatistics::QCNetworkConnectionPoolStatistics(qint64 totalRequests,
                                                                     qint64 reusedConnections,
                                                                     int activeConnections,
                                                                     int idleConnections)
    : d(new QCNetworkConnectionPoolStatisticsData)
{
    d->totalRequests = totalRequests;
    d->reusedConnections = reusedConnections;
    d->reuseRate = totalRequests > 0 ? (reusedConnections * 100.0) / totalRequests : 0.0;
    d->activeConnections = activeConnections;
    d->idleConnections = idleConnections;
}

qint64 QCNetworkConnectionPoolStatistics::totalRequests() const
{
    return d->totalRequests;
}

qint64 QCNetworkConnectionPoolStatistics::reusedConnections() const
{
    return d->reusedConnections;
}

double QCNetworkConnectionPoolStatistics::reuseRate() const
{
    return d->reuseRate;
}

int QCNetworkConnectionPoolStatistics::activeConnections() const
{
    return d->activeConnections;
}

int QCNetworkConnectionPoolStatistics::idleConnections() const
{
    return d->idleConnections;
}

} // namespace QCurl
