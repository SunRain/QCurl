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

#include "QCNetworkAccessManager.h"
#include "QCNetworkCache.h"
#include "QCNetworkCachePolicy.h"
#include "QCNetworkCacheRequestKey.h"
#include "QCNetworkDiskCache.h"
#include "QCNetworkHttpMethod.h"
#include "QCNetworkMemoryCache.h"
#include "QCNetworkRequest.h"
#include "private/QCNetworkCacheKey_p.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QPointer>
#include <QTemporaryDir>
#include <QtEndian>
#include <QtTest/QtTest>

#include <algorithm>
#include <limits>

using namespace QCurl;

namespace {

QCNetworkCacheMetadata metadataFor(const QUrl &url, int expiresInSeconds)
{
    QCNetworkCacheMetadata meta;
    meta.setUrl(url);
    meta.setStatusCode(200);
    meta.setExpirationDate(QDateTime::currentDateTime().addSecs(expiresInSeconds));
    return meta;
}

QCNetworkCacheRequestKey requestKeyFor(const QUrl &url,
                                       HttpMethod method = HttpMethod::Get,
                                       const QMap<QByteArray, QByteArray> &headers = {},
                                       const QByteArray &partitionKey              = {},
                                       bool authenticated                          = false)
{
    QCNetworkCacheRequestKey key(method, url);
    key.setRequestHeaders(headers);
    key.setCachePartitionKey(partitionKey);
    key.setAuthenticationContext(authenticated);
    return key;
}

QString uniqueCacheDir(QTemporaryDir &tempDir, const QString &name)
{
    const QString path = tempDir.filePath(name);
    QDir dir;
    dir.mkpath(path);
    return path;
}

void writeU16At(QByteArray &bytes, qsizetype offset, quint16 value)
{
    qToBigEndian(value, reinterpret_cast<uchar *>(bytes.data() + offset));
}

void writeU32At(QByteArray &bytes, qsizetype offset, quint32 value)
{
    qToBigEndian(value, reinterpret_cast<uchar *>(bytes.data() + offset));
}

void writeU64At(QByteArray &bytes, qsizetype offset, quint64 value)
{
    qToBigEndian(value, reinterpret_cast<uchar *>(bytes.data() + offset));
}

QByteArray envelopeFor(const QByteArray &payload,
                       quint16 version               = 4,
                       quint64 declaredPayloadLength = std::numeric_limits<quint64>::max())
{
    QByteArray envelope(52, '\0');
    writeU32At(envelope, 0, 0x51434348U);
    writeU16At(envelope, 4, version);
    writeU16At(envelope, 6, 0);
    writeU32At(envelope, 8, 52);
    writeU64At(envelope,
               12,
               declaredPayloadLength == std::numeric_limits<quint64>::max()
                   ? static_cast<quint64>(payload.size())
                   : declaredPayloadLength);
    const QByteArray checksum = QCryptographicHash::hash(payload, QCryptographicHash::Sha256);
    std::copy(checksum.cbegin(), checksum.cend(), envelope.begin() + 20);
    envelope.append(payload);
    return envelope;
}

QByteArray minimalPayload(const QByteArray &primaryDigest,
                          const QByteArray &variantDigest,
                          quint64 bodyLength,
                          quint32 varyCount = 0)
{
    QByteArray payload;
    payload.append(primaryDigest);
    payload.append(variantDigest);
    const qsizetype countOffset = payload.size();
    payload.resize(payload.size() + 4);
    writeU32At(payload, countOffset, varyCount);
    payload.resize(payload.size() + 4);
    writeU32At(payload, payload.size() - 4, 0); // URL length
    payload.resize(payload.size() + 4);
    writeU32At(payload, payload.size() - 4, 0); // raw-header count
    payload.resize(payload.size() + (5 * 8) + 8 + 8 + 4 + 8);
    writeU64At(payload, payload.size() - 8, bodyLength);
    return payload;
}

class LookupOnlyCache final : public QCNetworkCache
{
public:
    QCNetworkCacheLookupResult lookup(const QCNetworkCacheRequestKey &key,
                                      QCNetworkCacheReadMode mode) override
    {
        Q_UNUSED(key);
        Q_UNUSED(mode);
        return {};
    }

    void insert(const QCNetworkCacheRequestKey &key,
                const QByteArray &data,
                const QCNetworkCacheMetadata &meta) override
    {
        Q_UNUSED(key);
        Q_UNUSED(data);
        Q_UNUSED(meta);
    }

    bool remove(const QCNetworkCacheRequestKey &key) override
    {
        Q_UNUSED(key);
        return false;
    }

    [[nodiscard]] QCNetworkCacheClearResult clear() override
    {
        return QCNetworkCacheClearResult::success(0, 0);
    }
    [[nodiscard]] qint64 cacheSize() const override { return 0; }
    [[nodiscard]] qint64 maxCacheSize() const override { return 0; }
    void setMaxCacheSize(qint64 size) override { Q_UNUSED(size); }
};

} // namespace

class TestQCNetworkCache : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void cleanupTestCase();

    void testStructuredRequestKeyNormalizesIdentity();
    void testRequestCachePartitionKeyUsesValueSemantics();

    // ========== 内存缓存测试 ==========
    void testMemoryCacheInsertAndRetrieve();
    void testMemoryCacheRemove();
    void testMemoryCacheClear();
    void testMemoryCacheClearResult();
    void testMemoryCacheSizeLimit();
    void testMemoryCacheExpiration();
    void testMemoryCacheLookupZeroByteEntry();
    void testMemoryCacheLookupHonorsReadModeForExpiredEntry();
    void testMemoryCacheLookupDistinguishesExpiredZeroByteHit();
    void testMemoryCacheSeparatesMethodVaryAndAuthenticationPartition();
    void testMemoryCachePrefersMostRecentlyStoredMatchingVaryShape();
    void testMemoryCacheRejectsUnpartitionedAuthenticatedEntry();
    void testMemoryCacheReplacementKeepsExactCapacity();

    // ========== 磁盘缓存测试 ==========
    void testDiskCacheInsertAndRetrieve();
    void testDiskCachePersistence();
    void testDiskCacheRemove();
    void testDiskCacheClear();
    void testDiskCacheClearResult();
    void testDiskCacheSizeLimit();
    void testDiskCacheLookupZeroByteEntry();
    void testDiskCacheLookupHonorsReadModeForExpiredEntry();
    void testDiskCacheLookupDistinguishesExpiredZeroByteHit();
    void testDiskCacheLookupMissesWhenDataFileMissing();
    void testDiskCacheSeparatesVaryAndAuthenticationPartition();
    void testDiskCachePrefersMostRecentMatchingResponseDate();
    void testDiskCacheRejectsSensitiveResponseMetadata();
    void testDiskCacheAtomicReplacementPreservesPreviousEntry();
    void testDiskCacheCleansLegacyAndCorruptEntries();
    void testDiskCacheMixedCorruptionKeepsExactSize();
    void testDiskCacheLookupRefreshesLruOrder();
    void testDiskCacheEnvelopeV4RejectsMalformedLengths();
    void testManagerUsesExplicitDiskCacheInjection();
    void testManagerCachePointerClearsOnDestruction();
    void testCustomCacheSubclassCanImplementLookupOnly();

    // ========== HTTP 缓存头解析测试 ==========
    void testCacheControlMaxAge();
    void testCacheControlNoStore();
    void testCacheControlNoCacheAndMaxAgeZeroAreStoredStale();
    void testVaryStarIsNotCacheable();
    void testRawCacheControlCombinesMixedCaseLines();
    void testRawVaryCombinesMixedCaseLines();
    void testRawSetCookieIsNeverCacheableOrFresh();
    void testStandardHttpDateWithGmtZoneIsCacheable();
    void testConflictingOrInvalidDateAgeIsNotCacheable();
    void testAgeInt64MaxUsesSaturatedFreshnessArithmetic();
    void testExpiresHeader();

