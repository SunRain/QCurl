/**
 * @file tst_QCNetworkHttpErrorMappingOffline.cpp
 * @brief MockHandler 路径下 HTTP 错误映射纯离线门禁
 *
 * 说明：
 * - 不启动任何本地服务端，不绑定端口。
 * - 仅验证 status>=400 时，QCNetworkReply 的 error/httpStatusCode/state 等可观测契约稳定。
 */

#include "QCNetworkAccessManager.h"
#include "QCNetworkError.h"
#include "QCNetworkMockHandler.h"
#include "qcnetwork_mock_test_support.h"
#include "QCNetworkReply.h"
#include "QCNetworkRequest.h"
#include "qcnetwork_managed_reply_wait_helper.h"

#include <QScopedPointer>
#include <QtTest/QtTest>

using namespace QCurl;

class TestQCNetworkHttpErrorMappingOffline final : public QObject
{
    Q_OBJECT

private slots:
    void testHttpErrorMappingFromMockStatus();
    void testHeadersRemainReadableAfterErrorFinalHeaderBlockArrives();
};

void TestQCNetworkHttpErrorMappingOffline::testHttpErrorMappingFromMockStatus()
{
    auto runCase = [](int statusCode) {
        QCNetworkAccessManager manager;
        QCNetworkMockHandler mock;
        QCurl::TestSupport::setMockHandler(manager, &mock);

        const QUrl url(QStringLiteral("https://example.com/offline/status/%1").arg(statusCode));
        mock.mockResponse(HttpMethod::Get, url, QByteArray(), statusCode);

        QCNetworkRequest request(url);
        QScopedPointer<QCNetworkReply> reply(TestSupport::sendWaitedAsyncTestReply(manager, request));
        QVERIFY(reply);

        QCOMPARE(reply->httpStatusCode(), statusCode);
        QCOMPARE(reply->state(), ReplyState::Error);
        QCOMPARE(reply->error(), fromHttpCode(statusCode));
    };

    runCase(401);
    runCase(404);
    runCase(503);
}

void TestQCNetworkHttpErrorMappingOffline::testHeadersRemainReadableAfterErrorFinalHeaderBlockArrives()
{
    QCNetworkAccessManager manager;
    QCNetworkMockHandler mock;
    QCurl::TestSupport::setMockHandler(manager, &mock);

    const int statusCode = 404;
    const QUrl url(QStringLiteral("https://example.com/offline/error_headers"));
    const QByteArray rawHeaderData = QByteArrayLiteral(
        "HTTP/1.1 100 Continue\r\n"
        "X-Ignore: warmup\r\n"
        "\r\n"
        "HTTP/1.1 404 Not Found\r\n"
        "Content-Type: application/problem+json\r\n"
        "Retry-After: 120\r\n"
        "\r\n");

    mock.mockResponse(HttpMethod::Get,
                      url,
                      QByteArrayLiteral("{\"error\":true}"),
                      statusCode,
                      QMap<QByteArray, QByteArray>(),
                      rawHeaderData);

    QCNetworkRequest request(url);
    QScopedPointer<QCNetworkReply> reply(TestSupport::sendWaitedAsyncTestReply(manager, request));
    QVERIFY(reply);

    QCOMPARE(reply->state(), ReplyState::Error);
    QCOMPARE(reply->error(), fromHttpCode(statusCode));
    QCOMPARE(reply->httpStatusCode(), statusCode);
    QCOMPARE(reply->rawHeaderData(), rawHeaderData);
    QVERIFY(reply->hasRawHeader(QByteArrayLiteral("Content-Type")));
    QVERIFY(reply->hasRawHeader(QByteArrayLiteral("Retry-After")));
    QVERIFY(!reply->hasRawHeader(QByteArrayLiteral("X-Ignore")));
    QCOMPARE(reply->rawHeader(QByteArrayLiteral("Content-Type")),
             QByteArrayLiteral("application/problem+json"));
    QCOMPARE(reply->rawHeader(QByteArrayLiteral("Retry-After")), QByteArrayLiteral("120"));

    const QList<RawHeaderPair> headers = reply->rawHeaders();
    QCOMPARE(headers.size(), 2);
    QCOMPARE(headers.at(0).first, QByteArrayLiteral("Content-Type"));
    QCOMPARE(headers.at(1).first, QByteArrayLiteral("Retry-After"));
}

QTEST_MAIN(TestQCNetworkHttpErrorMappingOffline)
#include "tst_QCNetworkHttpErrorMappingOffline.moc"
