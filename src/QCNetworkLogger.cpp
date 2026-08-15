// SPDX-License-Identifier: MIT
// Copyright (c) 2025 QCurl Project

#include "QCNetworkLogger.h"

#include <QSharedData>
#include <QSharedPointer>

#include <utility>

namespace QCurl {

/// @brief 保存单条网络日志的隐式共享值。
class NetworkLogEntryData : public QSharedData
{
public:
    NetworkLogEntryData()
        : level(NetworkLogLevel::Info)
        , category()
        , message()
        , timestampUtc(QDateTime::currentDateTimeUtc())
    {}

    NetworkLogEntryData(NetworkLogLevel logLevel,
                        const QString &logCategory,
                        const QString &logMessage,
                        const QDateTime &utcTimestamp)
        : level(logLevel)
        , category(logCategory)
        , message(logMessage)
        , timestampUtc(utcTimestamp.isValid() ? utcTimestamp.toUTC() : QDateTime())
    {}

    NetworkLogLevel level;
    QString category;
    QString message;
    QDateTime timestampUtc;
};

/**
 * @brief 保存结构化日志结果的隐式共享数据。
 */
class QCNetworkLogResultData : public QSharedData
{
public:
    QCNetworkLogResult::Status status = QCNetworkLogResult::Status::Success;
    QString error;
};

/**
 * @brief 保存 opaque logger handle 的库内共享控制块引用。
 */
class QCNetworkLoggerHandlePrivate
{
public:
    explicit QCNetworkLoggerHandlePrivate(QSharedPointer<QCNetworkLogger> loggerValue)
        : logger(std::move(loggerValue))
    {}

    QSharedPointer<QCNetworkLogger> logger;
};

NetworkLogEntry::NetworkLogEntry()
    : d(new NetworkLogEntryData)
{}

NetworkLogEntry::NetworkLogEntry(NetworkLogLevel level,
                                 const QString &category,
                                 const QString &message,
                                 const QDateTime &timestampUtc)
    : d(new NetworkLogEntryData(level, category, message, timestampUtc))
{}

NetworkLogEntry::NetworkLogEntry(const NetworkLogEntry &other) = default;

NetworkLogEntry::NetworkLogEntry(NetworkLogEntry &&other) noexcept = default;

NetworkLogEntry::~NetworkLogEntry() = default;

NetworkLogEntry &NetworkLogEntry::operator=(const NetworkLogEntry &other) = default;

NetworkLogEntry &NetworkLogEntry::operator=(NetworkLogEntry &&other) noexcept = default;

NetworkLogLevel NetworkLogEntry::level() const
{
    return d->level;
}

QString NetworkLogEntry::category() const
{
    return d->category;
}

QString NetworkLogEntry::message() const
{
    return d->message;
}

QDateTime NetworkLogEntry::timestampUtc() const
{
    return d->timestampUtc;
}

void NetworkLogEntry::setLevel(NetworkLogLevel level)
{
    d->level = level;
}

void NetworkLogEntry::setCategory(const QString &category)
{
    d->category = category;
}

void NetworkLogEntry::setMessage(const QString &message)
{
    d->message = message;
}

void NetworkLogEntry::setTimestampUtc(const QDateTime &timestampUtc)
{
    d->timestampUtc = timestampUtc.isValid() ? timestampUtc.toUTC() : QDateTime();
}

QCNetworkLogResult::QCNetworkLogResult()
    : d(new QCNetworkLogResultData)
{}

QCNetworkLogResult::QCNetworkLogResult(const QCNetworkLogResult &other) = default;

QCNetworkLogResult::QCNetworkLogResult(QCNetworkLogResult &&other) noexcept = default;

QCNetworkLogResult::~QCNetworkLogResult() = default;

QCNetworkLogResult &QCNetworkLogResult::operator=(const QCNetworkLogResult &other) = default;

QCNetworkLogResult &QCNetworkLogResult::operator=(QCNetworkLogResult &&other) noexcept = default;

QCNetworkLogResult QCNetworkLogResult::fromStatus(Status status, const QString &error)
{
    QCNetworkLogResult result;
    result.d->status = status;
    result.d->error  = error;
    return result;
}

QCNetworkLogResult::Status QCNetworkLogResult::status() const noexcept
{
    return d->status;
}

bool QCNetworkLogResult::isSuccess() const noexcept
{
    return d->status == Status::Success;
}

QString QCNetworkLogResult::error() const
{
    return d->error;
}

QCNetworkLogResult QCNetworkLogger::log(NetworkLogLevel level,
                                        const QString &category,
                                        const QString &message)
{
    return log(NetworkLogEntry(level, category, message, QDateTime::currentDateTimeUtc()));
}

QCNetworkLoggerHandle::QCNetworkLoggerHandle() = default;

QCNetworkLoggerHandle::QCNetworkLoggerHandle(const QCNetworkLoggerHandle &other)
    : d_ptr(other.d_ptr ? new QCNetworkLoggerHandlePrivate(other.d_ptr->logger) : nullptr)
{}

QCNetworkLoggerHandle::QCNetworkLoggerHandle(QCNetworkLoggerHandle &&other) noexcept = default;

QCNetworkLoggerHandle::~QCNetworkLoggerHandle() = default;

QCNetworkLoggerHandle &QCNetworkLoggerHandle::operator=(const QCNetworkLoggerHandle &other)
{
    if (this == &other) {
        return *this;
    }

    auto *replacement = other.d_ptr ? new QCNetworkLoggerHandlePrivate(other.d_ptr->logger)
                                    : nullptr;
    d_ptr.reset(replacement);
    return *this;
}

QCNetworkLoggerHandle &QCNetworkLoggerHandle::operator=(QCNetworkLoggerHandle &&other) noexcept
{
    if (this == &other) {
        return *this;
    }

    d_ptr = std::move(other.d_ptr);
    return *this;
}

QCNetworkLogger *QCNetworkLoggerHandle::get() const noexcept
{
    return d_ptr ? d_ptr->logger.data() : nullptr;
}

QCNetworkLogger *QCNetworkLoggerHandle::operator->() const noexcept
{
    return get();
}

QCNetworkLoggerHandle::operator bool() const noexcept
{
    return get() != nullptr;
}

QCNetworkLoggerHandle::QCNetworkLoggerHandle(QCNetworkLoggerHandlePrivate *d)
    : d_ptr(d)
{}

QCNetworkLoggerHandle QCNetworkLoggerHandle::adopt(QCNetworkLogger *logger, DestroyFunction destroy)
{
    if (!logger) {
        return {};
    }
    return QCNetworkLoggerHandle(
        new QCNetworkLoggerHandlePrivate(QSharedPointer<QCNetworkLogger>(logger, destroy)));
}

bool operator==(const QCNetworkLoggerHandle &left, const QCNetworkLoggerHandle &right) noexcept
{
    return left.get() == right.get();
}

bool operator!=(const QCNetworkLoggerHandle &left, const QCNetworkLoggerHandle &right) noexcept
{
    return !(left == right);
}

QString logLevelToString(NetworkLogLevel level)
{
    switch (level) {
        case NetworkLogLevel::Debug:
            return QStringLiteral("DEBUG");
        case NetworkLogLevel::Info:
            return QStringLiteral("INFO");
        case NetworkLogLevel::Warning:
            return QStringLiteral("WARN");
        case NetworkLogLevel::Error:
            return QStringLiteral("ERROR");
    }
    return QStringLiteral("UNKNOWN");
}

} // namespace QCurl
