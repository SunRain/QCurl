// SPDX-License-Identifier: MIT
// Copyright (c) 2025 QCurl Project

#include "QCNetworkRequestScheduler.h"

#include "private/QCNetworkRequestSchedulerPrivate_p.h"

#include <QMutexLocker>
#include <QThread>
#include <QTimer>

namespace QCurl {

namespace {

QCNetworkRequestScheduler::Config schedulerConfigFromPolicy(
    const QCNetworkSchedulerPolicy &policy)
{
    QCNetworkRequestScheduler::Config config;
    config.setMaxConcurrentRequests(policy.maxConcurrentRequests());
    config.setMaxRequestsPerHost(policy.maxRequestsPerHost());
    config.setMaxBandwidthBytesPerSec(policy.maxBandwidthBytesPerSec());
    config.setEnableThrottling(policy.throttlingEnabled());
    return config;
}

QCNetworkRequestScheduler::LaneConfig schedulerLaneConfigFromPolicy(
    const QCNetworkSchedulerPolicy::LaneConfig &policyLane)
{
    QCNetworkRequestScheduler::LaneConfig config;
    config.setWeight(policyLane.weight());
    config.setQuantum(policyLane.quantum());
    config.setReservedGlobal(policyLane.reservedGlobal());
    config.setReservedPerHost(policyLane.reservedPerHost());
    return config;
}

void updateThrottleTimerLifecycle(QCNetworkRequestScheduler *scheduler,
                                  QTimer *timer,
                                  const QCNetworkRequestScheduler::Config &config)
{
    if (!timer) {
        return;
    }

    const bool canRunTimer = Internal::hasEventDispatcher(scheduler->thread());
    const bool shouldRunTimer = canRunTimer && config.enableThrottling();
    if (shouldRunTimer && !timer->isActive()) {
        timer->start();
        return;
    }
    if (!shouldRunTimer && timer->isActive()) {
        timer->stop();
    }
}

} // namespace

bool QCNetworkRequestScheduler::applyPolicy(const QCNetworkSchedulerPolicy &policy,
                                            QString *error)
{
    if (!policy.validate(error)) {
        return false;
    }

    if (QThread::currentThread() != thread()) {
        if (error) {
            *error = QStringLiteral(
                "QCNetworkRequestScheduler::applyPolicy must run on owner thread");
        }
        return false;
    }
    Internal::assertSchedulerOwnerThread(this, "QCNetworkRequestScheduler::applyPolicy");

    {
        QMutexLocker locker(&m_impl->mutex);
        m_impl->config = schedulerConfigFromPolicy(policy);
        m_impl->queues.clearLaneConfigs();
        for (const QCNetworkLaneKey &lane : policy.registeredLanes()) {
            QCNetworkSchedulerPolicy::LaneConfig laneConfig;
            const bool hasLaneConfig = policy.laneConfig(lane, &laneConfig, error);
            if (!hasLaneConfig) {
                return false;
            }
            m_impl->queues.setLaneConfig(
                lane.name(), schedulerLaneConfigFromPolicy(laneConfig));
        }
    }

    updateThrottleTimerLifecycle(this, m_impl->throttleTimer, m_impl->config);
    processQueue();
    return true;
}

#ifdef QCURL_ENABLE_TEST_HOOKS
void QCNetworkRequestScheduler::setConfigForTesting(const Config &config)
{
    if (QThread::currentThread() != thread()) {
        Internal::invokeOnSchedulerOwnerThread(
            this,
            [this, config]() { setConfigForTesting(config); },
            "QCNetworkRequestScheduler::setConfigForTesting");
        return;
    }
    Internal::assertSchedulerOwnerThread(
        this, "QCNetworkRequestScheduler::setConfigForTesting");

    {
        QMutexLocker locker(&m_impl->mutex);
        m_impl->config = config;
    }

    updateThrottleTimerLifecycle(this, m_impl->throttleTimer, m_impl->config);
    processQueue();
}

QCNetworkRequestScheduler::Config QCNetworkRequestScheduler::configForTesting() const
{
    if (QThread::currentThread() != thread()) {
        return Internal::rejectOffOwnerThreadValue(
            this, Config{}, "QCNetworkRequestScheduler::configForTesting");
    }
    Internal::assertSchedulerOwnerThread(
        this, "QCNetworkRequestScheduler::configForTesting");

    QMutexLocker locker(&m_impl->mutex);
    return m_impl->config;
}

void QCNetworkRequestScheduler::setLaneConfigForTesting(const QString &lane,
                                                        const LaneConfig &config)
{
    if (QThread::currentThread() != thread()) {
        Internal::invokeOnSchedulerOwnerThread(
            this,
            [this, lane, config]() { setLaneConfigForTesting(lane, config); },
            "QCNetworkRequestScheduler::setLaneConfigForTesting");
        return;
    }
    Internal::assertSchedulerOwnerThread(
        this, "QCNetworkRequestScheduler::setLaneConfigForTesting");

    const QString laneKey = Internal::normalizedLane(lane);
    const LaneConfig sanitized = Internal::sanitizedLaneConfig(config);

    {
        QMutexLocker locker(&m_impl->mutex);
        m_impl->queues.setLaneConfig(laneKey, sanitized);
    }

    processQueue();
}

QCNetworkRequestScheduler::LaneConfig
QCNetworkRequestScheduler::laneConfigForTesting(const QString &lane) const
{
    if (QThread::currentThread() != thread()) {
        return Internal::rejectOffOwnerThreadValue(
            this, LaneConfig{}, "QCNetworkRequestScheduler::laneConfigForTesting");
    }
    Internal::assertSchedulerOwnerThread(
        this, "QCNetworkRequestScheduler::laneConfigForTesting");

    QMutexLocker locker(&m_impl->mutex);
    return m_impl->queues.laneConfigFor(Internal::normalizedLane(lane));
}

#endif

} // namespace QCurl
