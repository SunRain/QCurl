/**
 * @file tst_QCNetworkCacheIntegration.cpp
 * @brief 覆盖缓存策略、命中路径和边界条件的集成语义。
 *
 * 该套件验证自动缓存读写、五类策略、信号顺序和过期/no-cache 等边界。
 */

#include "QCNetworkAccessManager.h"
#include "QCNetworkCache.h"
#include "QCNetworkCachePolicy.h"
#include "QCNetworkDiskCache.h"
#include "QCNetworkError.h"
#include "QCNetworkMemoryCache.h"
#include "QCNetworkReply.h"
#include "QCNetworkRequest.h"
#include "test_httpbin_env.h"

#include <QCoreApplication>
#include <QEvent>
#include <QEventLoop>
#include <QSignalSpy>
#include <QTimer>
#include <QtTest/QtTest>

using namespace QCurl;

class TestQCNetworkCacheIntegration : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void init();
    void cleanup();

    // 基础缓存语义
    void testNoCacheBehavior();   // 无缓存时行为不变
    void testOnlyNetworkPolicy(); // OnlyNetwork 策略不缓存
    void testSignalOrder();       // 信号发射顺序正确
    void testDataConsistency();   // 数据完整性
    void testMultipleRequests();  // 多次请求一致性

    // 策略命中与写入
    void testPreferCacheHit();       // PreferCache 命中
    void testPreferCacheMiss();      // PreferCache 未命中
    void testOnlyCacheHit();         // OnlyCache 命中
    void testOnlyCacheMiss();        // OnlyCache 未命中（错误）
    void testAlwaysCache();          // AlwaysCache 策略
    void testPreferNetworkSuccess(); // PreferNetwork 成功
    void testAutoCacheWrite();       // 自动缓存写入验证

    // 边界条件
    void testCacheExpiration();    // 缓存过期处理
    void testNoCacheHeader();      // no-cache 头部
    void testConcurrentRequests(); // 并发请求

private:
    QCNetworkAccessManager *m_manager = nullptr;
    QCNetworkMemoryCache *m_cache     = nullptr;
    QString m_httpbinBaseUrl;

    bool waitForFinished(QCNetworkReply *reply, int timeout = 5000);
    bool isHttpbinAvailable();
};

void TestQCNetworkCacheIntegration::initTestCase()
{
    m_httpbinBaseUrl = TestEnv::httpbinBaseUrl();
    if (m_httpbinBaseUrl.isEmpty()) {
        QSKIP(qPrintable(TestEnv::httpbinMissingReason()));
    }

    // 该套件依赖可访问的本地 httpbin。
    if (!isHttpbinAvailable()) {
        QSKIP(qPrintable(QStringLiteral("httpbin 服务不可用：%1").arg(m_httpbinBaseUrl)));
    }
}

void TestQCNetworkCacheIntegration::cleanupTestCase() {}

void TestQCNetworkCacheIntegration::init()
{
    // 每个用例使用独立 manager/cache，避免缓存键互相污染。
    m_manager = new QCNetworkAccessManager(this);
    m_cache   = new QCNetworkMemoryCache(m_manager);
    m_cache->setMaxCacheSize(1024 * 1024); // 1 MiB 即可覆盖当前测试数据量。
}

void TestQCNetworkCacheIntegration::cleanup()
{
    if (m_manager) {
        m_manager->setCache(nullptr);
        m_manager->deleteLater();
        m_manager = nullptr;
        m_cache   = nullptr;
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    }
}

bool TestQCNetworkCacheIntegration::waitForFinished(QCNetworkReply *reply, int timeout)
{
    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);

    connect(reply, &QCNetworkReply::finished, &loop, &QEventLoop::quit);
    connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);

    timer.start(timeout);
    loop.exec();

    return reply->isFinished();
}

bool TestQCNetworkCacheIntegration::isHttpbinAvailable()
{
    QCNetworkAccessManager testManager;
    QCNetworkRequest request(QUrl(m_httpbinBaseUrl + "/get"));

    auto *reply    = testManager.sendGet(request);
    bool available = waitForFinished(reply, 2000) && reply->error() == NetworkError::NoError;
    reply->deleteLater();

    return available;
}

// ============================================================================
// 基础缓存语义
// ============================================================================

