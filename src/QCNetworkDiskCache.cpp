#include "QCNetworkDiskCache.h"

#include <algorithm>

#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMutex>
#include <QMutexLocker>
#include <QStandardPaths>

namespace QCurl {

/// 磁盘缓存内部状态；所有文件路径和容量统计通过同一锁保护。
class QCNetworkDiskCachePrivate
{
public:
    // 所有磁盘缓存状态均由该锁保护，包括目录、容量和文件索引。
    mutable QMutex mutex;
    QString cacheDir;
    qint64 maxSize             = 50 * 1024 * 1024;
    // -1 表示尚未扫描磁盘，下一次读取容量时再计算。
    mutable qint64 currentSize = -1;

    QString cacheKey(const QUrl &url) const
    {
        const QByteArray hash = QCryptographicHash::hash(url.toString().toUtf8(),
                                                         QCryptographicHash::Md5);
        return QString::fromLatin1(hash.toHex());
    }

    QString dataFilePath(const QString &key) const
    {
        return cacheDir + QStringLiteral("/") + key + QStringLiteral(".data");
    }

    QString metaFilePath(const QString &key) const
    {
        return cacheDir + QStringLiteral("/") + key + QStringLiteral(".meta");
    }

    void ensureCacheDirectory()
    {
        QDir dir;
        if (!dir.exists(cacheDir)) {
            dir.mkpath(cacheDir);
        }
    }

    void updateCacheSize() const
    {
        if (currentSize >= 0) {
            return;
        }

        currentSize = 0;
        QDirIterator it(cacheDir, QStringList() << QStringLiteral("*.data"), QDir::Files);
        while (it.hasNext()) {
            it.next();
            currentSize += it.fileInfo().size();
        }
    }

    bool writeMetadata(const QString &key, const QCNetworkCacheMetadata &meta)
    {
        QJsonObject json;
        json[QStringLiteral("url")]            = meta.url().toString();
        json[QStringLiteral("size")]           = meta.size();
        json[QStringLiteral("creationDate")]   = meta.creationDate().toString(Qt::ISODate);
        json[QStringLiteral("expirationDate")] = meta.expirationDate().toString(Qt::ISODate);
        json[QStringLiteral("lastModified")]   = meta.lastModified().toString(Qt::ISODate);

        QJsonObject headersObj;
        const auto headers = meta.headers();
        for (auto it = headers.constBegin(); it != headers.constEnd(); ++it) {
            headersObj[QString::fromLatin1(it.key())] = QString::fromLatin1(it.value());
        }
        json[QStringLiteral("headers")] = headersObj;

        QFile file(metaFilePath(key));
        if (!file.open(QIODevice::WriteOnly)) {
            return false;
        }

        file.write(QJsonDocument(json).toJson(QJsonDocument::Compact));
        return true;
    }

    QCNetworkCacheMetadata readMetadata(const QString &key) const
    {
        QFile file(metaFilePath(key));
        if (!file.open(QIODevice::ReadOnly)) {
            return QCNetworkCacheMetadata();
        }

        const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
        if (!doc.isObject()) {
            return QCNetworkCacheMetadata();
        }

        const QJsonObject json = doc.object();
        QCNetworkCacheMetadata meta;
        meta.setUrl(QUrl(json[QStringLiteral("url")].toString()));
        meta.setSize(json[QStringLiteral("size")].toInteger());
        meta.setCreationDate(
            QDateTime::fromString(json[QStringLiteral("creationDate")].toString(), Qt::ISODate));
        meta.setExpirationDate(
            QDateTime::fromString(json[QStringLiteral("expirationDate")].toString(), Qt::ISODate));
        meta.setLastModified(
            QDateTime::fromString(json[QStringLiteral("lastModified")].toString(), Qt::ISODate));

        QMap<QByteArray, QByteArray> headers;
        const QJsonObject headersObj = json[QStringLiteral("headers")].toObject();
        for (auto it = headersObj.constBegin(); it != headersObj.constEnd(); ++it) {
            headers[it.key().toLatin1()] = it.value().toString().toLatin1();
        }
        meta.setHeaders(headers);

        return meta;
    }

