// SPDX-License-Identifier: MIT
// Copyright (c) 2025 QCurl Project

#include "QCNetworkLogger.h"

#include <QSharedData>

namespace QCurl {

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

void QCNetworkLogger::log(NetworkLogLevel level, const QString &category, const QString &message)
{
    log(NetworkLogEntry(level, category, message, QDateTime::currentDateTimeUtc()));
}

QString logLevelToString(NetworkLogLevel level)
{
    switch (level) {
        case NetworkLogLevel::Debug:
            return "DEBUG";
        case NetworkLogLevel::Info:
            return "INFO";
        case NetworkLogLevel::Warning:
            return "WARN";
        case NetworkLogLevel::Error:
            return "ERROR";
    }
    return "UNKNOWN";
}

} // namespace QCurl