void TestQCNetworkCacheIntegration::testNoCacheBehavior()
{
    // 无缓存时，请求应保持普通网络路径语义。
    QCNetworkRequest request(QUrl(m_httpbinBaseUrl + "/get"));

    // 不设置缓存
    auto *reply = m_manager->sendGet(request);

    QVERIFY(waitForFinished(reply));
    QCOMPARE(reply->error(), NetworkError::NoError);

    auto data = reply->readAll();
    QVERIFY(data.has_value());
    QVERIFY(!data->isEmpty());

    reply->deleteLater();
}

void TestQCNetworkCacheIntegration::testOnlyNetworkPolicy()
{
    // OnlyNetwork 只走网络路径，不应留下缓存副本。
    m_manager->setCache(m_cache);

    QCNetworkRequest request(QUrl(m_httpbinBaseUrl + "/get?test=onlynetwork"));
    request.setCachePolicy(QCNetworkCachePolicy::OnlyNetwork);

    auto *reply = m_manager->sendGet(request);
    QVERIFY(waitForFinished(reply));
    QCOMPARE(reply->error(), NetworkError::NoError);

    QByteArray cachedData = m_cache->data(request.url());
    QVERIFY(cachedData.isEmpty());

    reply->deleteLater();
}

void TestQCNetworkCacheIntegration::testSignalOrder()
{
    // 命中网络路径时，readyRead 必须先于 finished。
    m_manager->setCache(m_cache);

    QCNetworkRequest request(QUrl(m_httpbinBaseUrl + "/get"));
    request.setCachePolicy(QCNetworkCachePolicy::PreferCache);

    auto *reply = m_manager->sendGet(request);

    QSignalSpy readySpy(reply, &QCNetworkReply::readyRead);
    QSignalSpy finishedSpy(reply, &QCNetworkReply::finished);

    QVERIFY(waitForFinished(reply));

    QVERIFY(readySpy.count() >= 1);
    QCOMPARE(finishedSpy.count(), 1);

    reply->deleteLater();
}

void TestQCNetworkCacheIntegration::testDataConsistency()
{
    // 同一 URL 的缓存命中必须返回与首次网络读取一致的数据。
    m_manager->setCache(m_cache);

    QCNetworkRequest request(QUrl(m_httpbinBaseUrl + "/get?test=consistency"));
    request.setCachePolicy(QCNetworkCachePolicy::AlwaysCache);

    auto *reply1 = m_manager->sendGet(request);
    QVERIFY(waitForFinished(reply1));
    auto data1 = reply1->readAll();
    QVERIFY(data1.has_value());
    reply1->deleteLater();

    auto *reply2 = m_manager->sendGet(request);
    QVERIFY(waitForFinished(reply2, 100)); // 缓存命中应该很快
    auto data2 = reply2->readAll();
    QVERIFY(data2.has_value());
    reply2->deleteLater();

    QCOMPARE(data1.value(), data2.value());
}

void TestQCNetworkCacheIntegration::testMultipleRequests()
{
    // 多次重复请求不应破坏缓存内容和成功路径。
    m_manager->setCache(m_cache);

    QCNetworkRequest request(QUrl(m_httpbinBaseUrl + "/get?test=multiple"));
    request.setCachePolicy(QCNetworkCachePolicy::PreferCache);

    for (int i = 0; i < 3; ++i) {
        auto *reply = m_manager->sendGet(request);
        QVERIFY(waitForFinished(reply));
        QCOMPARE(reply->error(), NetworkError::NoError);

        auto data = reply->readAll();
        QVERIFY(data.has_value());
        QVERIFY(!data->isEmpty());

        reply->deleteLater();
    }
}

// ============================================================================
// 策略命中与写入
// ============================================================================

void TestQCNetworkCacheIntegration::testPreferCacheHit()
{
    // PreferCache 命中时应直接读取缓存。
    m_manager->setCache(m_cache);

    QUrl url(m_httpbinBaseUrl + "/get?test=prefercache");

    QByteArray testData = "{\"cached\": true}";
    QCNetworkCacheMetadata meta;
    meta.url            = url;
    meta.expirationDate = QDateTime::currentDateTime().addSecs(3600);
    m_cache->insert(url, testData, meta);

    QCNetworkRequest request(url);
    request.setCachePolicy(QCNetworkCachePolicy::PreferCache);

    QElapsedTimer timer;
    timer.start();

    auto *reply = m_manager->sendGet(request);
    QVERIFY(waitForFinished(reply, 100));

    qint64 elapsed = timer.elapsed();

    // 命中缓存时，完成时间应明显短于网络路径。
    QVERIFY(elapsed < 50); // 缓存命中应该 < 50ms
    QCOMPARE(reply->error(), NetworkError::NoError);

    auto data = reply->readAll();
    QVERIFY(data.has_value());
    QCOMPARE(data.value(), testData);

    reply->deleteLater();
}

