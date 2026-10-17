#include "QCNetworkAccessManager.h"
#include "QCNetworkHttpVersion.h"
#include "QCNetworkReply.h"
#include "QCNetworkRequest.h"
#include "QCNetworkSslConfig.h"
#include "qcurl_native_multi_probe.h"
#include "test_source_paths.h"
#include "tst_QCNetworkShareHandle.h"

#include <QScopeGuard>
#include <QSignalSpy>
#include <QTcpServer>
#include <QtTest>

using namespace QCurl;

namespace {

std::optional<QByteArray> bodyFor(QCNetworkAccessManager *manager, const QCNetworkRequest &request)
{
    auto *reply = manager->get(request);
    QSignalSpy done(reply, &QCNetworkReply::finished);
    if ((!reply->isFinished() && !done.wait(4000)) || reply->error() != NetworkError::NoError) {
        qWarning() << reply->errorString();
        return std::nullopt;
    }
    const auto body = reply->readAll();
    reply->deleteLater();
    return body;
}

QStringList rulesFor(quint16 port, int group)
{
    return {QStringLiteral("localhost:%1:127.0.0.1").arg(port),
            QStringLiteral("session-domain-%1.invalid:80:127.0.0.1").arg(group)};
}

} // namespace

void TestQCNetworkShareHandle::tlsSessionCachesMatchNative_data()
{
    QTest::addColumn<int>("flags");
    QTest::newRow("no-share") << 0;
    QTest::newRow("cookie-only") << 1;
    QTest::newRow("ssl-only") << 2;
    QTest::newRow("cookie-and-ssl") << 3;
}

void TestQCNetworkShareHandle::tlsSessionCachesMatchNative()
{
    QFETCH(int, flags);
    QTcpServer portProbe;
    QVERIFY(portProbe.listen(QHostAddress::LocalHost, 0));
    const auto port = portProbe.serverPort();
    portProbe.close();
    const auto cert = TestSourcePaths::sourcePath(
        QStringLiteral("tests/qcurl/testdata/http2/localhost.crt"));
    QProcess server;
    server.setProcessChannelMode(QProcess::MergedChannels);
    server.start(QStringLiteral("python3"),
                 {TestSourcePaths::sourcePath(
                      QStringLiteral("tests/libcurl_consistency/http_observe_server.py")),
                  QStringLiteral("--port"),
                  QString::number(port),
                  QStringLiteral("--log-file"),
                  m_tempDir.filePath(QStringLiteral("tls-%1.jsonl").arg(flags)),
                  QStringLiteral("--tls-cert"),
                  cert,
                  QStringLiteral("--tls-key"),
                  TestSourcePaths::sourcePath(
                      QStringLiteral("tests/qcurl/testdata/http2/localhost.key")),
                  QStringLiteral("--tls-min"),
                  QStringLiteral("tls1.2"),
                  QStringLiteral("--tls-max"),
                  QStringLiteral("tls1.2")});
    const auto stop = qScopeGuard([&server]() {
        server.terminate();
        if (!server.waitForFinished(2000)) {
            server.kill();
            server.waitForFinished(2000);
        }
    });
    QVERIFY(server.waitForStarted(2000));
    QVERIFY2(waitForPortReady(port, 3000), server.readAll().constData());
    const QUrl url(QStringLiteral("https://localhost:%1/tls_session").arg(port));
    NativeMultiProbe baseline(flags & 1, flags & 2);
    NativeProbeOptions options;
    options.caFile          = cert.toUtf8();
    options.resolve         = rulesFor(port, 0);
    options.freshConnection = true;
    const auto first        = baseline.get(url, options);
    QCOMPARE(first.code, CURLE_OK);
    QCOMPARE(first.body, QByteArrayLiteral("fresh"));
    const auto warm = baseline.get(url, options);
    QCOMPARE(warm.code, CURLE_OK);
    QCOMPARE(warm.newConnections, 1);
    QCOMPARE(warm.body, QByteArrayLiteral("resumed"));
    QVERIFY(baseline.replaceMulti());
    options.resolve   = rulesFor(port, 1);
    const auto across = baseline.get(url, options);
    QCOMPARE(across.code, CURLE_OK);
    QCOMPARE(across.newConnections, 1);
    QCOMPARE(across.body, flags & 2 ? QByteArrayLiteral("resumed") : QByteArrayLiteral("fresh"));
    QCNetworkAccessManager manager, other;
    QCNetworkAccessManager::ShareHandleConfig share;
    share.setShareCookies(flags & 1);
    share.setShareSslSession(flags & 2);
    manager.setShareHandleConfig(share);
    other.setShareHandleConfig(share);
    QCNetworkRequest request(url);
    QCNetworkSslConfig ssl;
    ssl.setCaCertPath(cert);
    request.setSslConfig(ssl);
    request.setHttpVersion(QCNetworkHttpVersion::Http1_1);
    QCOMPARE(request.setResolveOverride(rulesFor(port, 0)), QCNetworkConfigUpdateResult::Applied);
    QCOMPARE(bodyFor(&manager, request).value(), first.body);
    QCOMPARE(bodyFor(&manager, request).value(), warm.body);
    QCOMPARE(request.setResolveOverride(rulesFor(port, 1)), QCNetworkConfigUpdateResult::Applied);
    QCOMPARE(bodyFor(&manager, request).value(), across.body);
    QCOMPARE(request.setResolveOverride(rulesFor(port, 2)), QCNetworkConfigUpdateResult::Applied);
    QCOMPARE(bodyFor(&other, request).value(), QByteArrayLiteral("fresh"));
    qInfo().noquote() << "TLS session 配对 DSO:" << curl_version()
                      << "; 服务端 TLS1.2/OpenSSL，Connection: close；share flags=" << flags;
}
