/**
 * @file tst_QCNetworkCache.cpp
 * @brief QCurl 缓存机制测试
 *
 * 测试覆盖：
 * - 内存缓存基础功能
 * - 磁盘缓存基础功能
 * - LRU 淘汰策略
 * - HTTP 缓存头解析
 * - 缓存大小限制
 *
 */

#include "QCNetworkCache.h"
#include "QCNetworkCachePolicy.h"
#include "QCNetworkDiskCache.h"
#include "QCNetworkMemoryCache.h"
#include "QCNetworkAccessManager.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QPointer>
#include <QtTest/QtTest>

using namespace QCurl;

namespace {

QCNetworkCacheMetadata metadataFor(const QUrl &url, int expiresInSeconds)
{
    QCNetworkCacheMetadata meta;
    meta.url            = url;
    meta.expirationDate = QDateTime::currentDateTime().addSecs(expiresInSeconds);
    return meta;
}

QString uniqueCacheDir(QTemporaryDir &tempDir, const QString &name)
{
    const QString path = tempDir.filePath(name);
    QDir dir;
    dir.mkpath(path);
    return path;
}

class LookupOnlyCache final : public QCNetworkCache
{
public:
    QCNetworkCacheLookupResult lookup(const QUrl &url, QCNetworkCacheReadMode mode) override
    {
        Q_UNUSED(url);
        Q_UNUSED(mode);
        return {};
    }

    void insert(const QUrl &url,
                const QByteArray &data,
                const QCNetworkCacheMetadata &meta) override
    {
        Q_UNUSED(url);
        Q_UNUSED(data);
        Q_UNUSED(meta);
    }

    bool remove(const QUrl &url) override
    {
        Q_UNUSED(url);
        return false;
    }

    void clear() override {}
    [[nodiscard]] qint64 cacheSize() const override { return 0; }
    [[nodiscard]] qint64 maxCacheSize() const override { return 0; }
    void setMaxCacheSize(qint64 size) override { Q_UNUSED(size); }
};

} // namespace

class TestQCNetworkCache : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();

    // ========== 内存缓存测试 ==========
    void testMemoryCacheInsertAndRetrieve();
    void testMemoryCacheRemove();
    void testMemoryCacheClear();
    void testMemoryCacheSizeLimit();
    void testMemoryCacheExpiration();
    void testMemoryCacheLookupZeroByteEntry();
    void testMemoryCacheLookupHonorsReadModeForExpiredEntry();
    void testMemoryCacheLookupDistinguishesExpiredZeroByteHit();

    // ========== 磁盘缓存测试 ==========
    void testDiskCacheInsertAndRetrieve();
    void testDiskCachePersistence();
    void testDiskCacheRemove();
    void testDiskCacheClear();
    void testDiskCacheSizeLimit();
    void testDiskCacheLookupZeroByteEntry();
    void testDiskCacheLookupHonorsReadModeForExpiredEntry();
    void testDiskCacheLookupDistinguishesExpiredZeroByteHit();
    void testDiskCacheLookupMissesWhenDataFileMissing();
    void testSetCachePathReplacesOnlyAutoCreatedCache();
    void testCustomCacheSubclassCanImplementLookupOnly();

    // ========== HTTP 缓存头解析测试 ==========
    void testCacheControlMaxAge();
    void testCacheControlNoStore();
    void testExpiresHeader();

private:
    QTemporaryDir m_tempDir;
};

void TestQCNetworkCache::initTestCase()
{
    QVERIFY(m_tempDir.isValid());
}

void TestQCNetworkCache::cleanupTestCase() {}

// ============================================================================
// 内存缓存测试
// ============================================================================

void TestQCNetworkCache::testMemoryCacheInsertAndRetrieve()
{
    QCNetworkMemoryCache cache;

    QUrl url("https://example.com/test");
    QByteArray data("Hello, World!");

    QCNetworkCacheMetadata meta;
    meta.url  = url;
    meta.size = data.size();

    cache.insert(url, data, meta);

    const auto retrieved = cache.lookup(url, QCNetworkCacheReadMode::FreshOnly);
    QCOMPARE(retrieved.status, QCNetworkCacheLookupStatus::FreshHit);
    QCOMPARE(retrieved.body, data);
}

void TestQCNetworkCache::testMemoryCacheRemove()
{
    QCNetworkMemoryCache cache;

    QUrl url("https://example.com/test");
    QByteArray data("Test data");

    QCNetworkCacheMetadata meta;
    meta.url = url;

    cache.insert(url, data, meta);
    QVERIFY(cache.lookup(url, QCNetworkCacheReadMode::FreshOnly).hit());

    cache.remove(url);
    QCOMPARE(cache.lookup(url, QCNetworkCacheReadMode::FreshOnly).status,
             QCNetworkCacheLookupStatus::Miss);
}

