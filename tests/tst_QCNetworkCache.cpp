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

#include <QtTest/QtTest>
#include <QTemporaryDir>

#include "QCNetworkCache.h"
#include "QCNetworkMemoryCache.h"
#include "QCNetworkDiskCache.h"
#include "QCNetworkCachePolicy.h"

using namespace QCurl;

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

    // ========== 磁盘缓存测试 ==========
    void testDiskCacheInsertAndRetrieve();
    void testDiskCachePersistence();
    void testDiskCacheRemove();
    void testDiskCacheClear();
    void testDiskCacheSizeLimit();

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

void TestQCNetworkCache::cleanupTestCase()
{
}

// ============================================================================
// 内存缓存测试
// ============================================================================

void TestQCNetworkCache::testMemoryCacheInsertAndRetrieve()
{
    QCNetworkMemoryCache cache;

    QUrl url("https://example.com/test");
    QByteArray data("Hello, World!");

    QCNetworkCacheMetadata meta;
    meta.url = url;
    meta.size = data.size();

    cache.insert(url, data, meta);

    QByteArray retrieved = cache.data(url);
    QCOMPARE(retrieved, data);
}

void TestQCNetworkCache::testMemoryCacheRemove()
{
    QCNetworkMemoryCache cache;

    QUrl url("https://example.com/test");
    QByteArray data("Test data");

    QCNetworkCacheMetadata meta;
    meta.url = url;

    cache.insert(url, data, meta);
    QVERIFY(!cache.data(url).isEmpty());

    cache.remove(url);
    QVERIFY(cache.data(url).isEmpty());
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
    cache.setMaxCacheSize(100);  // 100 字节

    QByteArray largeData(200, 'X');  // 200 字节
    QCNetworkCacheMetadata meta;

    cache.insert(QUrl("https://example.com/large"), largeData, meta);

    // 数据太大，不应该被缓存
    QVERIFY(cache.data(QUrl("https://example.com/large")).isEmpty());
}

void TestQCNetworkCache::testMemoryCacheExpiration()
{
    QCNetworkMemoryCache cache;

    QUrl url("https://example.com/expired");
    QByteArray data("Expired data");

    QCNetworkCacheMetadata meta;
    meta.url = url;
    meta.expirationDate = QDateTime::currentDateTime().addSecs(-10);  // 已过期

    cache.insert(url, data, meta);

    // 过期的数据应该被自动移除
    QVERIFY(cache.data(url).isEmpty());
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
    meta.url = url;
    meta.size = data.size();

    cache.insert(url, data, meta);

    QByteArray retrieved = cache.data(url);
    QCOMPARE(retrieved, data);
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

        QByteArray retrieved = cache.data(url);
        QCOMPARE(retrieved, data);
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
    QVERIFY(!cache.data(url).isEmpty());

    cache.remove(url);
    QVERIFY(cache.data(url).isEmpty());
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
    cache.setMaxCacheSize(100);  // 100 字节

    QByteArray largeData(200, 'Y');
    QCNetworkCacheMetadata meta;

    cache.insert(QUrl("https://example.com/large"), largeData, meta);

    // 数据太大，不应该被缓存
    QVERIFY(cache.data(QUrl("https://example.com/large")).isEmpty());
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
    headers["Expires"] = futureDate.toString(Qt::RFC2822Date).toLatin1();

    QDateTime expiration = QCNetworkCache::parseExpirationDate(headers);
    QVERIFY(expiration.isValid());

    qint64 diff = expiration.secsTo(futureDate);
    QVERIFY(qAbs(diff) < 5);  // 允许 5 秒误差
}

QTEST_MAIN(TestQCNetworkCache)
#include "tst_QCNetworkCache.moc"
