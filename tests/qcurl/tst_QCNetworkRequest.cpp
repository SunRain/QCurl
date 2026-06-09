/**
 * @file tst_QCNetworkRequest.cpp
 * @brief QCNetworkRequest 单元测试
 */

#include "QCNetworkRequest.h"
#include "QCNetworkLaneKey.h"
#include "QCNetworkProxyConfig.h"
#include "QCNetworkRequestPriority.h"
#include "QCNetworkRetryPolicy.h"
#include "QCNetworkSslConfig.h"
#include "QCNetworkTimeoutConfig.h"

#include <QtTest/QtTest>

#include <chrono>

using namespace QCurl;

class TestQCNetworkRequest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();

    // 构造函数测试
    void testConstructor();
    void testConstructorWithUrl();

    // URL 测试
    void testUrl();

    // Header 测试
    void testSetRawHeader();
    void testRawHeader();
    void testRawHeaderList();

    // Range 测试
    void testSetRange();
    void testRangeStart();
    void testRangeEnd();

    // Follow Location 测试
    void testSetFollowLocation();
    void testFollowLocation();

    void testRedirectConfig();
    void testTransferConfig();

    // lane 测试
    void testLaneDefaults();
    void testSetLane();
    void testSetLaneAcceptsInvalidLaneAsFailClosedSnapshot();
    void testCopyAndEqualityPreserveLane();
    void testEqualityIgnoresExecutionConfigFamily();

    // P1：Expect: 100-continue
    void testExpect100ContinueTimeoutDefaults();
    void testSetExpect100ContinueTimeout();
};

void TestQCNetworkRequest::initTestCase()
{
    qDebug() << "初始化 QCNetworkRequest 测试套件";
}

void TestQCNetworkRequest::cleanupTestCase()
{
    qDebug() << "清理 QCNetworkRequest 测试套件";
}

void TestQCNetworkRequest::testConstructor()
{
    QCNetworkRequest request;
    QVERIFY(request.url().isEmpty());
}

void TestQCNetworkRequest::testConstructorWithUrl()
{
    QUrl testUrl("https://example.com/api");
    QCNetworkRequest request(testUrl);
    QCOMPARE(request.url(), testUrl);
}

void TestQCNetworkRequest::testUrl()
{
    QUrl testUrl("https://httpbin.org/get");
    QCNetworkRequest request(testUrl);
    QCOMPARE(request.url(), testUrl);
    QCOMPARE(request.url().toString(), QStringLiteral("https://httpbin.org/get"));
}

void TestQCNetworkRequest::testSetRawHeader()
{
    QCNetworkRequest request;
    request.setRawHeader("User-Agent", "QCurl-Test/1.0");
    request.setRawHeader("Accept", "application/json");

    QCOMPARE(request.rawHeader("User-Agent"), QByteArray("QCurl-Test/1.0"));
    QCOMPARE(request.rawHeader("Accept"), QByteArray("application/json"));
}

void TestQCNetworkRequest::testRawHeader()
{
    QCNetworkRequest request;
    QVERIFY(request.rawHeader("NonExistent").isEmpty());

    request.setRawHeader("Content-Type", "text/html");
    QCOMPARE(request.rawHeader("Content-Type"), QByteArray("text/html"));
}

void TestQCNetworkRequest::testRawHeaderList()
{
    QCNetworkRequest request;
    request.setRawHeader("Header1", "Value1");
    request.setRawHeader("Header2", "Value2");

    QList<QByteArray> headers = request.rawHeaderList();
    QVERIFY(headers.contains("Header1"));
    QVERIFY(headers.contains("Header2"));
    QCOMPARE(headers.size(), 2);
}

void TestQCNetworkRequest::testSetRange()
{
    QCNetworkRequest request;
    request.setRange(100, 500);
    QCOMPARE(request.rangeStart(), 100);
    QCOMPARE(request.rangeEnd(), 500);
}

void TestQCNetworkRequest::testRangeStart()
{
    QCNetworkRequest request;
    QCOMPARE(request.rangeStart(), -1); // 默认值

    request.setRange(0, 1024);
    QCOMPARE(request.rangeStart(), 0);
}

void TestQCNetworkRequest::testRangeEnd()
{
    QCNetworkRequest request;
    QCOMPARE(request.rangeEnd(), -1); // 默认值

    request.setRange(0, 1024);
    QCOMPARE(request.rangeEnd(), 1024);
}