    void evictIfNeeded(qint64 newDataSize)
    {
        updateCacheSize();

        if (currentSize + newDataSize <= maxSize) {
            return;
        }

        // 以 data 文件的最后修改时间近似 LRU 顺序，避免维护额外索引文件。
        struct CacheFile
        {
            QString key;
            QDateTime lastAccess;
            qint64 size;
        };
        QList<CacheFile> files;

        QDirIterator it(cacheDir, QStringList() << QStringLiteral("*.meta"), QDir::Files);
        while (it.hasNext()) {
            it.next();
            QString key = it.fileInfo().baseName();

            const QCNetworkCacheMetadata meta = readMetadata(key);
            if (!meta.url().isEmpty()) {
                QFileInfo dataInfo(dataFilePath(key));
                files.append({key, dataInfo.lastModified(), dataInfo.size()});
            }
        }

        std::sort(files.begin(), files.end(), [](const CacheFile &a, const CacheFile &b) {
            return a.lastAccess < b.lastAccess;
        });

        for (const CacheFile &file : files) {
            if (currentSize + newDataSize <= maxSize) {
                break;
            }

            QFile::remove(dataFilePath(file.key));
            QFile::remove(metaFilePath(file.key));
            currentSize -= file.size;
        }
    }
};

QCNetworkDiskCache::QCNetworkDiskCache(QObject *parent)
    : QCNetworkCache(parent)
    , d_ptr(new QCNetworkDiskCachePrivate)
{
    // 使用平台默认缓存目录，避免调用方必须先配置路径。
    d_ptr->cacheDir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + "/QCurl";
    d_ptr->ensureCacheDirectory();
}

QCNetworkDiskCache::~QCNetworkDiskCache() {}

void QCNetworkDiskCache::setCacheDirectory(const QString &path)
{
    QMutexLocker locker(&d_ptr->mutex);
    d_ptr->cacheDir    = path;
    d_ptr->currentSize = -1;
    d_ptr->ensureCacheDirectory();
}

QString QCNetworkDiskCache::cacheDirectory() const
{
    QMutexLocker locker(&d_ptr->mutex);
    return d_ptr->cacheDir;
}

QCNetworkCacheLookupResult QCNetworkDiskCache::lookup(const QUrl &url,
                                                      QCNetworkCacheReadMode mode)
{
    QMutexLocker locker(&d_ptr->mutex);

    const QString key = d_ptr->cacheKey(url);
    QCNetworkCacheMetadata meta = d_ptr->readMetadata(key);
    if (meta.url().isEmpty()) {
        return {};
    }

    const QString dataPath = d_ptr->dataFilePath(key);
    QFileInfo dataInfo(dataPath);
    if (!dataInfo.exists()) {
        return {};
    }

    const bool fresh = meta.isValid();
    if (!fresh && mode == QCNetworkCacheReadMode::FreshOnly) {
        // FreshOnly 将过期条目视为未命中，并同步移除磁盘残留。
        if (QFile::remove(dataPath) && d_ptr->currentSize >= 0) {
            d_ptr->currentSize -= dataInfo.size();
        }
        QFile::remove(d_ptr->metaFilePath(key));
        return {};
    }

    QFile file(dataPath);
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }

    QCNetworkCacheLookupResult result;
    result.setStatus(fresh ? QCNetworkCacheLookupStatus::FreshHit
                           : QCNetworkCacheLookupStatus::StaleHit);
    result.setMetadata(meta);
    result.setBody(file.readAll());
    return result;
}

void QCNetworkDiskCache::insert(const QUrl &url,
                                const QByteArray &data,
                                const QCNetworkCacheMetadata &meta)
{
    QMutexLocker locker(&d_ptr->mutex);

    if (!isCacheable(meta.headers())) {
        return;
    }

    QString key     = d_ptr->cacheKey(url);
    qint64 dataSize = data.size();

    // 单个响应超过容量上限时直接跳过，避免清空整个缓存仍无法写入。
    if (dataSize > d_ptr->maxSize) {
        return;
    }

    d_ptr->ensureCacheDirectory();

    d_ptr->evictIfNeeded(dataSize);

    QFile dataFile(d_ptr->dataFilePath(key));
    if (!dataFile.open(QIODevice::WriteOnly)) {
        return;
    }
    dataFile.write(data);
    dataFile.close();

    QCNetworkCacheMetadata metaCopy = meta;
    metaCopy.setSize(dataSize);
    metaCopy.setCreationDate(QDateTime::currentDateTime());
    d_ptr->writeMetadata(key, metaCopy);

    if (d_ptr->currentSize >= 0) {
        d_ptr->currentSize += dataSize;
    }
}

bool QCNetworkDiskCache::remove(const QUrl &url)
{
    QMutexLocker locker(&d_ptr->mutex);

    QString key = d_ptr->cacheKey(url);
    QFileInfo dataInfo(d_ptr->dataFilePath(key));

    bool removed = false;
    if (QFile::remove(d_ptr->dataFilePath(key))) {
        removed = true;
        if (d_ptr->currentSize >= 0) {
            d_ptr->currentSize -= dataInfo.size();
        }
    }

    QFile::remove(d_ptr->metaFilePath(key));
    return removed;
}

void QCNetworkDiskCache::clear()
{
    QMutexLocker locker(&d_ptr->mutex);

    QDirIterator it(d_ptr->cacheDir,
                    QStringList() << QStringLiteral("*.data") << QStringLiteral("*.meta"),
                    QDir::Files);
    while (it.hasNext()) {
        it.next();
        QFile::remove(it.filePath());
    }

    d_ptr->currentSize = 0;
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
    d_ptr->maxSize = size;
    d_ptr->evictIfNeeded(0); // 新上限立即生效，必要时删除旧条目。
}

} // namespace QCurl