private:
    QTemporaryDir m_tempDir;
};

void TestQCNetworkCache::initTestCase()
{
    QVERIFY(m_tempDir.isValid());
}

void TestQCNetworkCache::cleanupTestCase() {}

void TestQCNetworkCache::testStructuredRequestKeyNormalizesIdentity()
{
    QCNetworkCacheRequestKey key(HttpMethod::Get,
                                 QUrl(QStringLiteral(
                                     "HTTPS://user:secret@Example.COM:443/items?q=1#fragment")));
    key.setRequestHeader(QByteArrayLiteral("Accept-Language"), QByteArrayLiteral(" en-US "));
    key.setRequestHeader(QByteArrayLiteral("accept-language"), QByteArrayLiteral("fr-FR"));
    key.setCachePartitionKey(QByteArrayLiteral("tenant-a"));
    key.setAuthenticationContext(true);

    QCOMPARE(key.method(), HttpMethod::Get);
    QCOMPARE(key.normalizedUrl().toString(QUrl::FullyEncoded),
             QStringLiteral("https://example.com/items?q=1"));
    QCOMPARE(key.requestHeader(QByteArrayLiteral("ACCEPT-LANGUAGE")), QByteArrayLiteral("fr-FR"));
    QCOMPARE(key.requestHeaders().size(), 1);
    QCOMPARE(key.cachePartitionKey(), QByteArrayLiteral("tenant-a"));
    QVERIFY(key.hasAuthenticationContext());
}

void TestQCNetworkCache::testRequestCachePartitionKeyUsesValueSemantics()
{
    QCNetworkRequest request(QUrl(QStringLiteral("https://example.com/private")));
    request.setCachePartitionKey(QByteArrayLiteral("tenant-a"));

    QCNetworkRequest copy = request;
    copy.setCachePartitionKey(QByteArrayLiteral("tenant-b"));

    QCOMPARE(request.cachePartitionKey(), QByteArrayLiteral("tenant-a"));
    QCOMPARE(copy.cachePartitionKey(), QByteArrayLiteral("tenant-b"));
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
    meta.setUrl(url);
    meta.setSize(data.size());

    cache.insert(requestKeyFor(url), data, meta);

    const auto retrieved = cache.lookup(requestKeyFor(url), QCNetworkCacheReadMode::FreshOnly);
    QCOMPARE(retrieved.status(), QCNetworkCacheLookupStatus::FreshHit);
    QCOMPARE(retrieved.body(), data);
}

void TestQCNetworkCache::testMemoryCacheRemove()
{
    QCNetworkMemoryCache cache;

    QUrl url("https://example.com/test");
    QByteArray data("Test data");

    QCNetworkCacheMetadata meta;
    meta.setUrl(url);

    cache.insert(requestKeyFor(url), data, meta);
    QVERIFY(cache.lookup(requestKeyFor(url), QCNetworkCacheReadMode::FreshOnly).hit());

    QVERIFY(cache.remove(requestKeyFor(url)));
    QCOMPARE(cache.lookup(requestKeyFor(url), QCNetworkCacheReadMode::FreshOnly).status(),
             QCNetworkCacheLookupStatus::Miss);
}

void TestQCNetworkCache::testMemoryCacheClear()
{
    QCNetworkMemoryCache cache;

    cache.insert(requestKeyFor(QUrl("https://example.com/1")), "data1", QCNetworkCacheMetadata());
    cache.insert(requestKeyFor(QUrl("https://example.com/2")), "data2", QCNetworkCacheMetadata());

    QVERIFY(cache.cacheSize() > 0);

    QVERIFY(cache.clear().isSuccess());
    QCOMPARE(cache.cacheSize(), 0);
}

void TestQCNetworkCache::testMemoryCacheClearResult()
{
    QCNetworkMemoryCache cache;
    cache.insert(requestKeyFor(QUrl("https://example.com/clear-result")),
                 QByteArrayLiteral("body"),
                 QCNetworkCacheMetadata());

    const QCNetworkCacheClearResult result = cache.clear();
    QCOMPARE(result.status(), QCNetworkCacheClearResult::Status::Success);
    QCOMPARE(result.removedCount(), qint64(1));
    QCOMPARE(result.failedCount(), qint64(0));
    QCOMPARE(result.remainingBytes(), qint64(0));
    QCOMPARE(result.errorCode(), QCNetworkCacheClearResult::ErrorCode::None);
    QVERIFY(result.errorMessage().isEmpty());
}

void TestQCNetworkCache::testMemoryCacheSizeLimit()
{
    QCNetworkMemoryCache cache;
    cache.setMaxCacheSize(100); // 100 字节

    QByteArray largeData(200, 'X'); // 200 字节
    QCNetworkCacheMetadata meta;

    cache.insert(requestKeyFor(QUrl("https://example.com/large")), largeData, meta);

    // 数据太大，不应该被缓存
    QCOMPARE(cache
                 .lookup(requestKeyFor(QUrl("https://example.com/large")),
                         QCNetworkCacheReadMode::FreshOnly)
                 .status(),
             QCNetworkCacheLookupStatus::Miss);
}

void TestQCNetworkCache::testMemoryCacheExpiration()
{
    QCNetworkMemoryCache cache;

    QUrl url("https://example.com/expired");
    QByteArray data("Expired data");

    QCNetworkCacheMetadata meta;
    meta.setUrl(url);
    meta.setExpirationDate(QDateTime::currentDateTime().addSecs(-10)); // 已过期

    cache.insert(requestKeyFor(url), data, meta);

    // 过期的数据应该被自动移除
    QCOMPARE(cache.lookup(requestKeyFor(url), QCNetworkCacheReadMode::FreshOnly).status(),
             QCNetworkCacheLookupStatus::Miss);
}