void TestQCNetworkRequest::testSetFollowLocation()
{
    QCNetworkRequest request;
    request.setFollowLocation(true);
    QVERIFY(request.followLocation());

    request.setFollowLocation(false);
    QVERIFY(!request.followLocation());
}

void TestQCNetworkRequest::testFollowLocation()
{
    QCNetworkRequest request;
    QVERIFY(request.followLocation()); // 默认为 true
}

void TestQCNetworkRequest::testRedirectConfig()
{
    QCNetworkRequest request;
    QVERIFY(request.followLocation());
    QVERIFY(!request.maxRedirects().has_value());
    QCOMPARE(request.postRedirectPolicy(), QCNetworkPostRedirectPolicy::Default);
    QVERIFY(!request.autoRefererEnabled());
    QVERIFY(request.referer().isEmpty());
    QVERIFY(!request.allowUnrestrictedSensitiveHeadersOnRedirect());

    QCNetworkRedirectConfig config;
    config.setFollowLocation(false);
    config.setMaxRedirects(4);
    config.setPostRedirectPolicy(QCNetworkPostRedirectPolicy::KeepPost302);
    config.setAutoRefererEnabled(true);
    config.setReferer(QStringLiteral("https://example.com/from"));
    config.setAllowUnrestrictedSensitiveHeadersOnRedirect(true);
    request.setRedirectConfig(config);

    QCOMPARE(request.followLocation(), false);
    QCOMPARE(request.maxRedirects().value_or(-1), 4);
    QCOMPARE(request.postRedirectPolicy(), QCNetworkPostRedirectPolicy::KeepPost302);
    QVERIFY(request.autoRefererEnabled());
    QCOMPARE(request.referer(), QStringLiteral("https://example.com/from"));
    QVERIFY(request.allowUnrestrictedSensitiveHeadersOnRedirect());

    request.setMaxRedirects(-1);
    QVERIFY(!request.maxRedirects().has_value());
}

void TestQCNetworkRequest::testTransferConfig()
{
    QCNetworkRequest request;
    QVERIFY(!request.autoDecompressionEnabled());
    QVERIFY(request.acceptedEncodings().isEmpty());
    QVERIFY(!request.maxDownloadBytesPerSec().has_value());
    QVERIFY(!request.maxUploadBytesPerSec().has_value());
    QVERIFY(!request.expect100ContinueTimeout().has_value());

    QCNetworkTransferConfig config;
    config.setAcceptedEncodings({QStringLiteral("gzip"), QStringLiteral("br")});
    config.setMaxDownloadBytesPerSec(4096);
    config.setMaxUploadBytesPerSec(2048);
    config.setBackpressureLimitBytes(32 * 1024);
    config.setBackpressureResumeBytes(8 * 1024);
    config.setExpect100ContinueTimeout(std::chrono::milliseconds(250));
    config.setIpResolve(QCNetworkIpResolve::Ipv6);
    config.setAllowedProtocols({QStringLiteral("http"), QStringLiteral("https")});
    config.setAllowedRedirectProtocols({QStringLiteral("https")});
    config.setUnsupportedSecurityOptionPolicy(QCUnsupportedSecurityOptionPolicy::Warn);
    request.setTransferConfig(config);

    QVERIFY(request.autoDecompressionEnabled());
    QCOMPARE(request.acceptedEncodings(), QStringList({QStringLiteral("gzip"), QStringLiteral("br")}));
    QCOMPARE(request.maxDownloadBytesPerSec().value_or(-1), 4096);
    QCOMPARE(request.maxUploadBytesPerSec().value_or(-1), 2048);
    QCOMPARE(request.backpressureLimitBytes(), qint64(32 * 1024));
    QCOMPARE(request.backpressureResumeBytes(), qint64(8 * 1024));
    QCOMPARE(request.expect100ContinueTimeout()->count(), 250);
    QCOMPARE(request.ipResolve().value(), QCNetworkIpResolve::Ipv6);
    QCOMPARE(request.allowedProtocols()->size(), 2);
    QCOMPARE(request.allowedRedirectProtocols()->size(), 1);
    QCOMPARE(request.unsupportedSecurityOptionPolicy(), QCUnsupportedSecurityOptionPolicy::Warn);

    request.setAcceptedEncodings({});
    QVERIFY(!request.autoDecompressionEnabled());
    request.setMaxDownloadBytesPerSec(-1);
    request.setMaxUploadBytesPerSec(-1);
    request.setExpect100ContinueTimeout(std::chrono::milliseconds(-1));
    QVERIFY(!request.maxDownloadBytesPerSec().has_value());
    QVERIFY(!request.maxUploadBytesPerSec().has_value());
    QVERIFY(!request.expect100ContinueTimeout().has_value());
}