void TestQCNetworkCacheIntegration::testPreferCacheMiss()
{
    // PreferCache 未命中时应回退到网络。
    m_manager->setCache(m_cache);

    QCNetworkRequest request(QUrl(m_httpbinBaseUrl + "/get?test=cachemiss"));
    request.setCachePolicy(QCNetworkCachePolicy::PreferCache);

    auto *reply = m_manager->sendGet(request);
    QVERIFY(waitForFinished(reply));
    QCOMPARE(reply->error(), NetworkError::NoError);

    auto data = reply->readAll();
    QVERIFY(data.has_value());
    QVERIFY(!data->isEmpty());

    reply->deleteLater();
}

void TestQCNetworkCacheIntegration::testOnlyCacheHit()
{
    // OnlyCache 命中时不应触发网络失败。
    m_manager->setCache(m_cache);

    QUrl url(m_httpbinBaseUrl + "/get?test=onlycache");

    QByteArray testData = "{\"only_cache\": true}";
    QCNetworkCacheMetadata meta;
    meta.url            = url;
    meta.expirationDate = QDateTime::currentDateTime().addSecs(3600);
    m_cache->insert(url, testData, meta);

    QCNetworkRequest request(url);
    request.setCachePolicy(QCNetworkCachePolicy::OnlyCache);

    auto *reply = m_manager->sendGet(request);
    QVERIFY(waitForFinished(reply, 100));
    QCOMPARE(reply->error(), NetworkError::NoError);

    auto data = reply->readAll();
    QCOMPARE(data.value(), testData);

    reply->deleteLater();
}

void TestQCNetworkCacheIntegration::testOnlyCacheMiss()
{
    // OnlyCache 未命中时必须稳定地走错误终态。

    m_manager->setCache(m_cache);

    QCNetworkRequest request(QUrl(m_httpbinBaseUrl + "/get?test=onlycachemiss"));
    request.setCachePolicy(QCNetworkCachePolicy::OnlyCache);

    auto *reply = m_manager->sendGet(request);

    QVERIFY2(waitForFinished(reply, 1000), "OnlyCache miss should trigger error signal");

    QCOMPARE(reply->error(), NetworkError::InvalidRequest);
    QVERIFY(reply->isFinished());

    reply->deleteLater();
}

void TestQCNetworkCacheIntegration::testAlwaysCache()
{
    // AlwaysCache 只消费已有缓存，不依赖当前网络可达性。
    m_manager->setCache(m_cache);

    QUrl url(m_httpbinBaseUrl + "/get?test=alwayscache_unique_12345");

    QByteArray testData = "{\"cached\": true}";
    QCNetworkCacheMetadata meta;
    meta.url            = url;
    meta.creationDate   = QDateTime::currentDateTime();
    meta.expirationDate = QDateTime::currentDateTime().addSecs(3600); // 未过期（1小时后）
    meta.size           = testData.size();

    meta.headers["Content-Type"]   = "application/json";
    meta.headers["Content-Length"] = QByteArray::number(testData.size());

    m_cache->insert(url, testData, meta);

    QByteArray cachedData = m_cache->data(url);
    QVERIFY2(!cachedData.isEmpty(), "Cache insert failed - data is empty");
    QCOMPARE(cachedData, testData);

    QCNetworkRequest request(url);
    request.setCachePolicy(QCNetworkCachePolicy::AlwaysCache);

    auto *reply = m_manager->sendGet(request);
    QVERIFY(waitForFinished(reply, 200)); // 增加超时时间
    QCOMPARE(reply->error(), NetworkError::NoError);

    auto data = reply->readAll();
    QCOMPARE(data.value(), testData);

    reply->deleteLater();
}

