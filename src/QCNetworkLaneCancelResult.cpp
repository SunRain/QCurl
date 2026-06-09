// SPDX-License-Identifier: MIT
// Copyright (c) 2025 QCurl Project

#include "QCNetworkLaneCancelResult.h"

#include <QSharedData>

namespace QCurl {

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
    QCNetworkLaneCancelResult result;
    result.d->status = Status::Success;
    result.d->cancelledRequests = cancelledRequests;
    return result;
}

QCNetworkLaneCancelResult QCNetworkLaneCancelResult::failure(Status status, const QString &error)
{
    QCNetworkLaneCancelResult result;
    result.d->status = status;
    result.d->error = error;
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
