// SPDX-License-Identifier: MIT
// Copyright (c) 2025 QCurl Project

#include "QCNetworkRequestScheduler.h"

#include "private/QCNetworkRequestSchedulerPrivate_p.h"

#include <QMutexLocker>
#include <QThread>

namespace QCurl {

void QCNetworkRequestScheduler::setConfig(const Config &config)
{
    if (QThread::currentThread() != thread()) {
        Internal::invokeOnSchedulerOwnerThread(
            this, [this, config]() { setConfig(config); }, "QCNetworkRequestScheduler::setConfig");
        return;
    }
    Internal::assertSchedulerOwnerThread(this, "QCNetworkRequestScheduler::setConfig");

    {
        QMutexLocker locker(&m_impl->mutex);
        m_impl->config = config;
    }

    processQueue();
}

QCNetworkRequestScheduler::Config QCNetworkRequestScheduler::config() const
{
    if (QThread::currentThread() != thread()) {
        return Internal::rejectOffOwnerThreadValue(this, Config{}, "QCNetworkRequestScheduler::config");
    }
    Internal::assertSchedulerOwnerThread(this, "QCNetworkRequestScheduler::config");

    QMutexLocker locker(&m_impl->mutex);
    return m_impl->config;
}

void QCNetworkRequestScheduler::setLaneConfig(const QString &lane, const LaneConfig &config)
{
    if (QThread::currentThread() != thread()) {
        Internal::invokeOnSchedulerOwnerThread(this,
                                     [this, lane, config]() { setLaneConfig(lane, config); },
                                     "QCNetworkRequestScheduler::setLaneConfig");
        return;
    }
    Internal::assertSchedulerOwnerThread(this, "QCNetworkRequestScheduler::setLaneConfig");

    const QString laneKey = Internal::normalizedLane(lane);
    const LaneConfig sanitized = Internal::sanitizedLaneConfig(config);

    {
        QMutexLocker locker(&m_impl->mutex);
        m_impl->queues.setLaneConfig(laneKey, sanitized);
    }

    processQueue();
}

QCNetworkRequestScheduler::LaneConfig QCNetworkRequestScheduler::laneConfig(const QString &lane) const
{
    if (QThread::currentThread() != thread()) {
        return Internal::rejectOffOwnerThreadValue(this, LaneConfig{}, "QCNetworkRequestScheduler::laneConfig");
    }
    Internal::assertSchedulerOwnerThread(this, "QCNetworkRequestScheduler::laneConfig");

    QMutexLocker locker(&m_impl->mutex);
    return m_impl->queues.laneConfigFor(Internal::normalizedLane(lane));
}

} // namespace QCurl
