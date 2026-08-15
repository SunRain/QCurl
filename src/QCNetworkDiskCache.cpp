#include "QCNetworkDiskCache.h"

#include "private/QCNetworkCacheKey_p.h"
#include "private/QCNetworkDiskCacheEntry_p.h"

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMutex>
#include <QMutexLocker>
#include <QStandardPaths>

#include <algorithm>

namespace QCurl {

/**
 * @brief 保存磁盘缓存目录、容量统计和条目索引操作的内部状态。
 */
class QCNetworkDiskCachePrivate
{
public:
    mutable QMutex mutex;
    QString cacheDir;
    qint64 maxSize             = 50 * 1024 * 1024;
    mutable qint64 currentSize = -1;

    [[nodiscard]] QString entryPath(const QString &entryId) const
    {
        return QDir(cacheDir).filePath(entryId + QStringLiteral(".qce"));
    }

    [[nodiscard]] QFileInfoList entryFiles(const QString &pattern = QStringLiteral("*.qce")) const
    {
        return QDir(cacheDir).entryInfoList({pattern}, QDir::Files, QDir::Time | QDir::Reversed);
    }

    [[nodiscard]] QFileInfoList allCacheFiles() const
    {
        return QDir(cacheDir).entryInfoList({QStringLiteral("*.qce"),
                                             QStringLiteral("*.data"),
                                             QStringLiteral("*.meta")},
                                            QDir::Files);
    }

    [[nodiscard]] bool ensureCacheDirectory() { return QDir().mkpath(cacheDir); }

    void removeLegacyEntries()
    {
        const QFileInfoList legacyFiles = QDir(cacheDir).entryInfoList({QStringLiteral("*.data"),
                                                                        QStringLiteral("*.meta")},
                                                                       QDir::Files);
        for (const QFileInfo &file : legacyFiles) {
            (void)Internal::removeDiskCacheFile(file.filePath());
        }
    }

    [[nodiscard]] std::optional<Internal::QCNetworkDiskCacheEntry> readEntry(
        const QFileInfo &fileInfo, bool invalidateSizeOnCorruption = true) const
    {
        const auto entry = Internal::readDiskCacheEntry(fileInfo.filePath());
        if (!entry.has_value()) {
            if (invalidateSizeOnCorruption) {
                currentSize = -1;
            }
        }
        return entry;
    }

    void updateCacheSize() const
    {
        if (currentSize >= 0) {
            return;
        }

        qint64 scannedSize = 0;
        for (const QFileInfo &file : entryFiles()) {
            const auto entry = readEntry(file, false);
            if (entry.has_value()) {
                scannedSize += entry->body.size();
            }
        }
        currentSize = scannedSize;
    }

    void removeEntryFile(const QFileInfo &fileInfo)
    {
        const auto entry  = Internal::readDiskCacheEntry(fileInfo.filePath());
        const qint64 size = entry.has_value() ? entry->body.size() : 0;
        if (Internal::removeDiskCacheFile(fileInfo.filePath()) && currentSize >= 0) {
            currentSize = qMax<qint64>(0, currentSize - size);
        }
    }

    void evictIfNeeded(qint64 additionalSize, const QString &excludedPath = {})
    {
        updateCacheSize();
        if (currentSize + additionalSize <= maxSize) {
            return;
        }

        for (const QFileInfo &file : entryFiles()) {
            if (file.filePath() == excludedPath) {
                continue;
            }
            removeEntryFile(file);
            if (currentSize + additionalSize <= maxSize) {
                break;
            }
        }
    }

