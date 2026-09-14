#include "QCNetworkCache.h"

#include <QSharedData>

namespace QCurl {

/// 缓存查询结果的共享存储，status 负责区分未命中和空 body 命中。
class QCNetworkCacheLookupResultData : public QSharedData
{
public:
    QCNetworkCacheLookupStatus status = QCNetworkCacheLookupStatus::Miss;
    QCNetworkCacheMetadata metadata;
    QByteArray body;
};

/// 缓存清理操作的隐式共享结果存储。
class QCNetworkCacheClearResultData : public QSharedData
{
public:
    QCNetworkCacheClearResult::Status status       = QCNetworkCacheClearResult::Status::Success;
    qint64 removedCount                            = 0;
    qint64 failedCount                             = 0;
    qint64 remainingBytes                          = 0;
    QCNetworkCacheClearResult::ErrorCode errorCode = QCNetworkCacheClearResult::ErrorCode::None;
    QString errorMessage;
};

QCNetworkCacheLookupResult::QCNetworkCacheLookupResult()
    : d(new QCNetworkCacheLookupResultData)
{}

QCNetworkCacheLookupResult::QCNetworkCacheLookupResult(
    const QCNetworkCacheLookupResult &other) = default;

QCNetworkCacheLookupResult::QCNetworkCacheLookupResult(
    QCNetworkCacheLookupResult &&other) noexcept = default;

QCNetworkCacheLookupResult::~QCNetworkCacheLookupResult() = default;

QCNetworkCacheLookupResult &QCNetworkCacheLookupResult::operator=(
    const QCNetworkCacheLookupResult &other) = default;

QCNetworkCacheLookupResult &QCNetworkCacheLookupResult::operator=(
    QCNetworkCacheLookupResult &&other) noexcept = default;

QCNetworkCacheLookupStatus QCNetworkCacheLookupResult::status() const
{
    return d->status;
}

void QCNetworkCacheLookupResult::setStatus(QCNetworkCacheLookupStatus status)
{
    d->status = status;
}

QCNetworkCacheMetadata QCNetworkCacheLookupResult::metadata() const
{
    return d->metadata;
}

void QCNetworkCacheLookupResult::setMetadata(const QCNetworkCacheMetadata &metadata)
{
    d->metadata = metadata;
}

QByteArray QCNetworkCacheLookupResult::body() const
{
    return d->body;
}

void QCNetworkCacheLookupResult::setBody(const QByteArray &body)
{
    d->body = body;
}

bool QCNetworkCacheLookupResult::hit() const
{
    return d->status != QCNetworkCacheLookupStatus::Miss;
}

QCNetworkCacheClearResult::QCNetworkCacheClearResult()
    : d(new QCNetworkCacheClearResultData)
{}

QCNetworkCacheClearResult::QCNetworkCacheClearResult(
    const QCNetworkCacheClearResult &other) = default;

QCNetworkCacheClearResult::QCNetworkCacheClearResult(
    QCNetworkCacheClearResult &&other) noexcept = default;

QCNetworkCacheClearResult::~QCNetworkCacheClearResult() = default;

QCNetworkCacheClearResult &QCNetworkCacheClearResult::operator=(
    const QCNetworkCacheClearResult &other) = default;

QCNetworkCacheClearResult &QCNetworkCacheClearResult::operator=(
    QCNetworkCacheClearResult &&other) noexcept = default;

QCNetworkCacheClearResult QCNetworkCacheClearResult::success(qint64 removedCount,
                                                             qint64 remainingBytes)
{
    QCNetworkCacheClearResult result;
    result.d->removedCount   = qMax<qint64>(0, removedCount);
    result.d->remainingBytes = qMax<qint64>(0, remainingBytes);
    return result;
}

QCNetworkCacheClearResult QCNetworkCacheClearResult::partialFailure(qint64 removedCount,
                                                                    qint64 failedCount,
                                                                    qint64 remainingBytes,
                                                                    ErrorCode errorCode,
                                                                    const QString &errorMessage)
{
    QCNetworkCacheClearResult result;
    result.d->status         = Status::PartialFailure;
    result.d->removedCount   = qMax<qint64>(0, removedCount);
    result.d->failedCount    = qMax<qint64>(1, failedCount);
    result.d->remainingBytes = qMax<qint64>(0, remainingBytes);
    result.d->errorCode = errorCode == ErrorCode::None ? ErrorCode::EntryRemovalFailed : errorCode;
    result.d->errorMessage = errorMessage.isEmpty()
                                 ? QStringLiteral("one or more cache entries could not be removed")
                                 : errorMessage;
    return result;
}

QCNetworkCacheClearResult QCNetworkCacheClearResult::failure(qint64 failedCount,
                                                             qint64 remainingBytes,
                                                             ErrorCode errorCode,
                                                             const QString &errorMessage)
{
    QCNetworkCacheClearResult result;
    result.d->status         = Status::Failure;
    result.d->failedCount    = qMax<qint64>(1, failedCount);
    result.d->remainingBytes = qMax<qint64>(0, remainingBytes);
    result.d->errorCode = errorCode == ErrorCode::None ? ErrorCode::EntryRemovalFailed : errorCode;
    result.d->errorMessage = errorMessage.isEmpty()
                                 ? QStringLiteral("cache entries could not be removed")
                                 : errorMessage;
    return result;
}

QCNetworkCacheClearResult::Status QCNetworkCacheClearResult::status() const noexcept
{
    return d->status;
}

bool QCNetworkCacheClearResult::isSuccess() const noexcept
{
    return d->status == Status::Success;
}

qint64 QCNetworkCacheClearResult::removedCount() const noexcept
{
    return d->removedCount;
}

qint64 QCNetworkCacheClearResult::failedCount() const noexcept
{
    return d->failedCount;
}

qint64 QCNetworkCacheClearResult::remainingBytes() const noexcept
{
    return d->remainingBytes;
}

QCNetworkCacheClearResult::ErrorCode QCNetworkCacheClearResult::errorCode() const noexcept
{
    return d->errorCode;
}

QString QCNetworkCacheClearResult::errorMessage() const
{
    return d->errorMessage;
}

} // namespace QCurl