void TestQCNetworkCache::testMemoryCacheClear()
{
    QCNetworkMemoryCache cache;

    cache.insert(QUrl("https://example.com/1"), "data1", QCNetworkCacheMetadata());
    cache.insert(QUrl("https://example.com/2"), "data2", QCNetworkCacheMetadata());

    QVERIFY(cache.cacheSize() > 0);

    cache.clear();
    QCOMPARE(cache.cacheSize(), 0);
}

void TestQCNetworkCache::testMemoryCacheSizeLimit()
{
    QCNetworkMemoryCache cache;
    cache.setMaxCacheSize(100); // 100 字节

    QByteArray largeData(200, 'X'); // 200 字节
    QCNetworkCacheMetadata meta;

    cache.insert(QUrl("https://example.com/large"), largeData, meta);

    // 数据太大，不应该被缓存
    QCOMPARE(cache.lookup(QUrl("https://example.com/large"), QCNetworkCacheReadMode::FreshOnly).status,
             QCNetworkCacheLookupStatus::Miss);
}

void TestQCNetworkCache::testMemoryCacheExpiration()
{
    QCNetworkMemoryCache cache;

    QUrl url("https://example.com/expired");
    QByteArray data("Expired data");

    QCNetworkCacheMetadata meta;
    meta.url            = url;
    meta.expirationDate = QDateTime::currentDateTime().addSecs(-10); // 已过期

    cache.insert(url, data, meta);

    // 过期的数据应该被自动移除
    QCOMPARE(cache.lookup(url, QCNetworkCacheReadMode::FreshOnly).status,
             QCNetworkCacheLookupStatus::Miss);
}

void TestQCNetworkCache::testMemoryCacheLookupZeroByteEntry()
{
    QCNetworkMemoryCache cache;

    const QUrl url(QStringLiteral("https://example.com/zero-memory"));
    QCNetworkCacheMetadata meta;
    meta.url            = url;
    meta.expirationDate = QDateTime::currentDateTime().addSecs(60);

    cache.insert(url, QByteArray(), meta);

    const auto result = cache.lookup(url, QCNetworkCacheReadMode::FreshOnly);
    QCOMPARE(result.status, QCNetworkCacheLookupStatus::FreshHit);
    QVERIFY(result.body.isEmpty());
    QCOMPARE(result.metadata.url, url);
}

void TestQCNetworkCache::testMemoryCacheLookupHonorsReadModeForExpiredEntry()
{
    QCNetworkMemoryCache cache;

    const QUrl url(QStringLiteral("https://example.com/expired-memory-lookup"));
    const QByteArray body("expired memory body");
    cache.insert(url, body, metadataFor(url, -60));

    const auto stale = cache.lookup(url, QCNetworkCacheReadMode::AllowStale);
    QCOMPARE(stale.status, QCNetworkCacheLookupStatus::StaleHit);
    QCOMPARE(stale.metadata.url, url);
    QCOMPARE(stale.body, body);
    QVERIFY(stale.hit());

    const auto fresh = cache.lookup(url, QCNetworkCacheReadMode::FreshOnly);
    QCOMPARE(fresh.status, QCNetworkCacheLookupStatus::Miss);
    QVERIFY(!fresh.hit());
}

void TestQCNetworkCache::testMemoryCacheLookupDistinguishesExpiredZeroByteHit()
{
    QCNetworkMemoryCache cache;

    const QUrl url(QStringLiteral("https://example.com/expired-memory-zero"));
    cache.insert(url, QByteArray(), metadataFor(url, -60));

    const auto result = cache.lookup(url, QCNetworkCacheReadMode::AllowStale);
    QCOMPARE(result.status, QCNetworkCacheLookupStatus::StaleHit);
    QCOMPARE(result.metadata.url, url);
    QVERIFY(result.body.isEmpty());
    QVERIFY(result.hit());
}

// ============================================================================
// 磁盘缓存测试
// ============================================================================

void TestQCNetworkCache::testDiskCacheInsertAndRetrieve()
{
    QCNetworkDiskCache cache;
    cache.setCacheDirectory(m_tempDir.path());

    QUrl url("https://example.com/test");
    QByteArray data("Disk cache test");

    QCNetworkCacheMetadata meta;
    meta.url  = url;
    meta.size = data.size();

    cache.insert(url, data, meta);

    const auto retrieved = cache.lookup(url, QCNetworkCacheReadMode::FreshOnly);
    QCOMPARE(retrieved.status, QCNetworkCacheLookupStatus::FreshHit);
    QCOMPARE(retrieved.body, data);
}