    [[nodiscard]] std::optional<Internal::QCNetworkDiskCacheEntry> matchingEntry(
        const QCNetworkCacheRequestKey &key, QString *matchedPath = nullptr) const
    {
        const QByteArray primary = Internal::cachePrimaryDigest(key);
        const QString pattern    = QString::fromLatin1(primary.toHex()) + QStringLiteral("-*.qce");
        std::optional<Internal::QCNetworkDiskCacheEntry> matched;
        qint64 matchedTimestamp = std::numeric_limits<qint64>::min();
        QDateTime matchedFileTime;
        QString selectedPath;
        for (const QFileInfo &file : entryFiles(pattern)) {
            const auto entry = readEntry(file);
            if (!entry.has_value() || entry->primaryDigest != primary
                || !Internal::cacheVaryDimensionsAvailable(key, entry->varyHeaderNames)
                || entry->variantDigest
                       != Internal::cacheVariantDigest(key, entry->varyHeaderNames)) {
                continue;
            }

            const qint64 timestamp   = Internal::cacheResponseSelectionTimestamp(entry->metadata);
            const QDateTime fileTime = file.lastModified();
            if (!matched.has_value() || timestamp > matchedTimestamp
                || (timestamp == matchedTimestamp && fileTime > matchedFileTime)) {
                matched          = entry;
                matchedTimestamp = timestamp;
                matchedFileTime  = fileTime;
                selectedPath     = file.filePath();
            }
        }
        if (matchedPath && matched.has_value()) {
            *matchedPath = selectedPath;
        }
        return matched;
    }
};

QCNetworkDiskCache::QCNetworkDiskCache(QObject *parent)
    : QCNetworkCache(parent)
    , d_ptr(new QCNetworkDiskCachePrivate)
{
    d_ptr->cacheDir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation)
                      + QStringLiteral("/QCurl");
    if (!d_ptr->ensureCacheDirectory()) {
        qWarning() << "QCNetworkDiskCache: cannot create cache directory" << d_ptr->cacheDir;
    }
    d_ptr->removeLegacyEntries();
}

QCNetworkDiskCache::~QCNetworkDiskCache() = default;

void QCNetworkDiskCache::setCacheDirectory(const QString &path)
{
    QMutexLocker locker(&d_ptr->mutex);
    d_ptr->cacheDir    = path;
    d_ptr->currentSize = -1;
    if (!d_ptr->ensureCacheDirectory()) {
        qWarning() << "QCNetworkDiskCache: cannot create cache directory" << d_ptr->cacheDir;
    }
    d_ptr->removeLegacyEntries();
}

QString QCNetworkDiskCache::cacheDirectory() const
{
    QMutexLocker locker(&d_ptr->mutex);
    return d_ptr->cacheDir;
}

QCNetworkCacheLookupResult QCNetworkDiskCache::lookup(const QCNetworkCacheRequestKey &key,
                                                      QCNetworkCacheReadMode mode)
{
    QMutexLocker locker(&d_ptr->mutex);
    if (key.hasAuthenticationContext() && key.cachePartitionKey().isEmpty()) {
        return {};
    }

    QString matchedPath;
    const auto entry = d_ptr->matchingEntry(key, &matchedPath);
    if (!entry.has_value()) {
        return {};
    }

    const bool fresh = entry->metadata.isValid();
    if (!fresh && mode == QCNetworkCacheReadMode::FreshOnly) {
        return {};
    }

    QFile matchedFile(matchedPath);
    if (matchedFile.open(QIODevice::ReadOnly)) {
        matchedFile.setFileTime(QDateTime::currentDateTimeUtc(), QFileDevice::FileModificationTime);
    }

    QCNetworkCacheLookupResult result;
    result.setStatus(fresh ? QCNetworkCacheLookupStatus::FreshHit
                           : QCNetworkCacheLookupStatus::StaleHit);
    result.setMetadata(entry->metadata);
    result.setBody(entry->body);
    return result;
}