void TestQCNetworkCache::testMemoryCacheLookupZeroByteEntry()
{
    QCNetworkMemoryCache cache;

    const QUrl url(QStringLiteral("https://example.com/zero-memory"));
    QCNetworkCacheMetadata meta;
    meta.setUrl(url);
    meta.setExpirationDate(QDateTime::currentDateTime().addSecs(60));

    cache.insert(requestKeyFor(url), QByteArray(), meta);

    const auto result = cache.lookup(requestKeyFor(url), QCNetworkCacheReadMode::FreshOnly);
    QCOMPARE(result.status(), QCNetworkCacheLookupStatus::FreshHit);
    QVERIFY(result.body().isEmpty());
    QCOMPARE(result.metadata().url(), url);
}

void TestQCNetworkCache::testMemoryCacheLookupHonorsReadModeForExpiredEntry()
{
    QCNetworkMemoryCache cache;

    const QUrl url(QStringLiteral("https://example.com/expired-memory-lookup"));
    const QByteArray body("expired memory body");
    cache.insert(requestKeyFor(url), body, metadataFor(url, -60));

    const auto stale = cache.lookup(requestKeyFor(url), QCNetworkCacheReadMode::AllowStale);
    QCOMPARE(stale.status(), QCNetworkCacheLookupStatus::StaleHit);
    QCOMPARE(stale.metadata().url(), url);
    QCOMPARE(stale.body(), body);
    QVERIFY(stale.hit());

    const auto fresh = cache.lookup(requestKeyFor(url), QCNetworkCacheReadMode::FreshOnly);
    QCOMPARE(fresh.status(), QCNetworkCacheLookupStatus::Miss);
    QVERIFY(!fresh.hit());
}

void TestQCNetworkCache::testMemoryCacheLookupDistinguishesExpiredZeroByteHit()
{
    QCNetworkMemoryCache cache;

    const QUrl url(QStringLiteral("https://example.com/expired-memory-zero"));
    cache.insert(requestKeyFor(url), QByteArray(), metadataFor(url, -60));

    const auto result = cache.lookup(requestKeyFor(url), QCNetworkCacheReadMode::AllowStale);
    QCOMPARE(result.status(), QCNetworkCacheLookupStatus::StaleHit);
    QCOMPARE(result.metadata().url(), url);
    QVERIFY(result.body().isEmpty());
    QVERIFY(result.hit());
}

void TestQCNetworkCache::testMemoryCacheSeparatesMethodVaryAndAuthenticationPartition()
{
    QCNetworkMemoryCache cache;
    const QUrl url(QStringLiteral("https://example.com/catalog"));
    QCNetworkCacheMetadata meta = metadataFor(url, 60);
    meta.setVaryHeaderNames({QByteArrayLiteral("Accept-Language")});

    const auto english = requestKeyFor(url,
                                       HttpMethod::Get,
                                       {{QByteArrayLiteral("accept-language"),
                                         QByteArrayLiteral("en")}});
    const auto french  = requestKeyFor(url,
                                       HttpMethod::Get,
                                       {{QByteArrayLiteral("Accept-Language"),
                                         QByteArrayLiteral("fr")}});
    cache.insert(english, QByteArrayLiteral("english"), meta);
    cache.insert(french, QByteArrayLiteral("francais"), meta);

    QCOMPARE(cache.lookup(english, QCNetworkCacheReadMode::FreshOnly).body(),
             QByteArrayLiteral("english"));
    QCOMPARE(cache.lookup(french, QCNetworkCacheReadMode::FreshOnly).body(),
             QByteArrayLiteral("francais"));
    QCOMPARE(cache.lookup(requestKeyFor(url, HttpMethod::Post), QCNetworkCacheReadMode::FreshOnly)
                 .status(),
             QCNetworkCacheLookupStatus::Miss);

    QCNetworkCacheMetadata privateMeta = metadataFor(url, 60);
    const auto tenantA = requestKeyFor(url, HttpMethod::Get, {}, QByteArrayLiteral("tenant-a"), true);
    const auto tenantB = requestKeyFor(url, HttpMethod::Get, {}, QByteArrayLiteral("tenant-b"), true);
    cache.insert(tenantA, QByteArrayLiteral("private-a"), privateMeta);
    cache.insert(tenantB, QByteArrayLiteral("private-b"), privateMeta);

    QCOMPARE(cache.lookup(tenantA, QCNetworkCacheReadMode::FreshOnly).body(),
             QByteArrayLiteral("private-a"));
    QCOMPARE(cache.lookup(tenantB, QCNetworkCacheReadMode::FreshOnly).body(),
             QByteArrayLiteral("private-b"));
}

void TestQCNetworkCache::testMemoryCachePrefersMostRecentlyStoredMatchingVaryShape()
{
    QCNetworkMemoryCache cache;
    const QUrl url(QStringLiteral("https://example.com/vary-transition"));

    const auto genericKey = requestKeyFor(url);
    cache.insert(genericKey, QByteArrayLiteral("generic"), metadataFor(url, 60));

    QCNetworkCacheMetadata localizedMetadata = metadataFor(url, 60);
    localizedMetadata.setVaryHeaderNames({QByteArrayLiteral("accept-language")});
    const auto englishKey = requestKeyFor(url,
                                          HttpMethod::Get,
                                          {{QByteArrayLiteral("accept-language"),
                                            QByteArrayLiteral("en")}});
    cache.insert(englishKey, QByteArrayLiteral("english"), localizedMetadata);

    const auto frenchKey = requestKeyFor(url,
                                         HttpMethod::Get,
                                         {{QByteArrayLiteral("accept-language"),
                                           QByteArrayLiteral("fr")}});
    QCOMPARE(cache.lookup(frenchKey, QCNetworkCacheReadMode::FreshOnly).body(),
             QByteArrayLiteral("generic"));
    QCOMPARE(cache.lookup(englishKey, QCNetworkCacheReadMode::FreshOnly).body(),
             QByteArrayLiteral("english"));
}

void TestQCNetworkCache::testMemoryCacheRejectsUnpartitionedAuthenticatedEntry()
{
    QCNetworkMemoryCache cache;
    const QUrl url(QStringLiteral("https://example.com/private"));
    const auto key = requestKeyFor(url, HttpMethod::Get, {}, {}, true);

    cache.insert(key, QByteArrayLiteral("secret"), metadataFor(url, 60));

    QCOMPARE(cache.lookup(key, QCNetworkCacheReadMode::AllowStale).status(),
             QCNetworkCacheLookupStatus::Miss);
}

