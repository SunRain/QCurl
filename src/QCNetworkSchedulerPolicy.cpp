// SPDX-License-Identifier: MIT
// Copyright (c) 2025 QCurl Project

#include "QCNetworkSchedulerPolicy.h"

#include <QHash>
#include <QSharedData>

namespace QCurl {

class QCNetworkSchedulerStatisticsData : public QSharedData
{
public:
    int pendingRequests = 0;
    int runningRequests = 0;
    int completedRequests = 0;
    int cancelledRequests = 0;
    qint64 totalBytesReceived = 0;
    qint64 totalBytesSent = 0;
    double avgResponseTime = 0.0;
};

QCNetworkSchedulerStatistics::QCNetworkSchedulerStatistics()
    : d(new QCNetworkSchedulerStatisticsData)
{
}

QCNetworkSchedulerStatistics::QCNetworkSchedulerStatistics(
    const QCNetworkSchedulerStatistics &other)
    = default;

QCNetworkSchedulerStatistics::QCNetworkSchedulerStatistics(
    QCNetworkSchedulerStatistics &&other) noexcept
    = default;

QCNetworkSchedulerStatistics::~QCNetworkSchedulerStatistics() = default;

QCNetworkSchedulerStatistics &QCNetworkSchedulerStatistics::operator=(
    const QCNetworkSchedulerStatistics &other)
    = default;

QCNetworkSchedulerStatistics &QCNetworkSchedulerStatistics::operator=(
    QCNetworkSchedulerStatistics &&other) noexcept
    = default;

int QCNetworkSchedulerStatistics::pendingRequests() const
{
    return d->pendingRequests;
}

void QCNetworkSchedulerStatistics::setPendingRequests(int value)
{
    d->pendingRequests = value;
}

int QCNetworkSchedulerStatistics::runningRequests() const
{
    return d->runningRequests;
}

void QCNetworkSchedulerStatistics::setRunningRequests(int value)
{
    d->runningRequests = value;
}

int QCNetworkSchedulerStatistics::completedRequests() const
{
    return d->completedRequests;
}

void QCNetworkSchedulerStatistics::setCompletedRequests(int value)
{
    d->completedRequests = value;
}

int QCNetworkSchedulerStatistics::cancelledRequests() const
{
    return d->cancelledRequests;
}

void QCNetworkSchedulerStatistics::setCancelledRequests(int value)
{
    d->cancelledRequests = value;
}

qint64 QCNetworkSchedulerStatistics::totalBytesReceived() const
{
    return d->totalBytesReceived;
}

void QCNetworkSchedulerStatistics::setTotalBytesReceived(qint64 value)
{
    d->totalBytesReceived = value;
}

qint64 QCNetworkSchedulerStatistics::totalBytesSent() const
{
    return d->totalBytesSent;
}

void QCNetworkSchedulerStatistics::setTotalBytesSent(qint64 value)
{
    d->totalBytesSent = value;
}

double QCNetworkSchedulerStatistics::avgResponseTime() const
{
    return d->avgResponseTime;
}

void QCNetworkSchedulerStatistics::setAvgResponseTime(double value)
{
    d->avgResponseTime = value;
}

class QCNetworkSchedulerPolicyLaneConfigData : public QSharedData
{
public:
    int weight = 1;
    int quantum = 1;
    int reservedGlobal = 0;
    int reservedPerHost = 0;
};

QCNetworkSchedulerPolicy::LaneConfig::LaneConfig()
    : d(new QCNetworkSchedulerPolicyLaneConfigData)
{
}

QCNetworkSchedulerPolicy::LaneConfig::LaneConfig(const LaneConfig &other) = default;

QCNetworkSchedulerPolicy::LaneConfig::LaneConfig(LaneConfig &&other) noexcept = default;

QCNetworkSchedulerPolicy::LaneConfig::~LaneConfig() = default;

QCNetworkSchedulerPolicy::LaneConfig &QCNetworkSchedulerPolicy::LaneConfig::operator=(
    const LaneConfig &other)
    = default;

QCNetworkSchedulerPolicy::LaneConfig &QCNetworkSchedulerPolicy::LaneConfig::operator=(
    LaneConfig &&other) noexcept
    = default;

int QCNetworkSchedulerPolicy::LaneConfig::weight() const
{
    return d->weight;
}

void QCNetworkSchedulerPolicy::LaneConfig::setWeight(int value)
{
    d->weight = value;
}

int QCNetworkSchedulerPolicy::LaneConfig::quantum() const
{
    return d->quantum;
}

void QCNetworkSchedulerPolicy::LaneConfig::setQuantum(int value)
{
    d->quantum = value;
}

int QCNetworkSchedulerPolicy::LaneConfig::reservedGlobal() const
{
    return d->reservedGlobal;
}

void QCNetworkSchedulerPolicy::LaneConfig::setReservedGlobal(int value)
{
    d->reservedGlobal = value;
}

int QCNetworkSchedulerPolicy::LaneConfig::reservedPerHost() const
{
    return d->reservedPerHost;
}

void QCNetworkSchedulerPolicy::LaneConfig::setReservedPerHost(int value)
{
    d->reservedPerHost = value;
}

class QCNetworkSchedulerPolicyData : public QSharedData
{
public:
    QCNetworkLaneKey defaultLane = QCNetworkLaneKey::defaultLane();
    QHash<QString, QCNetworkSchedulerPolicy::LaneConfig> laneConfigs;
    QList<QString> laneOrder;
    int maxConcurrentRequests = 6;
    int maxRequestsPerHost = 2;
    qint64 maxBandwidthBytesPerSec = 0;
    bool throttlingEnabled = true;
};

namespace {

void setError(QString *error, const QString &message)
{
    if (error) {
        *error = message;
    }
}

bool isLaneConfigValid(const QCNetworkSchedulerPolicy::LaneConfig &config, QString *error)
{
    if (config.weight() <= 0) {
        setError(error, QStringLiteral("lane weight must be greater than zero"));
        return false;
    }
    if (config.quantum() <= 0) {
        setError(error, QStringLiteral("lane quantum must be greater than zero"));
        return false;
    }
    if (config.reservedGlobal() < 0) {
        setError(error, QStringLiteral("lane reservedGlobal must not be negative"));
        return false;
    }
    if (config.reservedPerHost() < 0) {
        setError(error, QStringLiteral("lane reservedPerHost must not be negative"));
        return false;
    }
    return true;
}

} // namespace

QCNetworkSchedulerPolicy::QCNetworkSchedulerPolicy()
    : d(new QCNetworkSchedulerPolicyData)
{
}

QCNetworkSchedulerPolicy::QCNetworkSchedulerPolicy(const QCNetworkSchedulerPolicy &other) = default;

QCNetworkSchedulerPolicy::QCNetworkSchedulerPolicy(QCNetworkSchedulerPolicy &&other) noexcept
    = default;

QCNetworkSchedulerPolicy::~QCNetworkSchedulerPolicy() = default;

QCNetworkSchedulerPolicy &QCNetworkSchedulerPolicy::operator=(
    const QCNetworkSchedulerPolicy &other)
    = default;

QCNetworkSchedulerPolicy &QCNetworkSchedulerPolicy::operator=(
    QCNetworkSchedulerPolicy &&other) noexcept
    = default;

QCNetworkLaneKey QCNetworkSchedulerPolicy::defaultLane() const
{
    return d->defaultLane;
}

void QCNetworkSchedulerPolicy::setDefaultLane(const QCNetworkLaneKey &lane)
{
    d->defaultLane = lane;
}

QCNetworkSchedulerPolicy::UnknownLaneMode QCNetworkSchedulerPolicy::unknownLaneMode() const noexcept
{
    return UnknownLaneMode::RequireRegistered;
}

bool QCNetworkSchedulerPolicy::isLaneRegistered(const QCNetworkLaneKey &lane) const
{
    return lane.isValid() && d->laneConfigs.contains(lane.name());
}

QList<QCNetworkLaneKey> QCNetworkSchedulerPolicy::registeredLanes() const
{
    QList<QCNetworkLaneKey> lanes;
    lanes.reserve(d->laneOrder.size());
    for (const QString &name : std::as_const(d->laneOrder)) {
        if (name.isEmpty()) {
            lanes.append(QCNetworkLaneKey::defaultLane());
            continue;
        }

        QCNetworkLaneKey lane;
        if (QCNetworkLaneKey::fromName(name, &lane)) {
            lanes.append(lane);
        }
    }
    return lanes;
}

bool QCNetworkSchedulerPolicy::setLaneConfig(const QCNetworkLaneKey &lane,
                                             const LaneConfig &config,
                                             QString *error)
{
    setError(error, QString());
    if (!lane.isValid()) {
        setError(error, QStringLiteral("invalid lane key cannot be registered"));
        return false;
    }

    if (!isLaneConfigValid(config, error)) {
        return false;
    }

    const QString laneName = lane.name();
    d->laneConfigs.insert(laneName, config);
    if (!d->laneOrder.contains(laneName)) {
        d->laneOrder.append(laneName);
    }
    return true;
}

QCNetworkSchedulerPolicy::LaneConfig QCNetworkSchedulerPolicy::laneConfig(
    const QCNetworkLaneKey &lane) const
{
    return d->laneConfigs.value(lane.name(), LaneConfig{});
}

int QCNetworkSchedulerPolicy::maxConcurrentRequests() const
{
    return d->maxConcurrentRequests;
}

void QCNetworkSchedulerPolicy::setMaxConcurrentRequests(int value)
{
    d->maxConcurrentRequests = value;
}

int QCNetworkSchedulerPolicy::maxRequestsPerHost() const
{
    return d->maxRequestsPerHost;
}

void QCNetworkSchedulerPolicy::setMaxRequestsPerHost(int value)
{
    d->maxRequestsPerHost = value;
}

qint64 QCNetworkSchedulerPolicy::maxBandwidthBytesPerSec() const
{
    return d->maxBandwidthBytesPerSec;
}

void QCNetworkSchedulerPolicy::setMaxBandwidthBytesPerSec(qint64 value)
{
    d->maxBandwidthBytesPerSec = value;
}

bool QCNetworkSchedulerPolicy::throttlingEnabled() const
{
    return d->throttlingEnabled;
}

void QCNetworkSchedulerPolicy::setThrottlingEnabled(bool enabled)
{
    d->throttlingEnabled = enabled;
}

bool QCNetworkSchedulerPolicy::validate(QString *error) const
{
    setError(error, QString());
    if (!d->defaultLane.isValid()) {
        setError(error, QStringLiteral("default lane must be a valid QCNetworkLaneKey"));
        return false;
    }
    if (!isLaneRegistered(d->defaultLane)) {
        setError(error, QStringLiteral("default lane must be registered"));
        return false;
    }
    if (d->maxConcurrentRequests <= 0) {
        setError(error, QStringLiteral("maxConcurrentRequests must be greater than zero"));
        return false;
    }
    if (d->maxRequestsPerHost <= 0) {
        setError(error, QStringLiteral("maxRequestsPerHost must be greater than zero"));
        return false;
    }
    if (d->maxBandwidthBytesPerSec < 0) {
        setError(error, QStringLiteral("maxBandwidthBytesPerSec must not be negative"));
        return false;
    }

    for (auto it = d->laneConfigs.cbegin(); it != d->laneConfigs.cend(); ++it) {
        Q_UNUSED(it.key());
        if (!isLaneConfigValid(it.value(), error)) {
            return false;
        }
    }
    return true;
}

QCNetworkSchedulerPolicy QCNetworkSchedulerPolicy::defaultPolicy()
{
    QCNetworkSchedulerPolicy policy;
    const bool defaultRegistered = policy.setLaneConfig(QCNetworkLaneKey::defaultLane(), LaneConfig{});
    const bool controlRegistered = policy.setLaneConfig(QCNetworkLaneKey::control(), LaneConfig{});
    const bool transferRegistered = policy.setLaneConfig(QCNetworkLaneKey::transfer(), LaneConfig{});
    const bool backgroundRegistered = policy.setLaneConfig(QCNetworkLaneKey::background(), LaneConfig{});
    Q_ASSERT(defaultRegistered);
    Q_ASSERT(controlRegistered);
    Q_ASSERT(transferRegistered);
    Q_ASSERT(backgroundRegistered);
    return policy;
}

} // namespace QCurl
