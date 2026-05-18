// SPDX-License-Identifier: MIT
// Copyright (c) 2025 QCurl Project

#include "QCNetworkRequestScheduler.h"

#include <QSharedData>

namespace QCurl {

class QCNetworkRequestSchedulerConfigData : public QSharedData
{
public:
    int maxConcurrentRequests   = 6;
    int maxRequestsPerHost      = 2;
    qint64 maxBandwidthBytesPerSec = 0;
    bool enableThrottling       = true;
};

QCNetworkRequestScheduler::Config::Config()
    : d(new QCNetworkRequestSchedulerConfigData)
{
}

QCNetworkRequestScheduler::Config::Config(const Config &other) = default;

QCNetworkRequestScheduler::Config::Config(Config &&other) = default;

QCNetworkRequestScheduler::Config::~Config() = default;

QCNetworkRequestScheduler::Config &QCNetworkRequestScheduler::Config::operator=(const Config &other)
    = default;

QCNetworkRequestScheduler::Config &QCNetworkRequestScheduler::Config::operator=(Config &&other)
    = default;

int QCNetworkRequestScheduler::Config::maxConcurrentRequests() const
{
    return d->maxConcurrentRequests;
}

void QCNetworkRequestScheduler::Config::setMaxConcurrentRequests(int value)
{
    d->maxConcurrentRequests = value;
}

int QCNetworkRequestScheduler::Config::maxRequestsPerHost() const
{
    return d->maxRequestsPerHost;
}

void QCNetworkRequestScheduler::Config::setMaxRequestsPerHost(int value)
{
    d->maxRequestsPerHost = value;
}

qint64 QCNetworkRequestScheduler::Config::maxBandwidthBytesPerSec() const
{
    return d->maxBandwidthBytesPerSec;
}

void QCNetworkRequestScheduler::Config::setMaxBandwidthBytesPerSec(qint64 value)
{
    d->maxBandwidthBytesPerSec = value;
}

bool QCNetworkRequestScheduler::Config::enableThrottling() const
{
    return d->enableThrottling;
}

void QCNetworkRequestScheduler::Config::setEnableThrottling(bool enabled)
{
    d->enableThrottling = enabled;
}

class QCNetworkRequestSchedulerStatisticsData : public QSharedData
{
public:
    int pendingRequests       = 0;
    int runningRequests       = 0;
    int completedRequests     = 0;
    int cancelledRequests     = 0;
    qint64 totalBytesReceived = 0;
    qint64 totalBytesSent     = 0;
    double avgResponseTime    = 0.0;
};

QCNetworkRequestScheduler::Statistics::Statistics()
    : d(new QCNetworkRequestSchedulerStatisticsData)
{
}

QCNetworkRequestScheduler::Statistics::Statistics(const Statistics &other) = default;

QCNetworkRequestScheduler::Statistics::Statistics(Statistics &&other) = default;

QCNetworkRequestScheduler::Statistics::~Statistics() = default;

QCNetworkRequestScheduler::Statistics &QCNetworkRequestScheduler::Statistics::operator=(
    const Statistics &other)
    = default;

QCNetworkRequestScheduler::Statistics &QCNetworkRequestScheduler::Statistics::operator=(
    Statistics &&other)
    = default;

int QCNetworkRequestScheduler::Statistics::pendingRequests() const
{
    return d->pendingRequests;
}

void QCNetworkRequestScheduler::Statistics::setPendingRequests(int value)
{
    d->pendingRequests = value;
}

int QCNetworkRequestScheduler::Statistics::runningRequests() const
{
    return d->runningRequests;
}

void QCNetworkRequestScheduler::Statistics::setRunningRequests(int value)
{
    d->runningRequests = value;
}

int QCNetworkRequestScheduler::Statistics::completedRequests() const
{
    return d->completedRequests;
}

void QCNetworkRequestScheduler::Statistics::setCompletedRequests(int value)
{
    d->completedRequests = value;
}

int QCNetworkRequestScheduler::Statistics::cancelledRequests() const
{
    return d->cancelledRequests;
}

void QCNetworkRequestScheduler::Statistics::setCancelledRequests(int value)
{
    d->cancelledRequests = value;
}

qint64 QCNetworkRequestScheduler::Statistics::totalBytesReceived() const
{
    return d->totalBytesReceived;
}

void QCNetworkRequestScheduler::Statistics::setTotalBytesReceived(qint64 value)
{
    d->totalBytesReceived = value;
}

qint64 QCNetworkRequestScheduler::Statistics::totalBytesSent() const
{
    return d->totalBytesSent;
}

void QCNetworkRequestScheduler::Statistics::setTotalBytesSent(qint64 value)
{
    d->totalBytesSent = value;
}

double QCNetworkRequestScheduler::Statistics::avgResponseTime() const
{
    return d->avgResponseTime;
}

void QCNetworkRequestScheduler::Statistics::setAvgResponseTime(double value)
{
    d->avgResponseTime = value;
}

class QCNetworkRequestSchedulerLaneConfigData : public QSharedData
{
public:
    int weight          = 1;
    int quantum         = 1;
    int reservedGlobal  = 0;
    int reservedPerHost = 0;
};

QCNetworkRequestScheduler::LaneConfig::LaneConfig()
    : d(new QCNetworkRequestSchedulerLaneConfigData)
{
}

QCNetworkRequestScheduler::LaneConfig::LaneConfig(const LaneConfig &other) = default;

QCNetworkRequestScheduler::LaneConfig::LaneConfig(LaneConfig &&other) = default;

QCNetworkRequestScheduler::LaneConfig::~LaneConfig() = default;

QCNetworkRequestScheduler::LaneConfig &QCNetworkRequestScheduler::LaneConfig::operator=(
    const LaneConfig &other)
    = default;

QCNetworkRequestScheduler::LaneConfig &QCNetworkRequestScheduler::LaneConfig::operator=(
    LaneConfig &&other)
    = default;

int QCNetworkRequestScheduler::LaneConfig::weight() const
{
    return d->weight;
}

void QCNetworkRequestScheduler::LaneConfig::setWeight(int value)
{
    d->weight = value;
}

int QCNetworkRequestScheduler::LaneConfig::quantum() const
{
    return d->quantum;
}

void QCNetworkRequestScheduler::LaneConfig::setQuantum(int value)
{
    d->quantum = value;
}

int QCNetworkRequestScheduler::LaneConfig::reservedGlobal() const
{
    return d->reservedGlobal;
}

void QCNetworkRequestScheduler::LaneConfig::setReservedGlobal(int value)
{
    d->reservedGlobal = value;
}

int QCNetworkRequestScheduler::LaneConfig::reservedPerHost() const
{
    return d->reservedPerHost;
}

void QCNetworkRequestScheduler::LaneConfig::setReservedPerHost(int value)
{
    d->reservedPerHost = value;
}

} // namespace QCurl
