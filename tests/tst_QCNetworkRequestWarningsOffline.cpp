/**
 * @file tst_QCNetworkRequestWarningsOffline.cpp
 * @brief 请求配置冲突告警（capabilityWarnings）纯离线门禁（MockHandler）
 *
 * 说明：
 * - 不启动任何本地服务端，不绑定端口。
 * - 仅验证“配置冲突”在离线回放路径下仍可诊断（避免真实网络路径才提示）。
 */

#include <QtTest/QtTest>

#include "QCNetworkAccessManager.h"
#include "QCNetworkMockHandler.h"
#include "QCNetworkReply.h"
#include "QCNetworkRequest.h"

using namespace QCurl;

class TestQCNetworkRequestWarningsOffline final : public QObject
{
    Q_OBJECT

private slots:
    void testAcceptEncodingConflictWarnings();
};

void TestQCNetworkRequestWarningsOffline::testAcceptEncodingConflictWarnings()
{
    QCNetworkAccessManager manager;
    QCNetworkMockHandler mock;
    manager.setMockHandler(&mock);

    const QUrl url(QStringLiteral("https://example.com/offline/warnings_accept_encoding"));
    mock.mockResponse(HttpMethod::Get, url, QByteArrayLiteral("ok"), 200);

    QCNetworkRequest request(url);
    request.setRawHeader("Accept-Encoding", "gzip");
    request.setAutoDecompressionEnabled(true);

    auto *reply = manager.sendGetSync(request);
    QVERIFY(reply);
    QCOMPARE(reply->error(), NetworkError::NoError);

    const QStringList warnings = reply->capabilityWarnings();
    bool found = false;
    for (const QString &w : warnings) {
        if (w.contains(QStringLiteral("Accept-Encoding")) && w.contains(QStringLiteral("不会自动解压"))) {
            found = true;
            break;
        }
    }
    QVERIFY(found);

    delete reply;
}

QTEST_MAIN(TestQCNetworkRequestWarningsOffline)
#include "tst_QCNetworkRequestWarningsOffline.moc"

