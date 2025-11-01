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

QTEST_MAIN(TestQCNetworkRequest)
#include "tst_QCNetworkRequest.moc"
