#include "QCBlockingNetworkClient.h"
#include "QCNetworkAccessManager.h"
#include "QCNetworkReply.h"
#include "QCNetworkRequest.h"
#include "QCurlConfig.h"
#include "qcblocking_upload_echo_server.h"
#include "qcurl_http_script_server.h"

#ifdef QCURL_WEBSOCKET_SUPPORT
#include "QCWebSocket.h"
#include "QCWebSocketTestServer.h"
#include "test_websocket_evidence_utils.h"
#endif

#include <QSignalSpy>
#include <QUrlQuery>
#include <QtTest>

using namespace QCurl;

namespace {

void addUrlCases()
{
    QTest::addColumn<QString>("source");
    QTest::addColumn<QByteArray>("expectedTarget");
    QTest::addColumn<QString>("expectedQuery");
    QTest::newRow("plain") << QStringLiteral("/echo?q=plain") << QByteArray("/echo?q=plain")
                           << QStringLiteral("plain");
    QTest::newRow("space") << QStringLiteral("/echo?q=two words")
                           << QByteArray("/echo?q=two%20words") << QStringLiteral("two words");
    QTest::newRow("unicode") << QStringLiteral("/echo?q=中文")
                             << QByteArray("/echo?q=%E4%B8%AD%E6%96%87") << QStringLiteral("中文");
    QTest::newRow("encoded-space")
        << QStringLiteral("/echo?q=two%20words") << QByteArray("/echo?q=two%20words")
        << QStringLiteral("two words");
    QTest::newRow("percent") << QStringLiteral("/echo?q=50%") << QByteArray("/echo?q=50%25")
                             << QStringLiteral("50%");
    QTest::newRow("literal-escape")
        << QStringLiteral("/echo?q=%2520") << QByteArray("/echo?q=%2520") << QStringLiteral("%20");
    QTest::newRow("reserved") << QStringLiteral("/echo?q=a%26b%3Dc%23d%2Be")
                              << QByteArray("/echo?q=a%26b%3Dc%23d%2Be")
                              << QStringLiteral("a&b=c#d+e");
    QTest::newRow("plus") << QStringLiteral("/echo?q=a+b") << QByteArray("/echo?q=a+b")
                          << QStringLiteral("a+b");
    QTest::newRow("path") << QStringLiteral("/two words/中文?q=value")
                          << QByteArray("/two%20words/%E4%B8%AD%E6%96%87?q=value")
                          << QStringLiteral("value");
}

void verifyTarget(const QByteArray &target,
                  const QByteArray &expectedTarget,
                  const QString &expectedQuery)
{
    QCOMPARE(target, expectedTarget);
    const QUrlQuery query(QUrl::fromEncoded(target));
    QCOMPARE(query.queryItemValue(QStringLiteral("q"), QUrl::FullyDecoded), expectedQuery);
}

} // namespace

class TestQCNetworkUrlEncoding : public QObject
{
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(TestQCNetworkUrlEncoding)

public:
    TestQCNetworkUrlEncoding() = default;

private Q_SLOTS:
    void asynchronous_data();
    void asynchronous();
    void blocking_data();
    void blocking();
#ifdef QCURL_WEBSOCKET_SUPPORT
    void webSocket_data();
    void webSocket();
#endif
};

void TestQCNetworkUrlEncoding::asynchronous_data()
{
    addUrlCases();
}

void TestQCNetworkUrlEncoding::asynchronous()
{
    QFETCH(QString, source);
    QFETCH(QByteArray, expectedTarget);
    QFETCH(QString, expectedQuery);
    HttpScriptServer server({HttpScriptServer::response(200, "{}")});
    QVERIFY(server.start());
    const QUrl url(server.url().adjusted(QUrl::RemovePath).toString() + source);
    QCNetworkAccessManager manager;
    auto *reply = manager.get(QCNetworkRequest(url));
    QVERIFY(reply);
    QSignalSpy finished(reply, &QCNetworkReply::finished);
    QTRY_COMPARE(finished.count(), 1);
    QCOMPARE(reply->error(), NetworkError::NoError);
    QCOMPARE(server.requests().size(), 1);
    verifyTarget(server.requests().first().split(' ').at(1), expectedTarget, expectedQuery);
}

void TestQCNetworkUrlEncoding::blocking_data()
{
    addUrlCases();
}

void TestQCNetworkUrlEncoding::blocking()
{
    QFETCH(QString, source);
    QFETCH(QByteArray, expectedTarget);
    QFETCH(QString, expectedQuery);
    UploadEchoServer server;
    QVERIFY(server.start());
    QCBlockingNetworkClient::Options options;
    options.setApplicationThreadPolicy(
        QCBlockingNetworkClient::ApplicationThreadPolicy::AllowForCliOrTests);
    QCBlockingNetworkClient client(options);
    const auto result = client.get(QCNetworkRequest(server.url(source)));
    QVERIFY2(result.isSuccess(), qPrintable(result.errorMessage()));
    verifyTarget(server.lastRequest().path, expectedTarget, expectedQuery);
}

#ifdef QCURL_WEBSOCKET_SUPPORT
void TestQCNetworkUrlEncoding::webSocket_data()
{
    addUrlCases();
}

void TestQCNetworkUrlEncoding::webSocket()
{
    QFETCH(QString, source);
    QFETCH(QByteArray, expectedTarget);
    QFETCH(QString, expectedQuery);
    QCWebSocketTestServer server;
    QVERIFY2(server.start(QCWebSocketTestServer::Mode::Ws), qPrintable(server.skipReason()));
    QUrl url(server.baseUrl() + source);
    const QString caseId = QString::fromLatin1(QTest::currentDataTag());
    QUrlQuery query(url);
    query.addQueryItem(QStringLiteral("case"), caseId);
    url.setQuery(query);
    QCWebSocket socket(url, QCWebSocketOptions{});
    QSignalSpy connected(&socket, &QCWebSocket::connected);
    QSignalSpy errors(&socket, &QCWebSocket::errorOccurred);
    QSignalSpy disconnected(&socket, &QCWebSocket::disconnected);
    static_cast<void>(socket.open());
    QTRY_VERIFY(!connected.isEmpty() || !errors.isEmpty());
    QCOMPARE(connected.count(), 1);
    QVERIFY2(errors.isEmpty(), qPrintable(socket.errorString()));
    const auto handshakes = TestWebSocketEvidenceUtils::waitCaseEvents(server.artifactsPath(),
                                                                       caseId,
                                                                       QStringLiteral(
                                                                           "handshake_ok"),
                                                                       1,
                                                                       3000);
    QCOMPARE(handshakes.size(), 1);
    verifyTarget(handshakes.first().value(QStringLiteral("target")).toString().toUtf8(),
                 expectedTarget + "&case=" + caseId.toUtf8(),
                 expectedQuery);
    // 此处验证握手目标，不要求夹具为任意路径提供回显；未知路径会按 1008 关闭。
    static_cast<void>(socket.close());
    QTRY_COMPARE(disconnected.count(), 1);
}
#endif

QTEST_GUILESS_MAIN(TestQCNetworkUrlEncoding)
#include "tst_QCNetworkUrlEncoding.moc"