void TestQCNetworkCache::testMemoryCacheReplacementKeepsExactCapacity()
{
    QCNetworkMemoryCache cache;
    const QUrl url(QStringLiteral("https://example.com/replacement"));
    const auto key  = requestKeyFor(url);
    const auto meta = metadataFor(url, 60);

    cache.insert(key, QByteArray(64, 'a'), meta);
    cache.insert(key, QByteArray(7, 'b'), meta);

    QCOMPARE(cache.cacheSize(), qint64(7));
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
    meta.setUrl(url);
    meta.setSize(data.size());

    cache.insert(requestKeyFor(url), data, meta);

    const auto retrieved = cache.lookup(requestKeyFor(url), QCNetworkCacheReadMode::FreshOnly);
    QCOMPARE(retrieved.status(), QCNetworkCacheLookupStatus::FreshHit);
    QCOMPARE(retrieved.body(), data);
}

void TestQCNetworkCache::testDiskCachePersistence()
{
    const QString cacheDir = uniqueCacheDir(m_tempDir, QStringLiteral("disk-persistence"));
    QUrl url("https://example.com/persistent");
    QByteArray data("Persistent data");

    {
        QCNetworkDiskCache cache;
        cache.setCacheDirectory(cacheDir);

        QCNetworkCacheMetadata meta;
        meta.setUrl(url);
        cache.insert(requestKeyFor(url), data, meta);

        const QStringList entries = QDir(cacheDir).entryList({QStringLiteral("*.qce")}, QDir::Files);
        QCOMPARE(entries.size(), 1);
        QFile envelope(QDir(cacheDir).filePath(entries.constFirst()));
        QVERIFY(envelope.open(QIODevice::ReadOnly));
        const QByteArray fixedHeader = envelope.read(52);
        QCOMPARE(fixedHeader.size(), 52);
        QCOMPARE(qFromBigEndian<quint32>(fixedHeader.constData()), quint32(0x51434348U));
        QCOMPARE(qFromBigEndian<quint16>(fixedHeader.constData() + 4), quint16(4));
    }

    // 创建新实例，数据应该仍然存在
    {
        QCNetworkDiskCache cache;
        cache.setCacheDirectory(cacheDir);

        const auto retrieved = cache.lookup(requestKeyFor(url), QCNetworkCacheReadMode::FreshOnly);
        QCOMPARE(retrieved.status(), QCNetworkCacheLookupStatus::FreshHit);
        QCOMPARE(retrieved.body(), data);
    }
}

void TestQCNetworkCache::testDiskCacheRemove()
{
    QCNetworkDiskCache cache;
    cache.setCacheDirectory(m_tempDir.path());

    QUrl url("https://example.com/remove");
    QByteArray data("To be removed");

    QCNetworkCacheMetadata meta;
    meta.setUrl(url);

    cache.insert(requestKeyFor(url), data, meta);
    QVERIFY(cache.lookup(requestKeyFor(url), QCNetworkCacheReadMode::FreshOnly).hit());

    QVERIFY(cache.remove(requestKeyFor(url)));
    QCOMPARE(cache.lookup(requestKeyFor(url), QCNetworkCacheReadMode::FreshOnly).status(),
             QCNetworkCacheLookupStatus::Miss);
}

void TestQCNetworkCache::testDiskCacheClear()
{
    QCNetworkDiskCache cache;
    cache.setCacheDirectory(m_tempDir.path());

    cache.insert(requestKeyFor(QUrl("https://example.com/1")), "data1", QCNetworkCacheMetadata());
    cache.insert(requestKeyFor(QUrl("https://example.com/2")), "data2", QCNetworkCacheMetadata());

    QVERIFY(cache.cacheSize() > 0);

    QVERIFY(cache.clear().isSuccess());
    QCOMPARE(cache.cacheSize(), 0);
}

void TestQCNetworkCache::testDiskCacheClearResult()
{
    const QString cacheDir = uniqueCacheDir(m_tempDir, QStringLiteral("disk-clear-result"));
    QCNetworkDiskCache cache;
    cache.setCacheDirectory(cacheDir);
    cache.insert(requestKeyFor(QUrl("https://example.com/clear-result/one")),
                 QByteArrayLiteral("one"),
                 QCNetworkCacheMetadata());
    cache.insert(requestKeyFor(QUrl("https://example.com/clear-result/two")),
                 QByteArrayLiteral("two"),
                 QCNetworkCacheMetadata());

    const QStringList entries = QDir(cacheDir).entryList({QStringLiteral("*.qce")}, QDir::Files);
    QCOMPARE(entries.size(), 2);
    qputenv("QCURL_TEST_DISK_CACHE_REMOVE_FAILURE_BASENAME", entries.constFirst().toUtf8());
    const QCNetworkCacheClearResult partial = cache.clear();
    qunsetenv("QCURL_TEST_DISK_CACHE_REMOVE_FAILURE_BASENAME");

    QCOMPARE(partial.status(), QCNetworkCacheClearResult::Status::PartialFailure);
    QCOMPARE(partial.removedCount(), qint64(1));
    QCOMPARE(partial.failedCount(), qint64(1));
    QCOMPARE(partial.remainingBytes(), qint64(3));
    QCOMPARE(partial.errorCode(), QCNetworkCacheClearResult::ErrorCode::EntryRemovalFailed);
    QVERIFY(!partial.errorMessage().isEmpty());
    QCOMPARE(cache.cacheSize(), partial.remainingBytes());

    const QCNetworkCacheClearResult success = cache.clear();
    QCOMPARE(success.status(), QCNetworkCacheClearResult::Status::Success);
    QCOMPARE(success.removedCount(), qint64(1));
    QCOMPARE(success.failedCount(), qint64(0));
    QCOMPARE(success.remainingBytes(), qint64(0));
}

void TestQCNetworkCache::testDiskCacheSizeLimit()
{
    QCNetworkDiskCache cache;
    cache.setCacheDirectory(m_tempDir.path());
    cache.setMaxCacheSize(100); // 100 字节

    QByteArray largeData(200, 'Y');
    QCNetworkCacheMetadata meta;

    cache.insert(requestKeyFor(QUrl("https://example.com/large")), largeData, meta);

    // 数据太大，不应该被缓存
    QCOMPARE(cache
                 .lookup(requestKeyFor(QUrl("https://example.com/large")),
                         QCNetworkCacheReadMode::FreshOnly)
                 .status(),
             QCNetworkCacheLookupStatus::Miss);
}

void TestQCNetworkCache::testDiskCacheLookupZeroByteEntry()
{
    QCNetworkDiskCache cache;
    cache.setCacheDirectory(m_tempDir.path());

    const QUrl url(QStringLiteral("https://example.com/zero-disk"));
    QCNetworkCacheMetadata meta;
    meta.setUrl(url);
    meta.setExpirationDate(QDateTime::currentDateTime().addSecs(60));

    cache.insert(requestKeyFor(url), QByteArray(), meta);

    const auto result = cache.lookup(requestKeyFor(url), QCNetworkCacheReadMode::FreshOnly);
    QCOMPARE(result.status(), QCNetworkCacheLookupStatus::FreshHit);
    QVERIFY(result.body().isEmpty());
    QCOMPARE(result.metadata().url(), url);
}

