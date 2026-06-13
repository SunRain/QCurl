// SPDX-License-Identifier: MIT
// Copyright (c) 2025 QCurl Project

#include "QCNetworkDefaultLogger.h"

#include <QDebug>
#include <QFile>
#include <QMutex>
#include <QTextStream>

namespace QCurl {

namespace {

constexpr qint64 kKiB                     = 1024;
constexpr qint64 kMiB                     = kKiB * kKiB;
constexpr qint64 kDefaultLogFileSizeBytes = 10 * kMiB;
constexpr int kDefaultBackupCount         = 5;
constexpr int kMaxInMemoryLogEntries      = 10000;

} // namespace

class QCNetworkDefaultLoggerPrivate
{
public:
    NetworkLogLevel minLevel = NetworkLogLevel::Info;
    bool enableConsole       = true;
    QString logFile;
    qint64 maxFileSize = kDefaultLogFileSizeBytes;
    int backupCount    = kDefaultBackupCount;
    QString logFormat  = QStringLiteral("%{time} [%{level}] %{category}: %{message}");
    std::function<void(const NetworkLogEntry &)> customCallback;
    QList<NetworkLogEntry> entries;
    mutable QMutex mutex;

    QString formatLog(const NetworkLogEntry &entry) const
    {
        QString result = logFormat;
        result.replace(QStringLiteral("%{level}"), logLevelToString(entry.level()));
        result.replace(QStringLiteral("%{time}"), entry.timestampUtc().toString(Qt::ISODateWithMs));
        result.replace(QStringLiteral("%{category}"), entry.category());
        result.replace(QStringLiteral("%{message}"), entry.message());
        return result;
    }

    void writeToFile(const QString &text)
    {
        if (logFile.isEmpty()) {
            return;
        }

        QFile file(logFile);
        if (file.size() > maxFileSize && maxFileSize > 0) {
            for (int i = backupCount - 1; i > 0; --i) {
                const QString oldFile = QStringLiteral("%1.%2").arg(logFile, QString::number(i));
                const QString newFile = QStringLiteral("%1.%2").arg(logFile, QString::number(i + 1));
                QFile::remove(newFile);
                QFile::rename(oldFile, newFile);
            }
            const QString backupFile = QStringLiteral("%1.1").arg(logFile);
            QFile::remove(backupFile);
            QFile::rename(logFile, backupFile);
        }

        if (file.open(QIODevice::Append | QIODevice::Text)) {
            QTextStream stream(&file);
            stream << text << "\n";
        }
    }
};

QCNetworkDefaultLogger::QCNetworkDefaultLogger()
    : d_ptr(new QCNetworkDefaultLoggerPrivate)
{}

QCNetworkDefaultLogger::~QCNetworkDefaultLogger() = default;

void QCNetworkDefaultLogger::enableConsoleOutput(bool enable)
{
    Q_D(QCNetworkDefaultLogger);
    QMutexLocker locker(&d->mutex);
    d->enableConsole = enable;
}

void QCNetworkDefaultLogger::enableFileOutput(const QString &filePath,
                                              qint64 maxSize,
                                              int backupCount)
{
    Q_D(QCNetworkDefaultLogger);
    QMutexLocker locker(&d->mutex);
    d->logFile     = filePath;
    d->maxFileSize = maxSize > 0 ? maxSize : kDefaultLogFileSizeBytes;
    d->backupCount = backupCount;
}

void QCNetworkDefaultLogger::disableFileOutput()
{
    Q_D(QCNetworkDefaultLogger);
    QMutexLocker locker(&d->mutex);
    d->logFile.clear();
}

void QCNetworkDefaultLogger::setCustomCallback(std::function<void(const NetworkLogEntry &)> callback)
{
    Q_D(QCNetworkDefaultLogger);
    QMutexLocker locker(&d->mutex);
    d->customCallback = std::move(callback);
}

void QCNetworkDefaultLogger::setLogFormat(const QString &format)
{
    Q_D(QCNetworkDefaultLogger);
    QMutexLocker locker(&d->mutex);
    d->logFormat = format;
}

void QCNetworkDefaultLogger::setMinLogLevel(NetworkLogLevel level)
{
    Q_D(QCNetworkDefaultLogger);
    QMutexLocker locker(&d->mutex);
    d->minLevel = level;
}

NetworkLogLevel QCNetworkDefaultLogger::minLogLevel() const
{
    Q_D(const QCNetworkDefaultLogger);
    QMutexLocker locker(&d->mutex);
    return d->minLevel;
}

void QCNetworkDefaultLogger::clear()
{
    Q_D(QCNetworkDefaultLogger);
    QMutexLocker locker(&d->mutex);
    d->entries.clear();
}

QList<NetworkLogEntry> QCNetworkDefaultLogger::entries() const
{
    Q_D(const QCNetworkDefaultLogger);
    QMutexLocker locker(&d->mutex);
    return d->entries;
}

void QCNetworkDefaultLogger::log(const NetworkLogEntry &entry)
{
    Q_D(QCNetworkDefaultLogger);
    QMutexLocker locker(&d->mutex);

    if (entry.level() < d->minLevel) {
        return;
    }

    d->entries.append(entry);
    if (d->entries.size() > kMaxInMemoryLogEntries) {
        d->entries.removeFirst();
    }

    const QString formatted = d->formatLog(entry);

    if (d->enableConsole) {
        switch (entry.level()) {
            case NetworkLogLevel::Debug:
                qDebug().noquote() << formatted;
                break;
            case NetworkLogLevel::Info:
                qInfo().noquote() << formatted;
                break;
            case NetworkLogLevel::Warning:
                qWarning().noquote() << formatted;
                break;
            case NetworkLogLevel::Error:
                qCritical().noquote() << formatted;
                break;
        }
    }

    if (!d->logFile.isEmpty()) {
        d->writeToFile(formatted);
    }

    if (d->customCallback) {
        d->customCallback(entry);
    }
}

} // namespace QCurl