void TestQCNetworkCache::testDiskCachePersistence()
{
    QUrl url("https://example.com/persistent");
    QByteArray data("Persistent data");

    {
        QCNetworkDiskCache cache;
        cache.setCacheDirectory(m_tempDir.path());

        QCNetworkCacheMetadata meta;
        meta.url = url;
        cache.insert(url, data, meta);
    }

    // 创建新实例，数据应该仍然存在
    {
        QCNetworkDiskCache cache;
        cache.setCacheDirectory(m_tempDir.path());

        const auto retrieved = cache.lookup(url, QCNetworkCacheReadMode::FreshOnly);
        QCOMPARE(retrieved.status, QCNetworkCacheLookupStatus::FreshHit);
        QCOMPARE(retrieved.body, data);
    }
}

void TestQCNetworkCache::testDiskCacheRemove()
{
    QCNetworkDiskCache cache;
    cache.setCacheDirectory(m_tempDir.path());

    QUrl url("https://example.com/remove");
    QByteArray data("To be removed");

    QCNetworkCacheMetadata meta;
    meta.url = url;

    cache.insert(url, data, meta);
    QVERIFY(cache.lookup(url, QCNetworkCacheReadMode::FreshOnly).hit());

    cache.remove(url);
    QCOMPARE(cache.lookup(url, QCNetworkCacheReadMode::FreshOnly).status,
             QCNetworkCacheLookupStatus::Miss);
}

void TestQCNetworkCache::testDiskCacheClear()
{
    QCNetworkDiskCache cache;
    cache.setCacheDirectory(m_tempDir.path());

    cache.insert(QUrl("https://example.com/1"), "data1", QCNetworkCacheMetadata());
    cache.insert(QUrl("https://example.com/2"), "data2", QCNetworkCacheMetadata());

    QVERIFY(cache.cacheSize() > 0);

    cache.clear();
    QCOMPARE(cache.cacheSize(), 0);
}

void TestQCNetworkCache::testDiskCacheSizeLimit()
{
    QCNetworkDiskCache cache;
    cache.setCacheDirectory(m_tempDir.path());
    cache.setMaxCacheSize(100); // 100 字节

    QByteArray largeData(200, 'Y');
    QCNetworkCacheMetadata meta;

    cache.insert(QUrl("https://example.com/large"), largeData, meta);

    // 数据太大，不应该被缓存
    QCOMPARE(cache.lookup(QUrl("https://example.com/large"), QCNetworkCacheReadMode::FreshOnly).status,
             QCNetworkCacheLookupStatus::Miss);
}

void TestQCNetworkCache::testDiskCacheLookupZeroByteEntry()
{
    QCNetworkDiskCache cache;
    cache.setCacheDirectory(m_tempDir.path());

    const QUrl url(QStringLiteral("https://example.com/zero-disk"));
    QCNetworkCacheMetadata meta;
    meta.url            = url;
    meta.expirationDate = QDateTime::currentDateTime().addSecs(60);

    cache.insert(url, QByteArray(), meta);

    const auto result = cache.lookup(url, QCNetworkCacheReadMode::FreshOnly);
    QCOMPARE(result.status, QCNetworkCacheLookupStatus::FreshHit);
    QVERIFY(result.body.isEmpty());
    QCOMPARE(result.metadata.url, url);
}

void TestQCNetworkCache::testDiskCacheLookupHonorsReadModeForExpiredEntry()
{
    QCNetworkDiskCache cache;
    cache.setCacheDirectory(uniqueCacheDir(m_tempDir, QStringLiteral("disk-lookup-expired")));

    const QUrl url(QStringLiteral("https://example.com/expired-disk-lookup"));
    const QByteArray body("expired disk body");
    cache.insert(url, body, metadataFor(url, -60));

    const auto stale = cache.lookup(url, QCNetworkCacheReadMode::AllowStale);
    QCOMPARE(stale.status, QCNetworkCacheLookupStatus::StaleHit);
    QCOMPARE(stale.metadata.url, url);
    QCOMPARE(stale.body, body);
    QVERIFY(stale.hit());

    const auto fresh = cache.lookup(url, QCNetworkCacheReadMode::FreshOnly);
    QCOMPARE(fresh.status, QCNetworkCacheLookupStatus::Miss);
    QVERIFY(!fresh.hit());
}

void TestQCNetworkCache::testDiskCacheLookupDistinguishesExpiredZeroByteHit()
{
    QCNetworkDiskCache cache;
    cache.setCacheDirectory(uniqueCacheDir(m_tempDir, QStringLiteral("disk-lookup-zero")));

    const QUrl url(QStringLiteral("https://example.com/expired-disk-zero"));
    cache.insert(url, QByteArray(), metadataFor(url, -60));

    const auto result = cache.lookup(url, QCNetworkCacheReadMode::AllowStale);
    QCOMPARE(result.status, QCNetworkCacheLookupStatus::StaleHit);
    QCOMPARE(result.metadata.url, url);
    QVERIFY(result.body.isEmpty());
    QVERIFY(result.hit());
}

