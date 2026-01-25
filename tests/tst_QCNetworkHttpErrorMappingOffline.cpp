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
#include "QCNetworkReply.h"
#include "QCNetworkRequest.h"

#include <QScopedPointer>
#include <QtTest/QtTest>

using namespace QCurl;

class TestQCNetworkHttpErrorMappingOffline final : public QObject
{
    Q_OBJECT

private slots:
    void testHttpErrorMappingFromMockStatus();
};

void TestQCNetworkHttpErrorMappingOffline::testHttpErrorMappingFromMockStatus()
{
    auto runCase = [](int statusCode) {
        QCNetworkAccessManager manager;
        QCNetworkMockHandler mock;
        manager.setMockHandler(&mock);

        const QUrl url(QStringLiteral("https://example.com/offline/status/%1").arg(statusCode));
        mock.mockResponse(HttpMethod::Get, url, QByteArray(), statusCode);

        QCNetworkRequest request(url);
        QScopedPointer<QCNetworkReply> reply(manager.sendGetSync(request));
        QVERIFY(reply);

        QCOMPARE(reply->httpStatusCode(), statusCode);
        QCOMPARE(reply->state(), ReplyState::Error);
        QCOMPARE(reply->error(), fromHttpCode(statusCode));
    };

    runCase(401);
    runCase(404);
    runCase(503);
}

QTEST_MAIN(TestQCNetworkHttpErrorMappingOffline)
#include "tst_QCNetworkHttpErrorMappingOffline.moc"