void TestQCNetworkCache::testDiskCacheLookupHonorsReadModeForExpiredEntry()
{
    QCNetworkDiskCache cache;
    cache.setCacheDirectory(uniqueCacheDir(m_tempDir, QStringLiteral("disk-lookup-expired")));

    const QUrl url(QStringLiteral("https://example.com/expired-disk-lookup"));
    const QByteArray body("expired disk body");
    cache.insert(requestKeyFor(url), body, metadataFor(url, -60));

    const auto stale = cache.lookup(requestKeyFor(url), QCNetworkCacheReadMode::AllowStale);
    QCOMPARE(stale.status(), QCNetworkCacheLookupStatus::StaleHit);
    QCOMPARE(stale.metadata().url(), url);
    QCOMPARE(stale.body(), body);
    QVERIFY(stale.hit());

    const auto fresh = cache.lookup(requestKeyFor(url), QCNetworkCacheReadMode::FreshOnly);
    QCOMPARE(fresh.status(), QCNetworkCacheLookupStatus::Miss);
    QVERIFY(!fresh.hit());
}

void TestQCNetworkCache::testDiskCacheLookupDistinguishesExpiredZeroByteHit()
{
    QCNetworkDiskCache cache;
    cache.setCacheDirectory(uniqueCacheDir(m_tempDir, QStringLiteral("disk-lookup-zero")));

    const QUrl url(QStringLiteral("https://example.com/expired-disk-zero"));
    cache.insert(requestKeyFor(url), QByteArray(), metadataFor(url, -60));

    const auto result = cache.lookup(requestKeyFor(url), QCNetworkCacheReadMode::AllowStale);
    QCOMPARE(result.status(), QCNetworkCacheLookupStatus::StaleHit);
    QCOMPARE(result.metadata().url(), url);
    QVERIFY(result.body().isEmpty());
    QVERIFY(result.hit());
}

void TestQCNetworkCache::testDiskCacheLookupMissesWhenDataFileMissing()
{
    const QString cacheDir = uniqueCacheDir(m_tempDir, QStringLiteral("disk-lookup-missing-data"));
    QCNetworkDiskCache cache;
    cache.setCacheDirectory(cacheDir);

    const QUrl url(QStringLiteral("https://example.com/expired-disk-missing-data"));
    cache.insert(requestKeyFor(url),
                 QByteArrayLiteral("metadata without body"),
                 metadataFor(url, -60));

    const auto dataFiles = QDir(cacheDir).entryList(QStringList{QStringLiteral("*.qce")},
                                                    QDir::Files);
    QCOMPARE(dataFiles.size(), 1);
    QVERIFY(QFile::remove(QDir(cacheDir).filePath(dataFiles.first())));

    const auto result = cache.lookup(requestKeyFor(url), QCNetworkCacheReadMode::AllowStale);
    QCOMPARE(result.status(), QCNetworkCacheLookupStatus::Miss);
    QVERIFY(!result.hit());
}

void TestQCNetworkCache::testDiskCacheSeparatesVaryAndAuthenticationPartition()
{
    QCNetworkDiskCache cache;
    cache.setCacheDirectory(uniqueCacheDir(m_tempDir, QStringLiteral("disk-structured-key")));
    const QUrl url(QStringLiteral("https://example.com/private-catalog"));
    QCNetworkCacheMetadata meta = metadataFor(url, 60);
    meta.setVaryHeaderNames({QByteArrayLiteral("accept-language")});

    const auto tenantEnglish = requestKeyFor(url,
                                             HttpMethod::Get,
                                             {{QByteArrayLiteral("Accept-Language"),
                                               QByteArrayLiteral("en")}},
                                             QByteArrayLiteral("tenant-a"),
                                             true);
    const auto tenantFrench  = requestKeyFor(url,
                                             HttpMethod::Get,
                                             {{QByteArrayLiteral("Accept-Language"),
                                               QByteArrayLiteral("fr")}},
                                             QByteArrayLiteral("tenant-a"),
                                             true);
    const auto otherTenant   = requestKeyFor(url,
                                             HttpMethod::Get,
                                             {{QByteArrayLiteral("Accept-Language"),
                                               QByteArrayLiteral("en")}},
                                             QByteArrayLiteral("tenant-b"),
                                             true);

    cache.insert(tenantEnglish, QByteArrayLiteral("en-a"), meta);
    cache.insert(tenantFrench, QByteArrayLiteral("fr-a"), meta);
    cache.insert(otherTenant, QByteArrayLiteral("en-b"), meta);

    QCOMPARE(cache.lookup(tenantEnglish, QCNetworkCacheReadMode::FreshOnly).body(),
             QByteArrayLiteral("en-a"));
    QCOMPARE(cache.lookup(tenantFrench, QCNetworkCacheReadMode::FreshOnly).body(),
             QByteArrayLiteral("fr-a"));
    QCOMPARE(cache.lookup(otherTenant, QCNetworkCacheReadMode::FreshOnly).body(),
             QByteArrayLiteral("en-b"));
}

void TestQCNetworkCache::testDiskCachePrefersMostRecentMatchingResponseDate()
{
    const QString cacheDir = uniqueCacheDir(m_tempDir, QStringLiteral("disk-vary-transition"));
    QCNetworkDiskCache cache;
    cache.setCacheDirectory(cacheDir);
    const QUrl url(QStringLiteral("https://example.com/vary-transition"));
    const QDateTime responseTime = QDateTime::currentDateTimeUtc();

    QCNetworkCacheMetadata genericMetadata = metadataFor(url, 60);
    genericMetadata.setHeader(QByteArrayLiteral("Date"),
                              responseTime.addSecs(-2).toString(Qt::RFC2822Date).toLatin1());
    cache.insert(requestKeyFor(url), QByteArrayLiteral("generic"), genericMetadata);

    const QStringList genericFiles = QDir(cacheDir).entryList({QStringLiteral("*.qce")},
                                                              QDir::Files);
    QCOMPARE(genericFiles.size(), 1);
    QFile genericFile(QDir(cacheDir).filePath(genericFiles.constFirst()));
    QVERIFY(genericFile.open(QIODevice::ReadWrite));
    QVERIFY(genericFile.setFileTime(QDateTime::fromString(QStringLiteral("2000-01-01T00:00:00Z"),
                                                          Qt::ISODate),
                                    QFileDevice::FileModificationTime));
    genericFile.close();

    QCNetworkCacheMetadata localizedMetadata = metadataFor(url, 60);
    localizedMetadata.setHeader(QByteArrayLiteral("Date"),
                                responseTime.addSecs(-1).toString(Qt::RFC2822Date).toLatin1());
    localizedMetadata.setVaryHeaderNames({QByteArrayLiteral("accept-language")});
    const auto englishKey = requestKeyFor(url,
                                          HttpMethod::Get,
                                          {{QByteArrayLiteral("accept-language"),
                                            QByteArrayLiteral("en")}});
    cache.insert(englishKey, QByteArrayLiteral("english"), localizedMetadata);

    QCOMPARE(cache.lookup(englishKey, QCNetworkCacheReadMode::FreshOnly).body(),
             QByteArrayLiteral("english"));
}