void TestQCNetworkCache::testDiskCacheLookupMissesWhenDataFileMissing()
{
    const QString cacheDir = uniqueCacheDir(m_tempDir, QStringLiteral("disk-lookup-missing-data"));
    QCNetworkDiskCache cache;
    cache.setCacheDirectory(cacheDir);

    const QUrl url(QStringLiteral("https://example.com/expired-disk-missing-data"));
    cache.insert(url, QByteArrayLiteral("metadata without body"), metadataFor(url, -60));

    const auto dataFiles = QDir(cacheDir).entryList(QStringList{QStringLiteral("*.data")},
                                                   QDir::Files);
    QCOMPARE(dataFiles.size(), 1);
    QVERIFY(QFile::remove(QDir(cacheDir).filePath(dataFiles.first())));

    const auto result = cache.lookup(url, QCNetworkCacheReadMode::AllowStale);
    QCOMPARE(result.status, QCNetworkCacheLookupStatus::Miss);
    QVERIFY(!result.hit());
}

void TestQCNetworkCache::testSetCachePathReplacesOnlyAutoCreatedCache()
{
    QCNetworkAccessManager manager;

    const QString cacheDir1 = m_tempDir.filePath(QStringLiteral("auto-cache-1"));
    const QString cacheDir2 = m_tempDir.filePath(QStringLiteral("auto-cache-2"));
    manager.setCachePath(cacheDir1, 1024 * 1024);

    auto *firstAutoCache = qobject_cast<QCNetworkDiskCache *>(manager.cache());
    QVERIFY(firstAutoCache != nullptr);
    QPointer<QObject> firstDestroyed(firstAutoCache);

    manager.setCachePath(cacheDir2, 1024 * 1024);
    auto *secondAutoCache = qobject_cast<QCNetworkDiskCache *>(manager.cache());
    QVERIFY(secondAutoCache != nullptr);
    QVERIFY(secondAutoCache != firstAutoCache);

    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QVERIFY(firstDestroyed.isNull());

    auto *externalCache = new QCNetworkMemoryCache(&manager);
    QPointer<QObject> externalGuard(externalCache);
    manager.setCache(externalCache);
    QCOMPARE(manager.cache(), externalCache);

    manager.setCachePath(m_tempDir.filePath(QStringLiteral("auto-cache-3")), 1024 * 1024);
    QCOMPARE(manager.cache()->parent(), &manager);
    QVERIFY(!externalGuard.isNull());
}

void TestQCNetworkCache::testCustomCacheSubclassCanImplementLookupOnly()
{
    LookupOnlyCache cache;

    const auto result = cache.lookup(QUrl(QStringLiteral("https://example.com/custom-cache")),
                                     QCNetworkCacheReadMode::FreshOnly);
    QCOMPARE(result.status, QCNetworkCacheLookupStatus::Miss);
}

// ============================================================================
// HTTP 缓存头解析测试
// ============================================================================

void TestQCNetworkCache::testCacheControlMaxAge()
{
    QMap<QByteArray, QByteArray> headers;
    headers["Cache-Control"] = "max-age=3600";

    QDateTime expiration = QCNetworkCache::parseExpirationDate(headers);
    QVERIFY(expiration.isValid());

    // 应该在未来约 1 小时
    qint64 secondsToExpire = QDateTime::currentDateTime().secsTo(expiration);
    QVERIFY(secondsToExpire > 3500 && secondsToExpire < 3700);
}

void TestQCNetworkCache::testCacheControlNoStore()
{
    QMap<QByteArray, QByteArray> headers;
    headers["Cache-Control"] = "no-store";

    QVERIFY(!QCNetworkCache::isCacheable(headers));
}

void TestQCNetworkCache::testExpiresHeader()
{
    QMap<QByteArray, QByteArray> headers;
    QDateTime futureDate = QDateTime::currentDateTime().addSecs(7200);
    headers["Expires"]   = futureDate.toString(Qt::RFC2822Date).toLatin1();

    QDateTime expiration = QCNetworkCache::parseExpirationDate(headers);
    QVERIFY(expiration.isValid());

    qint64 diff = expiration.secsTo(futureDate);
    QVERIFY(qAbs(diff) < 5); // 允许 5 秒误差
}

QTEST_MAIN(TestQCNetworkCache)
#include "tst_QCNetworkCache.moc"
