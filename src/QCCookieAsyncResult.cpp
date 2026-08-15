#include "QCCookieAsyncResult.h"

#include <QSharedData>

namespace QCurl {

/** cookie 修改操作结果的共享数据。 */
class QCCookieOperationResultData : public QSharedData
{
public:
    bool success                 = false;
    QCCookieAsyncError errorCode = QCCookieAsyncError::BusinessError;
    QString policyCode;
    QString error;
};

/** cookie 导出操作结果的共享数据。 */
class QCCookieExportResultData : public QSharedData
{
public:
    bool success                 = false;
    QCCookieAsyncError errorCode = QCCookieAsyncError::BusinessError;
    QString policyCode;
    QList<QCCookie> cookies;
    QString error;
};

QCCookieOperationResult::QCCookieOperationResult()
    : d(new QCCookieOperationResultData)
{}

QCCookieOperationResult::QCCookieOperationResult(const QCCookieOperationResult &other) = default;

QCCookieOperationResult::QCCookieOperationResult(QCCookieOperationResult &&other) noexcept = default;

QCCookieOperationResult::~QCCookieOperationResult() = default;

QCCookieOperationResult &QCCookieOperationResult::operator=(
    const QCCookieOperationResult &other) = default;

QCCookieOperationResult &QCCookieOperationResult::operator=(
    QCCookieOperationResult &&other) noexcept = default;

QCCookieOperationResult QCCookieOperationResult::success()
{
    QCCookieOperationResult result;
    result.d->success   = true;
    result.d->errorCode = QCCookieAsyncError::None;
    return result;
}

QCCookieOperationResult QCCookieOperationResult::failure(QCCookieAsyncError code,
                                                         const QString &error,
                                                         const QString &policyCode)
{
    QCCookieOperationResult result;
    result.d->success    = false;
    result.d->errorCode  = code == QCCookieAsyncError::None ? QCCookieAsyncError::BusinessError
                                                            : code;
    result.d->policyCode = policyCode;
    result.d->error      = error;
    return result;
}

QCCookieOperationResult QCCookieOperationResult::failure(const QString &error)
{
    return failure(QCCookieAsyncError::BusinessError, error);
}

bool QCCookieOperationResult::isSuccess() const noexcept
{
    return d->success;
}

QCCookieAsyncError QCCookieOperationResult::errorCode() const noexcept
{
    return d->errorCode;
}

QString QCCookieOperationResult::policyCode() const
{
    return d->policyCode;
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

QCCookieExportResult &QCCookieExportResult::operator=(const QCCookieExportResult &other) = default;

QCCookieExportResult &QCCookieExportResult::operator=(
    QCCookieExportResult &&other) noexcept = default;

QCCookieExportResult QCCookieExportResult::success(const QList<QCCookie> &cookies)
{
    QCCookieExportResult result;
    result.d->success   = true;
    result.d->errorCode = QCCookieAsyncError::None;
    result.d->cookies   = cookies;
    return result;
}

QCCookieExportResult QCCookieExportResult::failure(QCCookieAsyncError code,
                                                   const QString &error,
                                                   const QString &policyCode)
{
    QCCookieExportResult result;
    result.d->success    = false;
    result.d->errorCode  = code == QCCookieAsyncError::None ? QCCookieAsyncError::BusinessError
                                                            : code;
    result.d->policyCode = policyCode;
    result.d->error      = error;
    return result;
}

QCCookieExportResult QCCookieExportResult::failure(const QString &error)
{
    return failure(QCCookieAsyncError::BusinessError, error);
}

bool QCCookieExportResult::isSuccess() const noexcept
{
    return d->success;
}

QCCookieAsyncError QCCookieExportResult::errorCode() const noexcept
{
    return d->errorCode;
}

QString QCCookieExportResult::policyCode() const
{
    return d->policyCode;
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