void TestQCNetworkCache::testDiskCacheRejectsSensitiveResponseMetadata()
{
    const QString cacheDir = uniqueCacheDir(m_tempDir, QStringLiteral("disk-envelope"));
    QCNetworkDiskCache cache;
    cache.setCacheDirectory(cacheDir);
    const QUrl url(QStringLiteral("https://example.com/private"));
    const auto key              = requestKeyFor(url,
                                                HttpMethod::Get,
                                                {{QByteArrayLiteral("Authorization"),
                                                  QByteArrayLiteral("Bearer request-secret")}},
                                                QByteArrayLiteral("partition-secret"),
                                                true);
    QCNetworkCacheMetadata meta = metadataFor(url, 60);
    meta.setVaryHeaderNames({QByteArrayLiteral("authorization")});
    meta.setHeader(QByteArrayLiteral("Set-Cookie"), QByteArrayLiteral("session=response-secret"));

    cache.insert(key, QByteArrayLiteral("public-body"), meta);

    const QDir dir(cacheDir);
    const QStringList envelopes = dir.entryList({QStringLiteral("*.qce")}, QDir::Files);
    QVERIFY(envelopes.isEmpty());
    QVERIFY(
        dir.entryList({QStringLiteral("*.data"), QStringLiteral("*.meta")}, QDir::Files).isEmpty());
    QCOMPARE(cache.lookup(key, QCNetworkCacheReadMode::FreshOnly).status(),
             QCNetworkCacheLookupStatus::Miss);
    QCOMPARE(cache.cacheSize(), qint64(0));
}

void TestQCNetworkCache::testDiskCacheAtomicReplacementPreservesPreviousEntry()
{
    QCNetworkDiskCache cache;
    cache.setCacheDirectory(uniqueCacheDir(m_tempDir, QStringLiteral("disk-atomic-replace")));
    const QUrl url(QStringLiteral("https://example.com/atomic"));
    const auto key  = requestKeyFor(url);
    const auto meta = metadataFor(url, 60);
    cache.insert(key, QByteArrayLiteral("old-body"), meta);

    qputenv("QCURL_TEST_DISK_CACHE_COMMIT_FAILURE", "1");
    cache.insert(key, QByteArrayLiteral("new-body"), meta);
    qunsetenv("QCURL_TEST_DISK_CACHE_COMMIT_FAILURE");

    const auto result = cache.lookup(key, QCNetworkCacheReadMode::FreshOnly);
    QCOMPARE(result.body(), QByteArrayLiteral("old-body"));
    QCOMPARE(cache.cacheSize(), qint64(QByteArrayLiteral("old-body").size()));
}

void TestQCNetworkCache::testDiskCacheCleansLegacyAndCorruptEntries()
{
    const QString cacheDir = uniqueCacheDir(m_tempDir, QStringLiteral("disk-cleanup"));
    QFile legacyData(QDir(cacheDir).filePath(QStringLiteral("legacy.data")));
    QVERIFY(legacyData.open(QIODevice::WriteOnly));
    legacyData.write("legacy");
    legacyData.close();
    QFile legacyMeta(QDir(cacheDir).filePath(QStringLiteral("legacy.meta")));
    QVERIFY(legacyMeta.open(QIODevice::WriteOnly));
    legacyMeta.write("legacy");
    legacyMeta.close();
    QFile corrupt(QDir(cacheDir).filePath(QStringLiteral("corrupt.qce")));
    QVERIFY(corrupt.open(QIODevice::WriteOnly));
    corrupt.write("not-an-envelope");
    corrupt.close();

    QCNetworkDiskCache cache;
    cache.setCacheDirectory(cacheDir);

    QCOMPARE(cache.cacheSize(), qint64(0));
    QVERIFY(QDir(cacheDir).entryList(QDir::Files).isEmpty());
}

void TestQCNetworkCache::testDiskCacheMixedCorruptionKeepsExactSize()
{
    const QString cacheDir = uniqueCacheDir(m_tempDir, QStringLiteral("disk-mixed-corruption"));
    const QUrl url(QStringLiteral("https://example.com/valid-cache-entry"));
    const QByteArray body = QByteArrayLiteral("valid-body");

    {
        QCNetworkDiskCache cache;
        cache.setCacheDirectory(cacheDir);
        cache.insert(requestKeyFor(url), body, metadataFor(url, 60));
        QCOMPARE(cache.cacheSize(), qint64(body.size()));
    }

    QDir dir(cacheDir);
    const QStringList validFiles = dir.entryList({QStringLiteral("*.qce")}, QDir::Files);
    QCOMPARE(validFiles.size(), 1);
    QFile validFile(dir.filePath(validFiles.constFirst()));
    QVERIFY(validFile.open(QIODevice::ReadWrite));
    QVERIFY(validFile.setFileTime(QDateTime::fromString(QStringLiteral("2000-01-01T00:00:00Z"),
                                                        Qt::ISODate),
                                  QFileDevice::FileModificationTime));
    validFile.close();

    QFile corruptFile(dir.filePath(QStringLiteral("corrupt.qce")));
    QVERIFY(corruptFile.open(QIODevice::WriteOnly));
    QCOMPARE(corruptFile.write("not-an-envelope"), qint64(15));
    corruptFile.close();

    QCNetworkDiskCache cache;
    cache.setCacheDirectory(cacheDir);
    QCOMPARE(cache.cacheSize(), qint64(body.size()));
    QCOMPARE(cache.lookup(requestKeyFor(url), QCNetworkCacheReadMode::FreshOnly).body(), body);
    QVERIFY(!QFileInfo::exists(corruptFile.fileName()));
}