void TestQCNetworkRequest::testLaneDefaults()
{
    QCNetworkRequest request;
    QVERIFY(request.lane().isDefault());
}

void TestQCNetworkRequest::testSetLane()
{
    QCNetworkRequest request;

    request.setLane(QCNetworkLaneKey::control());
    QCOMPARE(request.lane(), QCNetworkLaneKey::control());

    request.setLane(QCNetworkLaneKey::defaultLane());
    QVERIFY(request.lane().isDefault());
}

void TestQCNetworkRequest::testSetLaneAcceptsInvalidLaneAsFailClosedSnapshot()
{
    QCNetworkRequest request;
    request.setLane(QCNetworkLaneKey::control());

    request.setLane(QCNetworkLaneKey());

    QVERIFY(!request.lane().isValid());
}

void TestQCNetworkRequest::testCopyAndEqualityPreserveLane()
{
    QCNetworkRequest lhs(QUrl(QStringLiteral("https://example.com/api")));
    lhs.setFollowLocation(false);
    lhs.setLane(QCNetworkLaneKey::control());

    // lane 是请求标识的一部分：copy/equality 必须保留它，才能支撑 lane-aware scheduler contract。
    QCNetworkRequest rhs(lhs);
    QCOMPARE(rhs.lane(), QCNetworkLaneKey::control());
    QVERIFY(lhs == rhs);

    rhs.setLane(QCNetworkLaneKey::transfer());
    QVERIFY(lhs != rhs);
}

void TestQCNetworkRequest::testEqualityIgnoresExecutionConfigFamily()
{
    QCNetworkRequest lhs(QUrl(QStringLiteral("https://example.com/api")));
    lhs.setFollowLocation(false);
    lhs.setLane(QCNetworkLaneKey::control());
    lhs.setRawHeader(QByteArrayLiteral("X-Test"), QByteArrayLiteral("lhs"));

    QCNetworkRequest rhs(lhs);

    QCNetworkSslConfig sslConfig;
    sslConfig.setPinnedPublicKey(QStringLiteral("sha256//consumer"));
    rhs.setSslConfig(sslConfig);

    QCNetworkProxyConfig proxyConfig;
    proxyConfig.setType(QCNetworkProxyConfig::ProxyType::Http);
    proxyConfig.setHostName(QStringLiteral("proxy.example.com"));
    proxyConfig.setPort(8080);
    rhs.setProxyConfig(proxyConfig);

    QCNetworkTimeoutConfig timeoutConfig;
    timeoutConfig.setTotalTimeout(std::chrono::seconds(30));
    rhs.setTimeoutConfig(timeoutConfig);

    QCNetworkRetryPolicy retryPolicy(3, std::chrono::milliseconds(250));
    rhs.setRetryPolicy(retryPolicy);
    rhs.setPriority(QCNetworkRequestPriority::High);

    // operator== 只比较 request identity/routing 子集，不把 execution config family 纳入比较。
    QVERIFY(lhs == rhs);
}

void TestQCNetworkRequest::testExpect100ContinueTimeoutDefaults()
{
    QCNetworkRequest request;
    QVERIFY(!request.expect100ContinueTimeout().has_value());
}

void TestQCNetworkRequest::testSetExpect100ContinueTimeout()
{
    QCNetworkRequest request;

    request.setExpect100ContinueTimeout(std::chrono::milliseconds(0));
    QVERIFY(request.expect100ContinueTimeout().has_value());
    QCOMPARE(static_cast<qint64>(request.expect100ContinueTimeout().value().count()),
             static_cast<qint64>(0));

    request.setExpect100ContinueTimeout(std::chrono::milliseconds(150));
    QVERIFY(request.expect100ContinueTimeout().has_value());
    QCOMPARE(static_cast<qint64>(request.expect100ContinueTimeout().value().count()),
             static_cast<qint64>(150));

    request.setExpect100ContinueTimeout(std::chrono::milliseconds(-1));
    QVERIFY(!request.expect100ContinueTimeout().has_value());
}

QTEST_MAIN(TestQCNetworkRequest)
#include "tst_QCNetworkRequest.moc"
