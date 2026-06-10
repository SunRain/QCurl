#include "QCCookieAsyncResult.h"

#include <QSharedData>

namespace QCurl {

/** cookie 修改操作结果的共享数据。 */
class QCCookieOperationResultData : public QSharedData
{
public:
    bool success = false;
    QString error;
};

/** cookie 导出操作结果的共享数据。 */
class QCCookieExportResultData : public QSharedData
{
public:
    bool success = false;
    QList<QCCookie> cookies;
    QString error;
};

QCCookieOperationResult::QCCookieOperationResult()
    : d(new QCCookieOperationResultData)
{
}

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

QCCookieOperationResult QCCookieOperationResult::failure(const QString &error)
{
    QCCookieOperationResult result;
    result.d->success = false;
    result.d->error = error;
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
{
}

QCCookieExportResult::QCCookieExportResult(const QCCookieExportResult &other) = default;

QCCookieExportResult::QCCookieExportResult(QCCookieExportResult &&other) noexcept = default;

QCCookieExportResult::~QCCookieExportResult() = default;

QCCookieExportResult &QCCookieExportResult::operator=(
    const QCCookieExportResult &other) = default;

QCCookieExportResult &QCCookieExportResult::operator=(
    QCCookieExportResult &&other) noexcept = default;

QCCookieExportResult QCCookieExportResult::success(const QList<QCCookie> &cookies)
{
    QCCookieExportResult result;
    result.d->success = true;
    result.d->cookies = cookies;
    return result;
}

QCCookieExportResult QCCookieExportResult::failure(const QString &error)
{
    QCCookieExportResult result;
    result.d->success = false;
    result.d->error = error;
    return result;
}

bool QCCookieExportResult::isSuccess() const noexcept
{
    return d->success;
}

QList<QCCookie> QCCookieExportResult::cookies() const
{
    return d->cookies;
}

QString QCCookieExportResult::error() const
{
    return d->error;
}

} // namespace QCurl
