// SPDX-License-Identifier: MIT
// Copyright (c) 2025 QCurl Project

#include "QCNetworkLaneCancelResult.h"

#include <QSharedData>

#include <algorithm>

namespace QCurl {

namespace {

QCNetworkLaneCancelResult::Status statusFromFailureReason(
    QCNetworkLaneCancelResult::FailureReason reason) noexcept
{
    switch (reason) {
    case QCNetworkLaneCancelResult::FailureReason::InvalidLane:
        return QCNetworkLaneCancelResult::Status::InvalidLane;
    case QCNetworkLaneCancelResult::FailureReason::UnregisteredLane:
        return QCNetworkLaneCancelResult::Status::UnregisteredLane;
    case QCNetworkLaneCancelResult::FailureReason::NonOwnerThread:
        return QCNetworkLaneCancelResult::Status::NonOwnerThread;
    case QCNetworkLaneCancelResult::FailureReason::SchedulerDisabled:
        return QCNetworkLaneCancelResult::Status::SchedulerDisabled;
    }
    Q_UNREACHABLE_RETURN(QCNetworkLaneCancelResult::Status::InvalidLane);
}

QString defaultErrorForFailureReason(QCNetworkLaneCancelResult::FailureReason reason)
{
    switch (reason) {
    case QCNetworkLaneCancelResult::FailureReason::InvalidLane:
        return QStringLiteral("scheduler lane is invalid");
    case QCNetworkLaneCancelResult::FailureReason::UnregisteredLane:
        return QStringLiteral("scheduler lane is not registered");
    case QCNetworkLaneCancelResult::FailureReason::NonOwnerThread:
        return QStringLiteral("scheduler lane cancellation must run on owner thread");
    case QCNetworkLaneCancelResult::FailureReason::SchedulerDisabled:
        return QStringLiteral("request scheduler is not enabled");
    }
    Q_UNREACHABLE_RETURN(QStringLiteral("scheduler lane cancellation failed"));
}

} // namespace

class QCNetworkLaneCancelResultData : public QSharedData
{
public:
    QCNetworkLaneCancelResult::Status status = QCNetworkLaneCancelResult::Status::Success;
    int cancelledRequests = 0;
    QString error;
};

QCNetworkLaneCancelResult::QCNetworkLaneCancelResult()
    : d(new QCNetworkLaneCancelResultData)
{
}

QCNetworkLaneCancelResult::QCNetworkLaneCancelResult(
    const QCNetworkLaneCancelResult &other)
    = default;

QCNetworkLaneCancelResult::QCNetworkLaneCancelResult(
    QCNetworkLaneCancelResult &&other) noexcept
    = default;

QCNetworkLaneCancelResult::~QCNetworkLaneCancelResult() = default;

QCNetworkLaneCancelResult &QCNetworkLaneCancelResult::operator=(
    const QCNetworkLaneCancelResult &other)
    = default;

QCNetworkLaneCancelResult &QCNetworkLaneCancelResult::operator=(
    QCNetworkLaneCancelResult &&other) noexcept
    = default;

QCNetworkLaneCancelResult QCNetworkLaneCancelResult::success(int cancelledRequests)
{
    Q_ASSERT(cancelledRequests >= 0);
    QCNetworkLaneCancelResult result;
    result.d->status = Status::Success;
    result.d->cancelledRequests = std::max(0, cancelledRequests);
    return result;
}

QCNetworkLaneCancelResult QCNetworkLaneCancelResult::failure(FailureReason reason,
                                                             const QString &error)
{
    QCNetworkLaneCancelResult result;
    result.d->status = statusFromFailureReason(reason);
    result.d->error = error.isEmpty() ? defaultErrorForFailureReason(reason) : error;
    return result;
}

QCNetworkLaneCancelResult::Status QCNetworkLaneCancelResult::status() const noexcept
{
    return d->status;
}

bool QCNetworkLaneCancelResult::isSuccess() const noexcept
{
    return d->status == Status::Success;
}

int QCNetworkLaneCancelResult::cancelledRequests() const noexcept
{
    return d->cancelledRequests;
}

QString QCNetworkLaneCancelResult::error() const
{
    return d->error;
}

} // namespace QCurl