void QCNetworkDiskCache::insert(const QCNetworkCacheRequestKey &key,
                                const QByteArray &data,
                                const QCNetworkCacheMetadata &meta)
{
    QMutexLocker locker(&d_ptr->mutex);
    if (!Internal::cacheEntryMayBeStored(key, meta) || data.size() > d_ptr->maxSize
        || !d_ptr->ensureCacheDirectory()) {
        return;
    }

    Internal::QCNetworkDiskCacheEntry entry;
    entry.varyHeaderNames = Internal::normalizedVaryHeaderNames(meta.varyHeaderNames());
    entry.primaryDigest   = Internal::cachePrimaryDigest(key);
    entry.variantDigest   = Internal::cacheVariantDigest(key, entry.varyHeaderNames);
    entry.metadata        = meta;
    entry.metadata.setUrl(key.normalizedUrl());
    entry.metadata.setSize(data.size());
    entry.metadata.setCreationDate(QDateTime::currentDateTime());
    entry.metadata.setVaryHeaderNames(entry.varyHeaderNames);
    entry.body = data;

    const QString entryId = Internal::cacheEntryId(key, entry.varyHeaderNames);
    const QString path    = d_ptr->entryPath(entryId);
    const auto previous   = Internal::readDiskCacheEntry(path);
    const qint64 oldSize  = previous.has_value() ? previous->body.size() : 0;
    d_ptr->evictIfNeeded(qMax<qint64>(0, data.size() - oldSize), path);

    if (!Internal::writeDiskCacheEntry(path, entry)) {
        return;
    }
    d_ptr->updateCacheSize();
    d_ptr->currentSize += data.size() - oldSize;
}

bool QCNetworkDiskCache::remove(const QCNetworkCacheRequestKey &key)
{
    QMutexLocker locker(&d_ptr->mutex);
    d_ptr->updateCacheSize();
    const QByteArray primary = Internal::cachePrimaryDigest(key);
    const QString pattern    = QString::fromLatin1(primary.toHex()) + QStringLiteral("-*.qce");
    bool removed             = false;
    for (const QFileInfo &file : d_ptr->entryFiles(pattern)) {
        const auto entry = d_ptr->readEntry(file);
        if (!entry.has_value()
            || entry->variantDigest != Internal::cacheVariantDigest(key, entry->varyHeaderNames)) {
            continue;
        }
        d_ptr->removeEntryFile(file);
        removed = true;
    }
    return removed;
}

QCNetworkCacheClearResult QCNetworkDiskCache::clear()
{
    QMutexLocker locker(&d_ptr->mutex);
    const QFileInfo directoryInfo(d_ptr->cacheDir);
    if (directoryInfo.exists() && !directoryInfo.isDir()) {
        return QCNetworkCacheClearResult::failure(
            1,
            qMax<qint64>(0, d_ptr->currentSize),
            QCNetworkCacheClearResult::ErrorCode::CacheDirectoryUnavailable,
            QStringLiteral("cache directory is unavailable"));
    }
    if (!directoryInfo.exists()) {
        d_ptr->currentSize = 0;
        return QCNetworkCacheClearResult::success(0, 0);
    }

    qint64 removedCount = 0;
    qint64 failedCount  = 0;
    for (const QFileInfo &file : d_ptr->allCacheFiles()) {
        if (Internal::removeDiskCacheFile(file.filePath())) {
            ++removedCount;
        } else {
            ++failedCount;
        }
    }

    d_ptr->currentSize = -1;
    d_ptr->updateCacheSize();
    if (failedCount == 0) {
        return QCNetworkCacheClearResult::success(removedCount, d_ptr->currentSize);
    }
    if (removedCount > 0) {
        return QCNetworkCacheClearResult::partialFailure(
            removedCount,
            failedCount,
            d_ptr->currentSize,
            QCNetworkCacheClearResult::ErrorCode::EntryRemovalFailed,
            QStringLiteral("one or more cache entries could not be removed"));
    }
    return QCNetworkCacheClearResult::failure(failedCount,
                                              d_ptr->currentSize,
                                              QCNetworkCacheClearResult::ErrorCode::EntryRemovalFailed,
                                              QStringLiteral("cache entries could not be removed"));
}

qint64 QCNetworkDiskCache::cacheSize() const
{
    QMutexLocker locker(&d_ptr->mutex);
    d_ptr->updateCacheSize();
    return d_ptr->currentSize;
}

qint64 QCNetworkDiskCache::maxCacheSize() const
{
    QMutexLocker locker(&d_ptr->mutex);
    return d_ptr->maxSize;
}

void QCNetworkDiskCache::setMaxCacheSize(qint64 size)
{
    QMutexLocker locker(&d_ptr->mutex);
    d_ptr->maxSize = qMax<qint64>(0, size);
    d_ptr->evictIfNeeded(0);
}

} // namespace QCurl
