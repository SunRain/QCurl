// SPDX-License-Identifier: MIT
// Copyright (c) 2025 QCurl Project

#include "QCNetworkDefaultLogger.h"

#include <QDebug>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QMutex>
#include <QQueue>
#include <QSet>
#include <QTextStream>
#include <QThread>
#include <QWaitCondition>

namespace QCurl {

namespace {

constexpr qint64 kKiB                     = 1024;
constexpr qint64 kMiB                     = kKiB * kKiB;
constexpr qint64 kDefaultLogFileSizeBytes = 10 * kMiB;
constexpr int kDefaultBackupCount         = 5;
constexpr int kMaxInMemoryLogEntries      = 10000;

struct LoggerOutputSnapshot
{
    bool consoleEnabled = true;
    QString filePath;
    qint64 maxFileSize = kDefaultLogFileSizeBytes;
    int backupCount    = kDefaultBackupCount;
    QString format;
    std::function<void(const NetworkLogEntry &)> callback;
};

/// 不可变日志记录把状态快照与后续外部副作用隔离。
struct LoggerRecord
{
    quint64 sequence = 0;
    NetworkLogEntry entry;
    LoggerOutputSnapshot output;
};

QString formatRecord(const LoggerRecord &record)
{
    QString result = record.output.format;
    result.replace(QStringLiteral("%{level}"), logLevelToString(record.entry.level()));
    result.replace(QStringLiteral("%{time}"),
                   record.entry.timestampUtc().toString(Qt::ISODateWithMs));
    result.replace(QStringLiteral("%{category}"), record.entry.category());
    result.replace(QStringLiteral("%{message}"), record.entry.message());
    return result;
}

QCNetworkLogResult removeRotationTarget(const QString &path)
{
    const QFileInfo info(path);
    if (!info.exists()) {
        return {};
    }
    if (!info.isFile() || !QFile::remove(path)) {
        return QCNetworkLogResult::fromStatus(QCNetworkLogResult::Status::RemoveFailed,
                                              QStringLiteral("无法删除日志轮转目标：%1").arg(path));
    }
    return {};
}

QCNetworkLogResult removeCurrentLogFile(const QString &path)
{
    const QFileInfo info(path);
    if (!info.exists()) {
        return {};
    }
    if (!info.isFile() || !QFile::remove(path)) {
        return QCNetworkLogResult::fromStatus(QCNetworkLogResult::Status::RemoveFailed,
                                              QStringLiteral("无法删除已达到轮转阈值的日志文件：%1")
                                                  .arg(path));
    }
    return {};
}

QCNetworkLogResult renameRotationSource(const QString &source, const QString &target)
{
    const QFileInfo sourceInfo(source);
    if (!sourceInfo.exists()) {
        return {};
    }
    if (!sourceInfo.isFile() || !QFile::rename(source, target)) {
        return QCNetworkLogResult::fromStatus(QCNetworkLogResult::Status::RenameFailed,
                                              QStringLiteral("无法轮转日志文件：%1 -> %2")
                                                  .arg(source, target));
    }
    return {};
}

QCNetworkLogResult rotateLogFile(const LoggerOutputSnapshot &output)
{
    for (int i = output.backupCount - 1; i > 0; --i) {
        const QString oldFile = QStringLiteral("%1.%2").arg(output.filePath, QString::number(i));
        const QString newFile = QStringLiteral("%1.%2").arg(output.filePath, QString::number(i + 1));
        const QCNetworkLogResult removeResult = removeRotationTarget(newFile);
        if (!removeResult.isSuccess()) {
            return removeResult;
        }
        const QCNetworkLogResult renameResult = renameRotationSource(oldFile, newFile);
        if (!renameResult.isSuccess()) {
            return renameResult;
        }
    }

    if (output.backupCount == 0) {
        return removeCurrentLogFile(output.filePath);
    }

    const QString backupFile              = QStringLiteral("%1.1").arg(output.filePath);
    const QCNetworkLogResult removeResult = removeRotationTarget(backupFile);
    if (!removeResult.isSuccess()) {
        return removeResult;
    }
    return renameRotationSource(output.filePath, backupFile);
}

QCNetworkLogResult writeRecordToFile(const LoggerRecord &record, const QString &text)
{
    const LoggerOutputSnapshot &output = record.output;
    if (output.filePath.isEmpty()) {
        return {};
    }

    const QFileInfo fileInfo(output.filePath);
    const qint64 nextRecordSize = text.toUtf8().size() + 1;
    if (fileInfo.exists() && output.maxFileSize > 0
        && fileInfo.size() + nextRecordSize > output.maxFileSize) {
        const QCNetworkLogResult rotationResult = rotateLogFile(output);
        if (!rotationResult.isSuccess()) {
            return rotationResult;
        }
    }

    QFile file(output.filePath);
    if (!file.open(QIODevice::Append | QIODevice::Text)) {
        return QCNetworkLogResult::fromStatus(QCNetworkLogResult::Status::OpenFailed,
                                              file.errorString());
    }

    QTextStream stream(&file);
    stream << text << "\n";
    stream.flush();
    if (stream.status() != QTextStream::Ok || !file.flush()) {
        return QCNetworkLogResult::fromStatus(QCNetworkLogResult::Status::WriteFailed,
                                              file.errorString());
    }
    return {};
}

QCNetworkLogResult writeRecordToConsole(const LoggerRecord &record, const QString &text)
{
    if (!record.output.consoleEnabled) {
        return {};
    }

    switch (record.entry.level()) {
        case NetworkLogLevel::Debug:
            qDebug().noquote() << text;
            break;
        case NetworkLogLevel::Info:
            qInfo().noquote() << text;
            break;
        case NetworkLogLevel::Warning:
            qWarning().noquote() << text;
            break;
        case NetworkLogLevel::Error:
            qCritical().noquote() << text;
            break;
    }
    return {};
}

QCNetworkLogResult writeRecord(const LoggerRecord &record)
{
    const QString formatted                = formatRecord(record);
    const QCNetworkLogResult consoleResult = writeRecordToConsole(record, formatted);
    const QCNetworkLogResult fileResult    = writeRecordToFile(record, formatted);
    if (record.output.callback) {
        record.output.callback(record.entry);
    }
    return consoleResult.isSuccess() ? fileResult : consoleResult;
}

} // namespace

/**
 * @brief 保存默认日志器的输出配置、回调和内存日志。
 *
 * stateMutex 只保护状态与快照；writerMutex 只保护串行输出队列，二者均不
 * 覆盖格式化、I/O、Qt logging 或用户回调。
 */
class QCNetworkDefaultLoggerPrivate
{
public:
    enum class WriterRole : quint8 {
        Drain,
        Wait,
        Reentrant,
    };

