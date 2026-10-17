/**
 * @file tst_QCNetworkResponseHeadersOffline.cpp
 * @brief 响应头解析（unfold）纯离线门禁（MockHandler）
 *
 * 说明：
 * - 不启动任何本地服务端，不绑定端口。
 * - 通过 QCNetworkMockHandler 注入原始响应头块，验证折叠行（SP/HTAB）unfold 规则。
 */

#include "QCNetworkAccessManager.h"
#include "QCNetworkMockHandler.h"
#include "QCNetworkReply.h"
#include "QCNetworkRequest.h"
#include "private/QCNetworkReplyResponse_p.h"
#include "qcnetwork_managed_reply_wait_helper.h"
#include "qcnetwork_mock_test_support.h"

#include <QScopedPointer>
#include <QtTest/QtTest>

#include <limits>

using namespace QCurl;

class TestQCNetworkResponseHeadersOffline final : public QObject
{
    Q_OBJECT

public:
    TestQCNetworkResponseHeadersOffline() = default;

private Q_SLOTS:
    void testHeaderUnfoldFromRawHeaderData();
    void testFinalHeaderBlockOnlyExposesLastResponseHeaders();
    void retryAfterParsing_data();
    void retryAfterParsing();
    void retryAfterHttpDateUsesQueryClock();

private:
    Q_DISABLE_COPY_MOVE(TestQCNetworkResponseHeadersOffline)
};

void TestQCNetworkResponseHeadersOffline::testHeaderUnfoldFromRawHeaderData()
{
    QCNetworkAccessManager manager;
    QCNetworkMockHandler mock;
    QCurl::TestSupport::setMockHandler(manager, &mock);

    const QUrl url(QStringLiteral("https://example.com/offline/headers_unfold"));
    const QByteArray rawHeaderData = QByteArrayLiteral("HTTP/1.1 200 OK\r\n"
                                                       "X-Test:  a\r\n"
                                                       "\tb\r\n"
                                                       "  c  \r\n"
                                                       "Y: v\r\n"
                                                       "\r\n");

    mock.mockResponse(HttpMethod::Get,
                      url,
                      QByteArrayLiteral("ok"),
                      200,
                      QMap<QByteArray, QByteArray>(),
                      rawHeaderData);

    QCNetworkRequest request(url);
    QScopedPointer<QCNetworkReply> reply(TestSupport::sendWaitedAsyncTestReply(manager, request));
    QVERIFY(reply);
    QCOMPARE(reply->error(), NetworkError::NoError);

    QCOMPARE(reply->rawHeaderData(), rawHeaderData);

    QByteArray xTestValue;
    QByteArray yValue;
    const QList<RawHeaderPair> headers = reply->rawHeaders();
    for (const RawHeaderPair &h : headers) {
        if (h.first == QByteArrayLiteral("X-Test")) {
            xTestValue = h.second;
        } else if (h.first == QByteArrayLiteral("Y")) {
            yValue = h.second;
        }
    }

    QCOMPARE(xTestValue, QByteArrayLiteral("a b c"));
    QCOMPARE(yValue, QByteArrayLiteral("v"));
}