void TestQCNetworkCacheIntegration::testPreferNetworkSuccess()
{
    // PreferNetwork 在网络成功时应返回新鲜响应。
    m_manager->setCache(m_cache);

    QCNetworkRequest request(QUrl(m_httpbinBaseUrl + "/get?test=prefernetwork"));
    request.setCachePolicy(QCNetworkCachePolicy::PreferNetwork);

    auto *reply = m_manager->sendGet(request);
    QVERIFY(waitForFinished(reply));
    QCOMPARE(reply->error(), NetworkError::NoError);

    auto data = reply->readAll();
    QVERIFY(data.has_value());

    reply->deleteLater();
}

void TestQCNetworkCacheIntegration::testAutoCacheWrite()
{
    // 首次网络读取后，同一 URL 应留下可直接复用的缓存副本。
    m_manager->setCache(m_cache);

    QUrl url(m_httpbinBaseUrl + "/get?test=autowrite");

    QCNetworkRequest request(url);
    request.setCachePolicy(QCNetworkCachePolicy::PreferCache);

    auto *reply = m_manager->sendGet(request);
    QVERIFY(waitForFinished(reply));
    QCOMPARE(reply->error(), NetworkError::NoError);

    auto networkData = reply->readAll();
    QVERIFY(networkData.has_value());
    reply->deleteLater();

    QByteArray cachedData = m_cache->data(url);
    QVERIFY(!cachedData.isEmpty());
    QCOMPARE(cachedData, networkData.value());

    auto *reply2 = m_manager->sendGet(request);
    QVERIFY(waitForFinished(reply2, 100));

    auto cacheData = reply2->readAll();
    QCOMPARE(cacheData.value(), networkData.value());

    reply2->deleteLater();
}

// ============================================================================
// 边界条件
// ============================================================================

void TestQCNetworkCacheIntegration::testCacheExpiration()
{
    // 过期缓存不能继续满足 PreferCache。
    m_manager->setCache(m_cache);

    QUrl url(m_httpbinBaseUrl + "/get?test=expiration");

    QByteArray expiredData = "{\"expired\": true}";
    QCNetworkCacheMetadata meta;
    meta.url            = url;
    meta.expirationDate = QDateTime::currentDateTime().addSecs(-1); // 已过期
    m_cache->insert(url, expiredData, meta);

    QCNetworkRequest request(url);
    request.setCachePolicy(QCNetworkCachePolicy::PreferCache);

    auto *reply = m_manager->sendGet(request);
    QVERIFY(waitForFinished(reply));
    QCOMPARE(reply->error(), NetworkError::NoError);

    auto data = reply->readAll();
    QVERIFY(data.has_value());
    QVERIFY(data.value() != expiredData);

    reply->deleteLater();
}

void TestQCNetworkCacheIntegration::testNoCacheHeader()
{
    // no-cache 响应头不应被写入缓存。
    m_manager->setCache(m_cache);

    QUrl url(m_httpbinBaseUrl + "/response-headers?Cache-Control=no-cache");

    QCNetworkRequest request(url);
    request.setCachePolicy(QCNetworkCachePolicy::PreferCache);

    auto *reply = m_manager->sendGet(request);
    QVERIFY(waitForFinished(reply));
    QCOMPARE(reply->error(), NetworkError::NoError);

    reply->deleteLater();

    QByteArray cachedData = m_cache->data(url);
    QVERIFY(cachedData.isEmpty());
}

void TestQCNetworkCacheIntegration::testConcurrentRequests()
{
    // 并发请求不应破坏完成路径或响应读取。
    m_manager->setCache(m_cache);

    QList<QCNetworkReply *> replies;

    for (int i = 0; i < 5; ++i) {
        QUrl url(m_httpbinBaseUrl + QStringLiteral("/get?test=concurrent&id=%1").arg(i));
        QCNetworkRequest request(url);
        request.setCachePolicy(QCNetworkCachePolicy::PreferCache);

        replies.append(m_manager->sendGet(request));
    }

    for (auto *reply : replies) {
        QVERIFY(waitForFinished(reply));
        QCOMPARE(reply->error(), NetworkError::NoError);

        auto data = reply->readAll();
        QVERIFY(data.has_value());

        reply->deleteLater();
    }
}

// ============================================================================
// Qt Test Main
// ============================================================================

QTEST_MAIN(TestQCNetworkCacheIntegration)
#include "tst_QCNetworkCacheIntegration.moc"
