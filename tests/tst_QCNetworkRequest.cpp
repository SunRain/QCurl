/**
 * @file tst_QCNetworkRequest.cpp
 * @brief QCNetworkRequest 单元测试
 */

#include <QtTest/QtTest>
#include "QCNetworkRequest.h"

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

    // M1：重定向策略
    void testRedirectPolicyDefaults();
    void testSetMaxRedirects();
    void testSetPostRedirectPolicy();
    void testSetAutoRefererEnabled();
    void testSetReferer();
    void testSetAllowUnrestrictedSensitiveHeadersOnRedirect();

    // M1：自动解压
    void testAutoDecompressionDefaults();
    void testSetAutoDecompressionEnabled();
    void testSetAcceptedEncodings();

    // M1：传输限速
    void testSpeedLimitDefaults();
    void testSetMaxDownloadBytesPerSec();
    void testSetMaxUploadBytesPerSec();

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
    QCOMPARE(request.rangeStart(), -1);  // 默认值

    request.setRange(0, 1024);
    QCOMPARE(request.rangeStart(), 0);
}

void TestQCNetworkRequest::testRangeEnd()
{
    QCNetworkRequest request;
    QCOMPARE(request.rangeEnd(), -1);  // 默认值

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
    QVERIFY(request.followLocation());  // 默认为 true
}

void TestQCNetworkRequest::testRedirectPolicyDefaults()
{
    QCNetworkRequest request;

    QVERIFY(!request.maxRedirects().has_value());
    QCOMPARE(request.postRedirectPolicy(), QCNetworkPostRedirectPolicy::Default);
    QVERIFY(!request.autoRefererEnabled());
    QVERIFY(request.referer().isEmpty());
    QVERIFY(!request.allowUnrestrictedSensitiveHeadersOnRedirect());
}

void TestQCNetworkRequest::testSetMaxRedirects()
{
    QCNetworkRequest request;
    request.setMaxRedirects(5);
    QVERIFY(request.maxRedirects().has_value());
    QCOMPARE(request.maxRedirects().value(), 5);

    request.setMaxRedirects(-1);
    QVERIFY(!request.maxRedirects().has_value());
}

void TestQCNetworkRequest::testSetPostRedirectPolicy()
{
    QCNetworkRequest request;
    request.setPostRedirectPolicy(QCNetworkPostRedirectPolicy::KeepPostAll);
    QCOMPARE(request.postRedirectPolicy(), QCNetworkPostRedirectPolicy::KeepPostAll);
}

void TestQCNetworkRequest::testSetAutoRefererEnabled()
{
    QCNetworkRequest request;
    QVERIFY(!request.autoRefererEnabled());

    request.setAutoRefererEnabled(true);
    QVERIFY(request.autoRefererEnabled());

    request.setAutoRefererEnabled(false);
    QVERIFY(!request.autoRefererEnabled());
}

void TestQCNetworkRequest::testSetReferer()
{
    QCNetworkRequest request;
    request.setReferer(QStringLiteral("https://example.com/"));
    QCOMPARE(request.referer(), QStringLiteral("https://example.com/"));
}

void TestQCNetworkRequest::testSetAllowUnrestrictedSensitiveHeadersOnRedirect()
{
    QCNetworkRequest request;
    QVERIFY(!request.allowUnrestrictedSensitiveHeadersOnRedirect());

    request.setAllowUnrestrictedSensitiveHeadersOnRedirect(true);
    QVERIFY(request.allowUnrestrictedSensitiveHeadersOnRedirect());
}

void TestQCNetworkRequest::testAutoDecompressionDefaults()
{
    QCNetworkRequest request;
    QVERIFY(!request.autoDecompressionEnabled());
    QVERIFY(request.acceptedEncodings().isEmpty());
}

void TestQCNetworkRequest::testSetAutoDecompressionEnabled()
{
    QCNetworkRequest request;
    request.setAutoDecompressionEnabled(true);
    QVERIFY(request.autoDecompressionEnabled());

    request.setAutoDecompressionEnabled(false);
    QVERIFY(!request.autoDecompressionEnabled());
}

void TestQCNetworkRequest::testSetAcceptedEncodings()
{
    QCNetworkRequest request;

    request.setAcceptedEncodings({QStringLiteral("gzip"), QStringLiteral("br")});
    QVERIFY(request.autoDecompressionEnabled());
    QCOMPARE(request.acceptedEncodings(), QStringList({QStringLiteral("gzip"), QStringLiteral("br")}));

    request.setAcceptedEncodings({});
    QVERIFY(!request.autoDecompressionEnabled());
    QVERIFY(request.acceptedEncodings().isEmpty());
}

void TestQCNetworkRequest::testSpeedLimitDefaults()
{
    QCNetworkRequest request;
    QVERIFY(!request.maxDownloadBytesPerSec().has_value());
    QVERIFY(!request.maxUploadBytesPerSec().has_value());
}

void TestQCNetworkRequest::testSetMaxDownloadBytesPerSec()
{
    QCNetworkRequest request;

    request.setMaxDownloadBytesPerSec(1024);
    QVERIFY(request.maxDownloadBytesPerSec().has_value());
    QCOMPARE(request.maxDownloadBytesPerSec().value(), 1024);

    request.setMaxDownloadBytesPerSec(0);
    QVERIFY(!request.maxDownloadBytesPerSec().has_value());

    request.setMaxDownloadBytesPerSec(-1);
    QVERIFY(!request.maxDownloadBytesPerSec().has_value());
}

void TestQCNetworkRequest::testSetMaxUploadBytesPerSec()
{
    QCNetworkRequest request;

    request.setMaxUploadBytesPerSec(2048);
    QVERIFY(request.maxUploadBytesPerSec().has_value());
    QCOMPARE(request.maxUploadBytesPerSec().value(), 2048);

    request.setMaxUploadBytesPerSec(0);
    QVERIFY(!request.maxUploadBytesPerSec().has_value());

    request.setMaxUploadBytesPerSec(-1);
    QVERIFY(!request.maxUploadBytesPerSec().has_value());
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
    QCOMPARE(static_cast<qint64>(request.expect100ContinueTimeout().value().count()), static_cast<qint64>(0));

    request.setExpect100ContinueTimeout(std::chrono::milliseconds(150));
    QVERIFY(request.expect100ContinueTimeout().has_value());
    QCOMPARE(static_cast<qint64>(request.expect100ContinueTimeout().value().count()), static_cast<qint64>(150));

    request.setExpect100ContinueTimeout(std::chrono::milliseconds(-1));
    QVERIFY(!request.expect100ContinueTimeout().has_value());
}

QTEST_MAIN(TestQCNetworkRequest)
#include "tst_QCNetworkRequest.moc"