    NetworkLogLevel minLevel = NetworkLogLevel::Info;
    bool enableConsole       = true;
    QString logFile;
    qint64 maxFileSize = kDefaultLogFileSizeBytes;
    int backupCount    = kDefaultBackupCount;
    QString logFormat  = QStringLiteral("%{time} [%{level}] %{category}: %{message}");
    std::function<void(const NetworkLogEntry &)> customCallback;
    QList<NetworkLogEntry> entries; ///< 有界内存日志，超过上限时淘汰最早条目。
    quint64 nextSequence = 0;
    mutable QMutex stateMutex; ///< 保护配置、回调、序号和内存日志。

    WriterRole enqueueRecord(LoggerRecord record)
    {
        QMutexLocker locker(&writerMutex);
        pendingRecords.enqueue(std::move(record));
        if (!writerActive) {
            writerActive       = true;
            activeWriterThread = QThread::currentThread();
            return WriterRole::Drain;
        }
        if (activeWriterThread == QThread::currentThread()) {
            return WriterRole::Reentrant;
        }
        return WriterRole::Wait;
    }

    void drainRecords()
    {
        while (true) {
            LoggerRecord record;
            {
                QMutexLocker locker(&writerMutex);
                if (pendingRecords.isEmpty()) {
                    writerActive       = false;
                    activeWriterThread = nullptr;
                    writerCondition.wakeAll();
                    return;
                }
                record = pendingRecords.dequeue();
            }

            const QCNetworkLogResult result = writeRecord(record);

            QMutexLocker locker(&writerMutex);
            lastWrittenSequence = record.sequence;
            completedResults.insert(record.sequence, result);
            if (unobservedSequences.remove(record.sequence)) {
                completedResults.remove(record.sequence);
            }
            writerCondition.wakeAll();
        }
    }

    QCNetworkLogResult takeResult(quint64 sequence)
    {
        QMutexLocker locker(&writerMutex);
        while (lastWrittenSequence < sequence) {
            writerCondition.wait(&writerMutex);
        }
        return completedResults.take(sequence);
    }