void TestQCNetworkResponseHeadersOffline::testFinalHeaderBlockOnlyExposesLastResponseHeaders()
{
    QCNetworkAccessManager manager;
    QCNetworkMockHandler mock;
    QCurl::TestSupport::setMockHandler(manager, &mock);

    const QUrl url(QStringLiteral("https://example.com/offline/final_headers"));
    const QByteArray rawHeaderData = QByteArrayLiteral("HTTP/1.1 100 Continue\r\n"
                                                       "X-Interim: warmup\r\n"
                                                       "Retry-After: 999\r\n"
                                                       "\r\n"
                                                       "HTTP/1.1 302 Found\r\n"
                                                       "Location: https://example.com/next\r\n"
                                                       "X-Redirect: hop\r\n"
                                                       "\r\n"
                                                       "HTTP/1.1 200 OK\r\n"
                                                       "Content-Type: application/json\r\n"
                                                       "X-Final: done\r\n"
                                                       "\r\n");

    mock.mockResponse(HttpMethod::Get,
                      url,
                      QByteArrayLiteral("{\"ok\":true}"),
                      200,
                      QMap<QByteArray, QByteArray>(),
                      rawHeaderData);

    QCNetworkRequest request(url);
    QScopedPointer<QCNetworkReply> reply(TestSupport::sendWaitedAsyncTestReply(manager, request));
    QVERIFY(reply);
    QCOMPARE(reply->error(), NetworkError::NoError);

    QCOMPARE(reply->rawHeaderData(), rawHeaderData);
    QCOMPARE(reply->httpStatusCode(), 200);
    QCOMPARE(reply->rawHeader(QByteArrayLiteral("Content-Type")),
             QByteArrayLiteral("application/json"));
    QCOMPARE(reply->rawHeader(QByteArrayLiteral("X-Final")), QByteArrayLiteral("done"));
    QVERIFY(reply->hasRawHeader(QByteArrayLiteral("Content-Type")));
    QVERIFY(!reply->hasRawHeader(QByteArrayLiteral("X-Interim")));
    QVERIFY(!reply->hasRawHeader(QByteArrayLiteral("Location")));
    QVERIFY(!reply->retryAfterDelay());
    QCOMPARE(reply->retryAttemptCount(), 0);
    QCOMPARE(reply->cumulativeRetryDelay().count(), 0);

    const QList<RawHeaderPair> headers = reply->rawHeaders();
    QCOMPARE(headers.size(), 2);
    QCOMPARE(headers.at(0).first, QByteArrayLiteral("Content-Type"));
    QCOMPARE(headers.at(0).second, QByteArrayLiteral("application/json"));
    QCOMPARE(headers.at(1).first, QByteArrayLiteral("X-Final"));
    QCOMPARE(headers.at(1).second, QByteArrayLiteral("done"));
}

void TestQCNetworkResponseHeadersOffline::retryAfterParsing_data()
{
    QTest::addColumn<QByteArray>("value");
    QTest::addColumn<qint64>("expectedMs");
    QTest::newRow("empty") << QByteArray() << qint64(-1);
    QTest::newRow("invalid") << QByteArray("not-a-date") << qint64(-1);
    QTest::newRow("negative") << QByteArray("-1") << qint64(-1);
    QTest::newRow("zero") << QByteArray("0") << qint64(0);
    QTest::newRow("trimmed-seconds") << QByteArray(" 2 ") << qint64(2000);
    const auto maximumSeconds = std::numeric_limits<qint64>::max() / 1000;
    QTest::newRow("maximum-seconds")
        << QByteArray::number(maximumSeconds) << qint64(maximumSeconds * 1000);
    QTest::newRow("milliseconds-overflow") << QByteArray::number(maximumSeconds + 1) << qint64(-1);
    QTest::newRow("integer-overflow") << QByteArray("999999999999999999999999999") << qint64(-1);
}

void TestQCNetworkResponseHeadersOffline::retryAfterParsing()
{
    QFETCH(QByteArray, value);
    QFETCH(qint64, expectedMs);
    const QList<RawHeaderPair> headers{{QByteArrayLiteral("rEtRy-AfTeR"), value}};
    const auto result = Internal::parseReplyRetryAfterDelay(headers, 1700000000);
    QCOMPARE(result.has_value(), expectedMs >= 0);
    if (result) {
        QCOMPARE(result->count(), expectedMs);
    }
    QVERIFY(!Internal::parseReplyRetryAfterDelay({}, 1700000000));
}

void TestQCNetworkResponseHeadersOffline::retryAfterHttpDateUsesQueryClock()
{
    const QList<RawHeaderPair> headers{
        {QByteArrayLiteral("Retry-After"), QByteArrayLiteral("Wed, 21 Oct 2015 07:28:00 GMT")}};
    constexpr std::time_t kDate = 1445412480;
    QCOMPARE(Internal::parseReplyRetryAfterDelay(headers, kDate - 3)->count(), 3000);
    QCOMPARE(Internal::parseReplyRetryAfterDelay(headers, kDate - 1)->count(), 1000);
    QCOMPARE(Internal::parseReplyRetryAfterDelay(headers, kDate)->count(), 0);
    QCOMPARE(Internal::parseReplyRetryAfterDelay(headers, kDate + 1)->count(), 0);
}

QTEST_MAIN(TestQCNetworkResponseHeadersOffline)
#include "tst_QCNetworkResponseHeadersOffline.moc"
