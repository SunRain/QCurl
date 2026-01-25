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

#include <QScopedPointer>
#include <QtTest/QtTest>

using namespace QCurl;

class TestQCNetworkResponseHeadersOffline final : public QObject
{
    Q_OBJECT

private slots:
    void testHeaderUnfoldFromRawHeaderData();
};

void TestQCNetworkResponseHeadersOffline::testHeaderUnfoldFromRawHeaderData()
{
    QCNetworkAccessManager manager;
    QCNetworkMockHandler mock;
    manager.setMockHandler(&mock);

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
    QScopedPointer<QCNetworkReply> reply(manager.sendGetSync(request));
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

QTEST_MAIN(TestQCNetworkResponseHeadersOffline)
#include "tst_QCNetworkResponseHeadersOffline.moc"