void TestQCNetworkCache::testDiskCacheLookupRefreshesLruOrder()
{
    const QString cacheDir = uniqueCacheDir(m_tempDir, QStringLiteral("disk-lru-lookup"));
    QCNetworkDiskCache cache;
    cache.setCacheDirectory(cacheDir);
    cache.setMaxCacheSize(8);

    const QUrl firstUrl(QStringLiteral("https://example.com/lru-first"));
    const QUrl secondUrl(QStringLiteral("https://example.com/lru-second"));
    const QUrl thirdUrl(QStringLiteral("https://example.com/lru-third"));
    cache.insert(requestKeyFor(firstUrl), QByteArrayLiteral("aaaa"), metadataFor(firstUrl, 60));

    QDir dir(cacheDir);
    const QStringList firstFiles = dir.entryList({QStringLiteral("*.qce")}, QDir::Files);
    QCOMPARE(firstFiles.size(), 1);
    const QString firstPath = dir.filePath(firstFiles.constFirst());

    cache.insert(requestKeyFor(secondUrl), QByteArrayLiteral("bbbb"), metadataFor(secondUrl, 60));
    const QStringList bothFiles = dir.entryList({QStringLiteral("*.qce")}, QDir::Files);
    QCOMPARE(bothFiles.size(), 2);
    QString secondPath;
    for (const QString &file : bothFiles) {
        const QString path = dir.filePath(file);
        if (path != firstPath) {
            secondPath = path;
            break;
        }
    }
    QVERIFY(!secondPath.isEmpty());

    QFile firstFile(firstPath);
    QVERIFY(firstFile.open(QIODevice::ReadWrite));
    QVERIFY(firstFile.setFileTime(QDateTime::fromString(QStringLiteral("2000-01-01T00:00:00Z"),
                                                        Qt::ISODate),
                                  QFileDevice::FileModificationTime));
    firstFile.close();
    QFile secondFile(secondPath);
    QVERIFY(secondFile.open(QIODevice::ReadWrite));
    QVERIFY(secondFile.setFileTime(QDateTime::fromString(QStringLiteral("2010-01-01T00:00:00Z"),
                                                         Qt::ISODate),
                                   QFileDevice::FileModificationTime));
    secondFile.close();

    QCOMPARE(cache.lookup(requestKeyFor(firstUrl), QCNetworkCacheReadMode::FreshOnly).body(),
             QByteArrayLiteral("aaaa"));
    cache.insert(requestKeyFor(thirdUrl), QByteArrayLiteral("cccc"), metadataFor(thirdUrl, 60));

    QCOMPARE(cache.lookup(requestKeyFor(firstUrl), QCNetworkCacheReadMode::FreshOnly).body(),
             QByteArrayLiteral("aaaa"));
    QCOMPARE(cache.lookup(requestKeyFor(secondUrl), QCNetworkCacheReadMode::FreshOnly).status(),
             QCNetworkCacheLookupStatus::Miss);
    QCOMPARE(cache.lookup(requestKeyFor(thirdUrl), QCNetworkCacheReadMode::FreshOnly).body(),
             QByteArrayLiteral("cccc"));
}

void TestQCNetworkCache::testDiskCacheEnvelopeV4RejectsMalformedLengths()
{
    const QString cacheDir = uniqueCacheDir(m_tempDir, QStringLiteral("disk-envelope-v4-bounds"));
    const QUrl url(QStringLiteral("https://example.com/hostile-envelope"));
    const auto key           = requestKeyFor(url);
    const QByteArray primary = Internal::cachePrimaryDigest(key);
    const QByteArray variant = Internal::cacheVariantDigest(key, {});

    const QList<QByteArray> hostileEntries{
        envelopeFor({}, 3),
        envelopeFor({}, 99),
        envelopeFor({}, 4, std::numeric_limits<quint64>::max()),
        envelopeFor(QByteArray(31, '\0')),
        envelopeFor(minimalPayload(primary, variant, 0, 65536)),
        envelopeFor(minimalPayload(primary, variant, 256 * 1024 * 1024ULL)),
    };

    for (qsizetype index = 0; index < hostileEntries.size(); ++index) {
        const QString path = QDir(cacheDir).filePath(
            QStringLiteral("%1-hostile-%2.qce").arg(QString::fromLatin1(primary.toHex())).arg(index));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write(hostileEntries.at(index)), qint64(hostileEntries.at(index).size()));
    }

    QCNetworkDiskCache cache;
    cache.setCacheDirectory(cacheDir);
    QCOMPARE(cache.lookup(key, QCNetworkCacheReadMode::FreshOnly).status(),
             QCNetworkCacheLookupStatus::Miss);
    QCOMPARE(cache.cacheSize(), qint64(0));
    QVERIFY(QDir(cacheDir).entryList({QStringLiteral("*.qce")}, QDir::Files).isEmpty());
}

void TestQCNetworkCache::testManagerUsesExplicitDiskCacheInjection()
{
    QCNetworkAccessManager manager;

    auto *diskCache = new QCNetworkDiskCache(&manager);
    diskCache->setCacheDirectory(uniqueCacheDir(m_tempDir, QStringLiteral("explicit-disk")));
    diskCache->setMaxCacheSize(1024 * 1024);

    manager.setCache(diskCache);
    QCOMPARE(manager.cache(), diskCache);
    QCOMPARE(diskCache->maxCacheSize(), qint64(1024 * 1024));

    auto *memoryCache = new QCNetworkMemoryCache(&manager);
    QPointer<QObject> diskGuard(diskCache);
    manager.setCache(memoryCache);

    QCOMPARE(manager.cache(), memoryCache);
    QVERIFY(!diskGuard.isNull());
}

void TestQCNetworkCache::testManagerCachePointerClearsOnDestruction()
{
    QCNetworkAccessManager manager;
    auto *cache = new QCNetworkMemoryCache;

    manager.setCache(cache);
    QCOMPARE(manager.cache(), cache);

    delete cache;
    QCOMPARE(manager.cache(), nullptr);
}

void TestQCNetworkCache::testCustomCacheSubclassCanImplementLookupOnly()
{
    LookupOnlyCache cache;

    const auto result = cache.lookup(requestKeyFor(
                                         QUrl(QStringLiteral("https://example.com/custom-cache"))),
                                     QCNetworkCacheReadMode::FreshOnly);
    QCOMPARE(result.status(), QCNetworkCacheLookupStatus::Miss);
}

// ============================================================================
// HTTP 缓存头解析测试
// ============================================================================

void TestQCNetworkCache::testCacheControlMaxAge()
{
    const QList<QCNetworkCacheMetadata::RawHeaderPair> headers{
        qMakePair(QByteArrayLiteral("Cache-Control"), QByteArrayLiteral("max-age=3600")),
    };

    const QDateTime expiration = QCNetworkCache::parseExpirationDate(headers);
    QVERIFY(expiration.isValid());

    // 应该在未来约 1 小时
    qint64 secondsToExpire = QDateTime::currentDateTime().secsTo(expiration);
    QVERIFY(secondsToExpire > 3500 && secondsToExpire < 3700);
}

void TestQCNetworkCache::testCacheControlNoStore()
{
    const QList<QCNetworkCacheMetadata::RawHeaderPair> headers{
        qMakePair(QByteArrayLiteral("Cache-Control"), QByteArrayLiteral("no-store")),
    };

    QVERIFY(!QCNetworkCache::isCacheable(headers));
}

