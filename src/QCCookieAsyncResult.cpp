#include "QCCookieAsyncResult.h"

#include <QSharedData>

#include <utility>

namespace QCurl {

class QCCookieOperationResultData : public QSharedData
{
public:
    bool success = false;
    QString error;
};

class QCCookieExportResultData : public QSharedData
{
public:
    bool success = false;
    QList<QNetworkCookie> cookies;
    QString error;
};

QCCookieOperationResult::QCCookieOperationResult()
    : d(new QCCookieOperationResultData)
{}

QCCookieOperationResult::QCCookieOperationResult(
    const QCCookieOperationResult &other) = default;

QCCookieOperationResult::QCCookieOperationResult(
    QCCookieOperationResult &&other) noexcept = default;

QCCookieOperationResult::~QCCookieOperationResult() = default;

QCCookieOperationResult &QCCookieOperationResult::operator=(
    const QCCookieOperationResult &other) = default;

QCCookieOperationResult &QCCookieOperationResult::operator=(
    QCCookieOperationResult &&other) noexcept = default;

QCCookieOperationResult QCCookieOperationResult::success()
{
    QCCookieOperationResult result;
    result.d->success = true;
    return result;
}

QCCookieOperationResult QCCookieOperationResult::failure(QString error)
{
    QCCookieOperationResult result;
    result.d->success = false;
    result.d->error = std::move(error);
    return result;
}

bool QCCookieOperationResult::isSuccess() const noexcept
{
    return d->success;
}

QString QCCookieOperationResult::error() const
{
    return d->error;
}

QCCookieExportResult::QCCookieExportResult()
    : d(new QCCookieExportResultData)
{}

QCCookieExportResult::QCCookieExportResult(const QCCookieExportResult &other) = default;

QCCookieExportResult::QCCookieExportResult(QCCookieExportResult &&other) noexcept = default;

QCCookieExportResult::~QCCookieExportResult() = default;

QCCookieExportResult &QCCookieExportResult::operator=(
    const QCCookieExportResult &other) = default;

QCCookieExportResult &QCCookieExportResult::operator=(
    QCCookieExportResult &&other) noexcept = default;

QCCookieExportResult QCCookieExportResult::success(QList<QNetworkCookie> cookies)
{
    QCCookieExportResult result;
    result.d->success = true;
    result.d->cookies = std::move(cookies);
    return result;
}

QCCookieExportResult QCCookieExportResult::failure(QString error)
{
    QCCookieExportResult result;
    result.d->success = false;
    result.d->error = std::move(error);
    return result;
}

bool QCCookieExportResult::isSuccess() const noexcept
{
    return d->success;
}

QList<QNetworkCookie> QCCookieExportResult::cookies() const
{
    return d->cookies;
}

QString QCCookieExportResult::error() const
{
    return d->error;
}

} // namespace QCurl
