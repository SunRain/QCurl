#include "QCNetworkDiskCache.h"
#include <QMutexLocker>
#include <QCryptographicHash>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QStandardPaths>
#include <QDirIterator>

namespace QCurl {

QCNetworkDiskCache::QCNetworkDiskCache(QObject *parent)
    : QCNetworkCache(parent)
    , m_maxSize(50 * 1024 * 1024)  // 默认 50MB
    , m_currentSize(-1)  // 延迟计算
{
    // 默认缓存目录
    m_cacheDir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + "/QCurl";
    ensureCacheDirectory();
}

QCNetworkDiskCache::~QCNetworkDiskCache()
{
}

void QCNetworkDiskCache::setCacheDirectory(const QString &path)
{
    QMutexLocker locker(&m_mutex);
    m_cacheDir = path;
    m_currentSize = -1;  // 重新计算
    ensureCacheDirectory();
}

QString QCNetworkDiskCache::cacheDirectory() const
{
    QMutexLocker locker(&m_mutex);
    return m_cacheDir;
}

QString QCNetworkDiskCache::cacheKey(const QUrl &url) const
{
    QByteArray hash = QCryptographicHash::hash(
        url.toString().toUtf8(), QCryptographicHash::Md5);
    return QString::fromLatin1(hash.toHex());
}

QString QCNetworkDiskCache::dataFilePath(const QString &key) const
{
    return m_cacheDir + "/" + key + ".data";
}

QString QCNetworkDiskCache::metaFilePath(const QString &key) const
{
    return m_cacheDir + "/" + key + ".meta";
}

void QCNetworkDiskCache::ensureCacheDirectory()
{
    QDir dir;
    if (!dir.exists(m_cacheDir)) {
        dir.mkpath(m_cacheDir);
    }
}

void QCNetworkDiskCache::updateCacheSize() const
{
    if (m_currentSize >= 0) {
        return;  // 已计算
    }

    m_currentSize = 0;
    QDirIterator it(m_cacheDir, QStringList() << "*.data", QDir::Files);
    while (it.hasNext()) {
        it.next();
        m_currentSize += it.fileInfo().size();
    }
}

void QCNetworkDiskCache::evictIfNeeded(qint64 newDataSize)
{
    updateCacheSize();

    if (m_currentSize + newDataSize <= m_maxSize) {
        return;  // 空间足够
    }

    // 收集所有缓存文件及其访问时间
    struct CacheFile {
        QString key;
        QDateTime lastAccess;
        qint64 size;
    };
    QList<CacheFile> files;

    QDirIterator it(m_cacheDir, QStringList() << "*.meta", QDir::Files);
    while (it.hasNext()) {
        it.next();
        QString metaPath = it.filePath();
        QString key = it.fileInfo().baseName();

        QCNetworkCacheMetadata meta = readMetadata(key);
        if (!meta.url.isEmpty()) {
            QFileInfo dataInfo(dataFilePath(key));
            files.append({key, dataInfo.lastModified(), dataInfo.size()});
        }
    }

    // 按访问时间排序（最旧的在前）
    std::sort(files.begin(), files.end(), [](const CacheFile &a, const CacheFile &b) {
        return a.lastAccess < b.lastAccess;
    });

    // 删除最旧的文件直到空间足够
    for (const CacheFile &file : files) {
        if (m_currentSize + newDataSize <= m_maxSize) {
            break;
        }

        QFile::remove(dataFilePath(file.key));
        QFile::remove(metaFilePath(file.key));
        m_currentSize -= file.size;
    }
}

bool QCNetworkDiskCache::writeMetadata(const QString &key, const QCNetworkCacheMetadata &meta)
{
    QJsonObject json;
    json["url"] = meta.url.toString();
    json["size"] = meta.size;
    json["creationDate"] = meta.creationDate.toString(Qt::ISODate);
    json["expirationDate"] = meta.expirationDate.toString(Qt::ISODate);
    json["lastModified"] = meta.lastModified.toString(Qt::ISODate);

    // 保存响应头
    QJsonObject headersObj;
    for (auto it = meta.headers.constBegin(); it != meta.headers.constEnd(); ++it) {
        headersObj[QString::fromLatin1(it.key())] = QString::fromLatin1(it.value());
    }
    json["headers"] = headersObj;

    QFile file(metaFilePath(key));
    if (!file.open(QIODevice::WriteOnly)) {
        return false;
    }

    file.write(QJsonDocument(json).toJson(QJsonDocument::Compact));
    return true;
}

QCNetworkCacheMetadata QCNetworkDiskCache::readMetadata(const QString &key) const
{
    QFile file(metaFilePath(key));
    if (!file.open(QIODevice::ReadOnly)) {
        return QCNetworkCacheMetadata();
    }

    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    if (!doc.isObject()) {
        return QCNetworkCacheMetadata();
    }

    QJsonObject json = doc.object();
    QCNetworkCacheMetadata meta;
    meta.url = QUrl(json["url"].toString());
    meta.size = json["size"].toInteger();
    meta.creationDate = QDateTime::fromString(json["creationDate"].toString(), Qt::ISODate);
    meta.expirationDate = QDateTime::fromString(json["expirationDate"].toString(), Qt::ISODate);
    meta.lastModified = QDateTime::fromString(json["lastModified"].toString(), Qt::ISODate);

    // 读取响应头
    QJsonObject headersObj = json["headers"].toObject();
    for (auto it = headersObj.constBegin(); it != headersObj.constEnd(); ++it) {
        meta.headers[it.key().toLatin1()] = it.value().toString().toLatin1();
    }

    return meta;
}

QByteArray QCNetworkDiskCache::data(const QUrl &url)
{
    QMutexLocker locker(&m_mutex);

    QString key = cacheKey(url);
    QCNetworkCacheMetadata meta = readMetadata(key);

    if (meta.url.isEmpty()) {
        return QByteArray();
    }

    // 检查是否过期
    if (!meta.isValid()) {
        remove(url);
        return QByteArray();
    }

    // 读取数据
    QFile file(dataFilePath(key));
    if (!file.open(QIODevice::ReadOnly)) {
        return QByteArray();
    }

    // 读取数据并返回
    return file.readAll();
}

QCNetworkCacheMetadata QCNetworkDiskCache::metadata(const QUrl &url)
{
    QMutexLocker locker(&m_mutex);
    return readMetadata(cacheKey(url));
}

void QCNetworkDiskCache::insert(const QUrl &url, const QByteArray &data,
                                 const QCNetworkCacheMetadata &meta)
{
    QMutexLocker locker(&m_mutex);

    // 检查是否可缓存
    if (!isCacheable(meta.headers)) {
        return;
    }

    QString key = cacheKey(url);
    qint64 dataSize = data.size();

    // 如果数据太大，不缓存
    if (dataSize > m_maxSize) {
        return;
    }

    ensureCacheDirectory();

    // 淘汰旧数据
    evictIfNeeded(dataSize);

    // 写入数据文件
    QFile dataFile(dataFilePath(key));
    if (!dataFile.open(QIODevice::WriteOnly)) {
        return;
    }
    dataFile.write(data);
    dataFile.close();

    // 写入元数据
    QCNetworkCacheMetadata metaCopy = meta;
    metaCopy.size = dataSize;
    metaCopy.creationDate = QDateTime::currentDateTime();
    writeMetadata(key, metaCopy);

    // 更新缓存大小
    if (m_currentSize >= 0) {
        m_currentSize += dataSize;
    }
}

bool QCNetworkDiskCache::remove(const QUrl &url)
{
    QMutexLocker locker(&m_mutex);

    QString key = cacheKey(url);
    QFileInfo dataInfo(dataFilePath(key));

    bool removed = false;
    if (QFile::remove(dataFilePath(key))) {
        removed = true;
        if (m_currentSize >= 0) {
            m_currentSize -= dataInfo.size();
        }
    }

    QFile::remove(metaFilePath(key));
    return removed;
}

void QCNetworkDiskCache::clear()
{
    QMutexLocker locker(&m_mutex);

    QDirIterator it(m_cacheDir, QStringList() << "*.data" << "*.meta", QDir::Files);
    while (it.hasNext()) {
        it.next();
        QFile::remove(it.filePath());
    }

    m_currentSize = 0;
}

qint64 QCNetworkDiskCache::cacheSize() const
{
    QMutexLocker locker(&m_mutex);
    updateCacheSize();
    return m_currentSize;
}

qint64 QCNetworkDiskCache::maxCacheSize() const
{
    QMutexLocker locker(&m_mutex);
    return m_maxSize;
}

void QCNetworkDiskCache::setMaxCacheSize(qint64 size)
{
    QMutexLocker locker(&m_mutex);
    m_maxSize = size;
    evictIfNeeded(0);  // 立即淘汰超出的数据
}

} // namespace QCurl