void TestQCNetworkCache::testCacheControlNoCacheAndMaxAgeZeroAreStoredStale()
{
    const QList<QCNetworkCacheMetadata::RawHeaderPair> noCacheHeaders{
        qMakePair(QByteArrayLiteral("cAcHe-CoNtRoL"), QByteArrayLiteral("no-cache")),
    };
    QVERIFY(QCNetworkCache::isCacheable(noCacheHeaders));

    const QList<QCNetworkCacheMetadata::RawHeaderPair> maxAgeHeaders{
        qMakePair(QByteArrayLiteral("CACHE-CONTROL"), QByteArrayLiteral("public, max-age=0")),
    };
    const QDateTime expiration = QCNetworkCache::parseExpirationDate(maxAgeHeaders);
    QVERIFY(expiration.isValid());
    QVERIFY(expiration <= QDateTime::currentDateTime().addSecs(1));
}

void TestQCNetworkCache::testVaryStarIsNotCacheable()
{
    const QList<QCNetworkCacheMetadata::RawHeaderPair> headers{
        qMakePair(QByteArrayLiteral("VaRy"), QByteArrayLiteral("*")),
    };

    QVERIFY(!QCNetworkCache::isCacheable(headers));
}

void TestQCNetworkCache::testRawCacheControlCombinesMixedCaseLines()
{
    const QList<QCNetworkCacheMetadata::RawHeaderPair> headers{
        qMakePair(QByteArrayLiteral("Cache-Control"), QByteArrayLiteral("public, no-store")),
        qMakePair(QByteArrayLiteral("cAcHe-CoNtRoL"), QByteArrayLiteral("max-age=3600")),
    };

    QVERIFY(!QCNetworkCache::isCacheable(headers));
}

void TestQCNetworkCache::testRawVaryCombinesMixedCaseLines()
{
    QList<QCNetworkCacheMetadata::RawHeaderPair> headers{
        qMakePair(QByteArrayLiteral("Vary"), QByteArrayLiteral("Accept-Language")),
        qMakePair(QByteArrayLiteral("vArY"), QByteArrayLiteral("Accept-Encoding, User-Agent")),
    };

    QCOMPARE(QCNetworkCache::varyHeaderNames(headers),
             QList<QByteArray>({QByteArrayLiteral("accept-encoding"),
                                QByteArrayLiteral("accept-language"),
                                QByteArrayLiteral("user-agent")}));
    QVERIFY(QCNetworkCache::isCacheable(headers));

    headers.append(qMakePair(QByteArrayLiteral("VARY"), QByteArrayLiteral("*")));
    QVERIFY(!QCNetworkCache::isCacheable(headers));
}

void TestQCNetworkCache::testRawSetCookieIsNeverCacheableOrFresh()
{
    const QList<QCNetworkCacheMetadata::RawHeaderPair> headers{
        qMakePair(QByteArrayLiteral("Cache-Control"), QByteArrayLiteral("public, max-age=3600")),
        qMakePair(QByteArrayLiteral("Set-Cookie"), QByteArrayLiteral("a=1; HttpOnly")),
        qMakePair(QByteArrayLiteral("set-cookie"), QByteArrayLiteral("b=2; Secure")),
    };
    QVERIFY(!QCNetworkCache::isCacheable(headers));

    QCNetworkCacheMetadata metadata;
    metadata.setRawHeaders(headers);
    metadata.setExpirationDate(QDateTime::currentDateTimeUtc().addSecs(3600));
    QVERIFY(!metadata.isValid());
}

void TestQCNetworkCache::testStandardHttpDateWithGmtZoneIsCacheable()
{
    const QList<QCNetworkCacheMetadata::RawHeaderPair> headers{
        qMakePair(QByteArrayLiteral("Date"), QByteArrayLiteral("Fri, 31 Jul 2026 14:47:56 GMT")),
    };

    QVERIFY(QCNetworkCache::isCacheable(headers));
}

void TestQCNetworkCache::testConflictingOrInvalidDateAgeIsNotCacheable()
{
    const QByteArray date = QDateTime::currentDateTimeUtc().toString(Qt::RFC2822Date).toLatin1();
    const QList<QCNetworkCacheMetadata::RawHeaderPair> duplicateDate{
        qMakePair(QByteArrayLiteral("Date"), date),
        qMakePair(QByteArrayLiteral("dAtE"), date),
    };
    QVERIFY(!QCNetworkCache::isCacheable(duplicateDate));

    const QList<QCNetworkCacheMetadata::RawHeaderPair> duplicateAge{
        qMakePair(QByteArrayLiteral("Age"), QByteArrayLiteral("1")),
        qMakePair(QByteArrayLiteral("aGe"), QByteArrayLiteral("2")),
    };
    QVERIFY(!QCNetworkCache::isCacheable(duplicateAge));

    const QList<QCNetworkCacheMetadata::RawHeaderPair> invalidAge{
        qMakePair(QByteArrayLiteral("Age"), QByteArrayLiteral("-1")),
    };
    QVERIFY(!QCNetworkCache::isCacheable(invalidAge));
}

void TestQCNetworkCache::testAgeInt64MaxUsesSaturatedFreshnessArithmetic()
{
    const QList<QCNetworkCacheMetadata::RawHeaderPair> headers{
        qMakePair(QByteArrayLiteral("Cache-Control"), QByteArrayLiteral("max-age=60")),
        qMakePair(QByteArrayLiteral("Age"), QByteArray::number(std::numeric_limits<qint64>::max())),
    };
    QVERIFY(QCNetworkCache::isCacheable(headers));

    QCNetworkCacheMetadata metadata;
    metadata.setRawHeaders(headers);
    metadata.setCorrectedInitialAgeSeconds(std::numeric_limits<qint64>::max());
    metadata.setResponseTime(QDateTime::currentDateTimeUtc().addSecs(-1));
    metadata.setExpirationDate(QDateTime::currentDateTimeUtc());
    QCOMPARE(metadata.currentAgeSeconds(), std::numeric_limits<qint64>::max());
    QVERIFY(!metadata.isValid());
}

void TestQCNetworkCache::testExpiresHeader()
{
    const QDateTime futureDate = QDateTime::currentDateTime().addSecs(7200);
    const QList<QCNetworkCacheMetadata::RawHeaderPair> headers{
        qMakePair(QByteArrayLiteral("Expires"), futureDate.toString(Qt::RFC2822Date).toLatin1()),
    };

    const QDateTime expiration = QCNetworkCache::parseExpirationDate(headers);
    QVERIFY(expiration.isValid());

    qint64 diff = expiration.secsTo(futureDate);
    QVERIFY(qAbs(diff) < 5); // 允许 5 秒误差
}

QTEST_MAIN(TestQCNetworkCache)
#include "tst_QCNetworkCache.moc"
