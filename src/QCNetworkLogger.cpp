// SPDX-License-Identifier: MIT
// Copyright (c) 2025 QCurl Project

#include "QCNetworkLogger.h"
#include <QFile>
#include <QTextStream>
#include <QDebug>
#include <QMutex>
#include <QVector>

namespace QCurl {

// ============================================================================
// NetworkLogEntry Implementation
// ============================================================================

QString NetworkLogEntry::toJson() const
{
    return QString(R"({"level":"%1","category":"%2","message":"%3","timestamp":"%4"})")
        .arg(logLevelToString(level), category, message, timestamp.toString(Qt::ISODate));
}

QString NetworkLogEntry::toPlainText() const
{
    return QString("%1 [%2] %3: %4")
        .arg(timestamp.toString("yyyy-MM-dd hh:mm:ss.zzz"),
             logLevelToString(level),
             category,
             message);
}

// ============================================================================
// Utility Functions
// ============================================================================

QString logLevelToString(NetworkLogLevel level)
{
    switch (level) {
    case NetworkLogLevel::Debug:    return "DEBUG";
    case NetworkLogLevel::Info:     return "INFO";
    case NetworkLogLevel::Warning:  return "WARN";
    case NetworkLogLevel::Error:    return "ERROR";
    }
    return "UNKNOWN";
}

NetworkLogLevel stringToLogLevel(const QString &str)
{
    if (str == "DEBUG") return NetworkLogLevel::Debug;
    if (str == "INFO") return NetworkLogLevel::Info;
    if (str == "WARN" || str == "WARNING") return NetworkLogLevel::Warning;
    if (str == "ERROR") return NetworkLogLevel::Error;
    return NetworkLogLevel::Info;
}

// ============================================================================
// QCNetworkDefaultLogger::Private
// ============================================================================

class QCNetworkDefaultLogger::Private
{
public:
    NetworkLogLevel minLevel = NetworkLogLevel::Info;
    bool enableConsole = true;
    QString logFile;
    qint64 maxFileSize = 10 * 1024 * 1024; // 10MB
    int backupCount = 5;
    QString logFormat = "%{time} [%{level}] %{category}: %{message}";
    std::function<void(const NetworkLogEntry &)> customCallback;
    QVector<NetworkLogEntry> entries;
    QMutex mutex;
    
    QString formatLog(const NetworkLogEntry &entry) const
    {
        QString result = logFormat;
        result.replace("%{level}", logLevelToString(entry.level));
        result.replace("%{time}", entry.timestamp.toString("yyyy-MM-dd hh:mm:ss.zzz"));
        result.replace("%{category}", entry.category);
        result.replace("%{message}", entry.message);
        return result;
    }
    
    void writeToFile(const QString &text)
    {
        if (logFile.isEmpty()) return;
        
        QFile file(logFile);
        if (file.size() > maxFileSize && maxFileSize > 0) {
            // Rotate log files
            for (int i = backupCount - 1; i > 0; --i) {
                QString oldFile = QString("%1.%2").arg(logFile, QString::number(i));
                QString newFile = QString("%1.%2").arg(logFile, QString::number(i + 1));
                QFile::remove(newFile);
                QFile::rename(oldFile, newFile);
            }
            QString backupFile = QString("%1.1").arg(logFile);
            QFile::remove(backupFile);
            QFile::rename(logFile, backupFile);
        }
        
        if (file.open(QIODevice::Append | QIODevice::Text)) {
            QTextStream stream(&file);
            stream << text << "\n";
            file.close();
        }
    }
};

// ============================================================================
// QCNetworkDefaultLogger Implementation
// ============================================================================

QCNetworkDefaultLogger::QCNetworkDefaultLogger()
    : d_ptr(std::make_unique<Private>())
{
}

QCNetworkDefaultLogger::~QCNetworkDefaultLogger() = default;

void QCNetworkDefaultLogger::enableConsoleOutput(bool enable)
{
    QMutexLocker locker(&d_ptr->mutex);
    d_ptr->enableConsole = enable;
}

void QCNetworkDefaultLogger::enableFileOutput(const QString &filePath, qint64 maxSize, int backupCount)
{
    QMutexLocker locker(&d_ptr->mutex);
    d_ptr->logFile = filePath;
    d_ptr->maxFileSize = maxSize > 0 ? maxSize : (10 * 1024 * 1024);
    d_ptr->backupCount = backupCount;
}

void QCNetworkDefaultLogger::disableFileOutput()
{
    QMutexLocker locker(&d_ptr->mutex);
    d_ptr->logFile.clear();
}

void QCNetworkDefaultLogger::setCustomCallback(std::function<void(const NetworkLogEntry &)> callback)
{
    QMutexLocker locker(&d_ptr->mutex);
    d_ptr->customCallback = callback;
}

void QCNetworkDefaultLogger::setLogFormat(const QString &format)
{
    QMutexLocker locker(&d_ptr->mutex);
    d_ptr->logFormat = format;
}

void QCNetworkDefaultLogger::log(NetworkLogLevel level, const QString &category, const QString &message)
{
    QMutexLocker locker(&d_ptr->mutex);
    
    if (level < d_ptr->minLevel) {
        return;
    }
    
    NetworkLogEntry entry;
    entry.level = level;
    entry.category = category;
    entry.message = message;
    entry.timestamp = QDateTime::currentDateTime();
    
    // Store entry
    d_ptr->entries.append(entry);
    if (d_ptr->entries.size() > 10000) {
        d_ptr->entries.removeFirst();
    }
    
    QString formatted = d_ptr->formatLog(entry);
    
    // Console output
    if (d_ptr->enableConsole) {
        switch (level) {
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
    
    // File output
    if (!d_ptr->logFile.isEmpty()) {
        d_ptr->writeToFile(formatted);
    }
    
    // Custom callback
    if (d_ptr->customCallback) {
        d_ptr->customCallback(entry);
    }
}

void QCNetworkDefaultLogger::setMinLogLevel(NetworkLogLevel level)
{
    QMutexLocker locker(&d_ptr->mutex);
    d_ptr->minLevel = level;
}

NetworkLogLevel QCNetworkDefaultLogger::minLogLevel() const
{
    QMutexLocker locker(&d_ptr->mutex);
    return d_ptr->minLevel;
}

void QCNetworkDefaultLogger::clear()
{
    QMutexLocker locker(&d_ptr->mutex);
    d_ptr->entries.clear();
}

QList<NetworkLogEntry> QCNetworkDefaultLogger::entries() const
{
    QMutexLocker locker(&d_ptr->mutex);
    return QList<NetworkLogEntry>(d_ptr->entries.begin(), d_ptr->entries.end());
}

} // namespace QCurl