    void discardResult(quint64 sequence)
    {
        QMutexLocker locker(&writerMutex);
        if (lastWrittenSequence >= sequence) {
            completedResults.remove(sequence);
        } else {
            unobservedSequences.insert(sequence);
        }
    }

private:
    QMutex writerMutex;
    QWaitCondition writerCondition;
    QQueue<LoggerRecord> pendingRecords;
    QHash<quint64, QCNetworkLogResult> completedResults;
    QSet<quint64> unobservedSequences;
    quint64 lastWrittenSequence = 0;
    QThread *activeWriterThread = nullptr;
    bool writerActive           = false;
};

QCNetworkDefaultLogger::QCNetworkDefaultLogger()
    : d_ptr(new QCNetworkDefaultLoggerPrivate)
{}

QCNetworkDefaultLogger::~QCNetworkDefaultLogger() = default;

void QCNetworkDefaultLogger::enableConsoleOutput(bool enable)
{
    Q_D(QCNetworkDefaultLogger);
    QMutexLocker locker(&d->stateMutex);
    d->enableConsole = enable;
}

QCNetworkLogResult QCNetworkDefaultLogger::enableFileOutput(const QString &filePath,
                                                            qint64 maxSize,
                                                            int backupCount)
{
    if (filePath.isEmpty()) {
        return QCNetworkLogResult::fromStatus(QCNetworkLogResult::Status::InvalidArgument,
                                              QStringLiteral("日志文件路径不能为空"));
    }
    if (maxSize < 0) {
        return QCNetworkLogResult::fromStatus(QCNetworkLogResult::Status::InvalidArgument,
                                              QStringLiteral("日志文件轮转阈值不能为负数"));
    }
    if (backupCount < 0) {
        return QCNetworkLogResult::fromStatus(QCNetworkLogResult::Status::InvalidArgument,
                                              QStringLiteral("日志备份数量不能为负数"));
    }

    Q_D(QCNetworkDefaultLogger);
    QMutexLocker locker(&d->stateMutex);
    d->logFile     = filePath;
    d->maxFileSize = maxSize > 0 ? maxSize : kDefaultLogFileSizeBytes;
    d->backupCount = backupCount;
    return {};
}

void QCNetworkDefaultLogger::disableFileOutput()
{
    Q_D(QCNetworkDefaultLogger);
    QMutexLocker locker(&d->stateMutex);
    d->logFile.clear();
}

void QCNetworkDefaultLogger::setCustomCallback(std::function<void(const NetworkLogEntry &)> callback)
{
    Q_D(QCNetworkDefaultLogger);
    QMutexLocker locker(&d->stateMutex);
    d->customCallback = std::move(callback);
}

void QCNetworkDefaultLogger::setLogFormat(const QString &format)
{
    Q_D(QCNetworkDefaultLogger);
    QMutexLocker locker(&d->stateMutex);
    d->logFormat = format;
}

void QCNetworkDefaultLogger::setMinLogLevel(NetworkLogLevel level)
{
    Q_D(QCNetworkDefaultLogger);
    QMutexLocker locker(&d->stateMutex);
    d->minLevel = level;
}

NetworkLogLevel QCNetworkDefaultLogger::minLogLevel() const
{
    Q_D(const QCNetworkDefaultLogger);
    QMutexLocker locker(&d->stateMutex);
    return d->minLevel;
}

void QCNetworkDefaultLogger::clear()
{
    Q_D(QCNetworkDefaultLogger);
    QMutexLocker locker(&d->stateMutex);
    d->entries.clear();
}

QList<NetworkLogEntry> QCNetworkDefaultLogger::entries() const
{
    Q_D(const QCNetworkDefaultLogger);
    QMutexLocker locker(&d->stateMutex);
    return d->entries;
}

QCNetworkLogResult QCNetworkDefaultLogger::log(const NetworkLogEntry &entry)
{
    Q_D(QCNetworkDefaultLogger);
    quint64 sequence = 0;
    QCNetworkDefaultLoggerPrivate::WriterRole writerRole;
    {
        QMutexLocker locker(&d->stateMutex);
        if (entry.level() < d->minLevel) {
            return QCNetworkLogResult::fromStatus(QCNetworkLogResult::Status::Filtered);
        }

        d->entries.append(entry);
        if (d->entries.size() > kMaxInMemoryLogEntries) {
            d->entries.removeFirst();
        }

        sequence = ++d->nextSequence;
        LoggerOutputSnapshot output{d->enableConsole,
                                    d->logFile,
                                    d->maxFileSize,
                                    d->backupCount,
                                    d->logFormat,
                                    d->customCallback};
        writerRole = d->enqueueRecord(LoggerRecord{sequence, entry, std::move(output)});
    }

    if (writerRole == QCNetworkDefaultLoggerPrivate::WriterRole::Drain) {
        d->drainRecords();
        return d->takeResult(sequence);
    }
    if (writerRole == QCNetworkDefaultLoggerPrivate::WriterRole::Wait) {
        return d->takeResult(sequence);
    }
    d->discardResult(sequence);
    return QCNetworkLogResult::fromStatus(QCNetworkLogResult::Status::Queued);
}

} // namespace QCurl
