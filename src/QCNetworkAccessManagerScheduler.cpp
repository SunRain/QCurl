// SPDX-License-Identifier: MIT
// Copyright (c) 2025 QCurl Project

#include "QCNetworkAccessManager.h"
#include "QCNetworkAccessManager_p.h"

#include <QDebug>
#include <QThread>

namespace QCurl {

void QCNetworkAccessManager::enableRequestScheduler(bool enabled)
{
    Q_D(QCNetworkAccessManager);
    if (!d->rejectOffOwnerThread(nullptr, "QCNetworkAccessManager::enableRequestScheduler")) {
        d->schedulerEnabled = enabled;
    }
}

bool QCNetworkAccessManager::isSchedulerEnabled() const
{
    Q_D(const QCNetworkAccessManager);
    return !d->rejectOffOwnerThread(nullptr, "QCNetworkAccessManager::isSchedulerEnabled")
           && d->schedulerEnabled;
}

bool QCNetworkAccessManagerPrivate::rejectOffOwnerThread(QString *error, const char *apiName) const
{
    if (QThread::currentThread() == q_func()->thread()) {
        return false;
    }
    if (error) {
        *error = QStringLiteral("%1 must run on manager owner thread")
                     .arg(QString::fromUtf8(apiName));
    }
    qWarning() << apiName << ": called from non-owner thread";
    return true;
}

bool QCNetworkAccessManager::setSchedulerPolicy(const QCNetworkSchedulerPolicy &policy,
                                                QString *error)
{
    Q_D(QCNetworkAccessManager);
    if (d->rejectOffOwnerThread(error, "QCNetworkAccessManager::setSchedulerPolicy")) {
        return false;
    }
    return d->scheduler->applyPolicy(policy, error);
}

QCNetworkSchedulerPolicy QCNetworkAccessManager::schedulerPolicy() const
{
    Q_D(const QCNetworkAccessManager);
    if (d->rejectOffOwnerThread(nullptr, "QCNetworkAccessManager::schedulerPolicy")) {
        return {};
    }
    return d->scheduler->policy();
}

QCNetworkSchedulerStatistics QCNetworkAccessManager::schedulerStatistics() const
{
    Q_D(const QCNetworkAccessManager);
    if (d->rejectOffOwnerThread(nullptr, "QCNetworkAccessManager::schedulerStatistics")) {
        return {};
    }
    return d->scheduler->statistics();
}

QCNetworkLaneCancelResult QCNetworkAccessManager::cancelLaneRequests(const QCNetworkLaneKey &lane,
                                                                     SchedulerCancelScope scope)
{
    Q_D(QCNetworkAccessManager);
    using Failure = QCNetworkLaneCancelResult::FailureReason;
    if (d->rejectOffOwnerThread(nullptr, "QCNetworkAccessManager::cancelLaneRequests")) {
        return QCNetworkLaneCancelResult::failure(Failure::NonOwnerThread,
                                                  QStringLiteral(
                                                      "lane cancellation requires owner thread"));
    }
    if (!d->schedulerEnabled) {
        return QCNetworkLaneCancelResult::failure(Failure::SchedulerDisabled,
                                                  QStringLiteral(
                                                      "request scheduler is not enabled"));
    }
    if (!lane.isValid()) {
        return QCNetworkLaneCancelResult::failure(Failure::InvalidLane,
                                                  QStringLiteral("invalid scheduler lane"));
    }
    if (scope != SchedulerCancelScope::PendingOnly
        && scope != SchedulerCancelScope::PendingAndRunning) {
        return QCNetworkLaneCancelResult::failure(Failure::InvalidScope,
                                                  QStringLiteral(
                                                      "invalid lane cancellation scope"));
    }
    if (!d->scheduler->policy().isLaneRegistered(lane)) {
        return QCNetworkLaneCancelResult::failure(Failure::UnregisteredLane,
                                                  QStringLiteral(
                                                      "scheduler lane is not registered: %1")
                                                      .arg(lane.name()));
    }
    return QCNetworkLaneCancelResult::success(
        d->scheduler->cancelLaneRequests(lane, scope == SchedulerCancelScope::PendingAndRunning));
}

} // namespace QCurl
