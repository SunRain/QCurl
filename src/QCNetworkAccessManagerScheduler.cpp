/**
 * @file
 * @brief Implements request scheduler controls exposed by QCNetworkAccessManager.
 */

#include "QCNetworkAccessManager.h"
#include "QCNetworkAccessManager_p.h"
#include "QCNetworkRequestScheduler.h"

#include <QDebug>
#include <QThread>

namespace QCurl {

void QCNetworkAccessManager::enableRequestScheduler(bool enabled)
{
    Q_D(QCNetworkAccessManager);
    d->schedulerEnabled = enabled;
}

bool QCNetworkAccessManager::isSchedulerEnabled() const
{
    Q_D(const QCNetworkAccessManager);
    return d->schedulerEnabled;
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
    Q_ASSERT_X(QThread::currentThread() == q_func()->thread(),
               apiName,
               "QCNetworkAccessManager scheduler API must run on owner thread");
    return true;
}

bool QCNetworkAccessManager::setSchedulerPolicy(const QCNetworkSchedulerPolicy &policy,
                                                QString *error)
{
    Q_D(QCNetworkAccessManager);
    if (d->rejectOffOwnerThread(error, "QCNetworkAccessManager::setSchedulerPolicy")) {
        return false;
    }
    if (!policy.validate(error)) {
        return false;
    }
    if (!d->scheduler->applyPolicy(policy, error)) {
        return false;
    }
    d->schedulerPolicy = policy;
    return true;
}

QCNetworkSchedulerPolicy QCNetworkAccessManager::schedulerPolicy() const
{
    Q_D(const QCNetworkAccessManager);
    if (d->rejectOffOwnerThread(nullptr, "QCNetworkAccessManager::schedulerPolicy")) {
        return QCNetworkSchedulerPolicy{};
    }
    return d->schedulerPolicy;
}

QCNetworkSchedulerStatistics QCNetworkAccessManager::schedulerStatistics() const
{
    Q_D(const QCNetworkAccessManager);
    if (d->rejectOffOwnerThread(nullptr, "QCNetworkAccessManager::schedulerStatistics")) {
        return QCNetworkSchedulerStatistics{};
    }

    const QCNetworkRequestScheduler::Statistics stats = d->scheduler->statistics();

    QCNetworkSchedulerStatistics result;
    result.setPendingRequests(stats.pendingRequests());
    result.setRunningRequests(stats.runningRequests());
    result.setCompletedRequests(stats.completedRequests());
    result.setCancelledRequests(stats.cancelledRequests());
    result.setTotalBytesReceived(stats.totalBytesReceived());
    result.setTotalBytesSent(stats.totalBytesSent());
    result.setAvgResponseTime(stats.avgResponseTime());
    return result;
}

QCNetworkLaneCancelResult QCNetworkAccessManager::cancelLaneRequests(const QCNetworkLaneKey &lane,
                                                                     SchedulerCancelScope scope)
{
    Q_D(QCNetworkAccessManager);
    if (d->rejectOffOwnerThread(nullptr, "QCNetworkAccessManager::cancelLaneRequests")) {
        return QCNetworkLaneCancelResult::failure(
            QCNetworkLaneCancelResult::FailureReason::NonOwnerThread,
            QStringLiteral("QCNetworkAccessManager::cancelLaneRequests must run on owner thread"));
    }
    if (!d->schedulerEnabled) {
        return QCNetworkLaneCancelResult::failure(
            QCNetworkLaneCancelResult::FailureReason::SchedulerDisabled,
            QStringLiteral("QCNetworkAccessManager: request scheduler is not enabled"));
    }
    if (!lane.isValid()) {
        return QCNetworkLaneCancelResult::failure(
            QCNetworkLaneCancelResult::FailureReason::InvalidLane,
            QStringLiteral("QCNetworkAccessManager: invalid scheduler lane cannot be cancelled"));
    }
    if (!d->schedulerPolicy.isLaneRegistered(lane)) {
        return QCNetworkLaneCancelResult::failure(
            QCNetworkLaneCancelResult::FailureReason::UnregisteredLane,
            QStringLiteral("QCNetworkAccessManager: scheduler lane is not registered: %1")
                .arg(lane.name()));
    }
    const auto schedulerScope = scope == SchedulerCancelScope::PendingAndRunning
                                    ? QCNetworkRequestScheduler::CancelLaneScope::PendingAndRunning
                                    : QCNetworkRequestScheduler::CancelLaneScope::PendingOnly;
    int cancelledRequests     = 0;
    const QCNetworkRequestScheduler::CommandResult schedulerResult
        = d->scheduler->cancelLaneRequests(lane.name(), schedulerScope, &cancelledRequests);
    switch (schedulerResult) {
        case QCNetworkRequestScheduler::CommandResult::Applied:
        case QCNetworkRequestScheduler::CommandResult::NoChange:
            return QCNetworkLaneCancelResult::success(cancelledRequests);
        case QCNetworkRequestScheduler::CommandResult::WrongThread:
            return QCNetworkLaneCancelResult::failure(
                QCNetworkLaneCancelResult::FailureReason::NonOwnerThread,
                QStringLiteral("QCNetworkAccessManager: scheduler lane cancellation must run on "
                               "owner thread"));
        case QCNetworkRequestScheduler::CommandResult::InvalidArgument:
            return QCNetworkLaneCancelResult::failure(
                QCNetworkLaneCancelResult::FailureReason::InvalidLane,
                QStringLiteral(
                    "QCNetworkAccessManager: scheduler rejected lane cancellation arguments"));
        case QCNetworkRequestScheduler::CommandResult::NotTracked:
        case QCNetworkRequestScheduler::CommandResult::InvalidState:
        case QCNetworkRequestScheduler::CommandResult::NullReply:
        case QCNetworkRequestScheduler::CommandResult::ThreadAffinityMismatch:
            break;
    }
    return QCNetworkLaneCancelResult::failure(
        QCNetworkLaneCancelResult::FailureReason::SchedulerDisabled,
        QStringLiteral("QCNetworkAccessManager: scheduler lane cancellation invariant failed"));
}

#ifdef QCURL_ENABLE_TEST_HOOKS
QCNetworkRequestScheduler *QCNetworkAccessManager::schedulerForTesting() const
{
    Q_D(const QCNetworkAccessManager);
    if (d->rejectOffOwnerThread(nullptr, "QCNetworkAccessManager::schedulerForTesting")) {
        return nullptr;
    }
    return d->scheduler;
}

void QCNetworkAccessManager::registerSchedulerLaneForTesting(const QCNetworkLaneKey &lane)
{
    Q_D(QCNetworkAccessManager);
    if (!lane.isValid()
        || d->rejectOffOwnerThread(nullptr,
                                   "QCNetworkAccessManager::registerSchedulerLaneForTesting")) {
        return;
    }
    if (!d->schedulerPolicy.isLaneRegistered(lane)) {
        QString error;
        const bool registered
            = d->schedulerPolicy.setLaneConfig(lane, QCNetworkSchedulerPolicy::LaneConfig{}, &error);
        Q_ASSERT_X(registered,
                   "QCNetworkAccessManager::registerSchedulerLaneForTesting",
                   qPrintable(error));
    }
}
#endif

} // namespace QCurl
