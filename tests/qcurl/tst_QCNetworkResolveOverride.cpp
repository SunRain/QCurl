#include "tst_QCNetworkResolveOverride.h"

#include "QCNetworkAccessManager.h"
#include "QCNetworkReply.h"
#include "QCNetworkRequest.h"
#include "QCNetworkSslConfig.h"
#include "qcurl_resolve_test_server.h"

#include <QEventLoop>
#include <QSignalSpy>
#include <QTimer>
#include <QtTest>

using namespace QCurl;

void TestQCNetworkResolveOverride::updatesAreAtomic_data()
{
    QTest::addColumn<QString>("invalid");
    for (const auto &entry :
         QStringList{QString(),
                     QStringLiteral(" "),
                     QStringLiteral("host:0:127.0.0.1"),
                     QStringLiteral("host:65536:127.0.0.1"),
                     QStringLiteral("host:+80:127.0.0.1"),
                     QStringLiteral("host:80:256.0.0.1"),
                     QStringLiteral("host:80:127.0.0.01"),
                     QStringLiteral("host:80:127.0.0.+1"),
                     QStringLiteral("https://host:80:127.0.0.1"),
                     QStringLiteral("user@host:80:127.0.0.1"),
                     QStringLiteral("*:80:127.0.0.1"),
                     QStringLiteral("+host:80:127.0.0.1"),
                     QStringLiteral("-host:80:127.0.0.1"),
                     QStringLiteral("host:80:[::1]"),
                     QStringLiteral("host:80:127.0.0.1,127.0.0.2"),
                     QStringLiteral("ho st:80:127.0.0.1"),
                     QStringLiteral("ho\nst:80:127.0.0.1"),
                     QStringLiteral("a..invalid:80:127.0.0.1"),
                     QStringLiteral("xn--:80:127.0.0.1"),
                     QString(64, QLatin1Char('a')) + QStringLiteral(":80:127.0.0.1"),
                     QStringLiteral("ho\u0000st:80:127.0.0.1")}) {
        QTest::newRow(qPrintable(entry)) << entry;
    }
}

void TestQCNetworkResolveOverride::updatesAreAtomic()
{
    QFETCH(QString, invalid);
    QCNetworkRequest request;
    using Result       = QCNetworkConfigUpdateResult;
    const auto initial = QStringList{QStringLiteral("xn--bcher-kva.invalid:80:127.0.0.1")};
    QCOMPARE(request.setResolveOverride({QStringLiteral(" bücher.invalid : 80 : 127.0.0.1 ")}),
             Result::Applied);
    QCOMPARE(request.resolveOverride().value(), initial);
    QCOMPARE(request.setResolveOverride({QStringLiteral("other:80:127.0.0.2"), invalid}),
             Result::InvalidArgument);
    QCOMPARE(request.resolveOverride().value(), initial);
    auto copy = request;
    QCOMPARE(copy.setResolveOverride({}), Result::Applied);
    QVERIFY(!copy.resolveOverride());
    QCOMPARE(request.resolveOverride().value(), initial);
}

void TestQCNetworkResolveOverride::encodedHostsReachEndpoint_data()
{
    QTest::addColumn<QString>("host");
    for (const auto &host : {QStringLiteral("internal_name"),
                             QStringLiteral("bücher.invalid"),
                             QStringLiteral("bu\u0308cher.invalid"),
                             QStringLiteral("BÜCHER.INVALID"),
                             QStringLiteral("xn--bcher-kva.invalid"),
                             QStringLiteral("XN--BCHER-KVA.INVALID")}) {
        QTest::newRow(qPrintable(host)) << host;
    }
}

void TestQCNetworkResolveOverride::encodedHostsReachEndpoint()
{
    QFETCH(QString, host);
    ResolveTestServer server(QByteArrayLiteral("encoded-host"));
    QVERIFY2(server.start(QStringLiteral("127.0.0.1")), qPrintable(server.errorString()));
    QUrl url(QStringLiteral("http://placeholder.invalid/"));
    url.setHost(host);
    url.setPort(server.serverPort());
    const QString aceHost = QString::fromLatin1(QUrl::toAce(url.host()));
    QCNetworkRequest request(url);
    const QString normalized = QStringLiteral("%1:%2:127.0.0.1").arg(aceHost).arg(url.port());
    QCOMPARE(request.setResolveOverride(
                 {QStringLiteral("%1:%2:127.0.0.1").arg(host).arg(url.port())}),
             QCNetworkConfigUpdateResult::Applied);
    QCOMPARE(request.resolveOverride().value(), QStringList{normalized});
    QCNetworkAccessManager manager;
    auto *reply = manager.get(request);
    QSignalSpy done(reply, &QCNetworkReply::finished);
    QVERIFY(done.wait(3000));
    QCOMPARE(reply->error(), NetworkError::NoError);
    QCOMPARE(reply->httpStatusCode(), 200);
    QCOMPARE(reply->url(), url);
    QCOMPARE(reply->readAll().value(), QByteArrayLiteral("encoded-host"));
    QCOMPARE(server.requests().size(), 1);
    QVERIFY(server.requests().first().contains(QByteArrayLiteral("Host: ") + aceHost.toLatin1()
                                               + ':' + QByteArray::number(url.port())));
    reply->deleteLater();
    QCOMPARE(request.setResolveOverride({}), QCNetworkConfigUpdateResult::Applied);
    QVERIFY(!request.resolveOverride());
    if (host != QStringLiteral("internal_name")) {
        auto *uncovered = manager.get(request);
        QSignalSpy uncoveredDone(uncovered, &QCNetworkReply::finished);
        QVERIFY(uncoveredDone.wait(3000));
        QVERIFY(uncovered->error() != NetworkError::NoError);
        QCOMPARE(server.requests().size(), 1);
        uncovered->deleteLater();
    }
}

void TestQCNetworkResolveOverride::requestsAreIsolated_data()
{
    QTest::addColumn<int>("shareFlags");
    QTest::newRow("multi-cache") << 0;
    QTest::newRow("shared-cookie") << 1;
    QTest::newRow("shared-ssl-session") << 2;
    QTest::newRow("shared-cookie-and-ssl") << 3;
}

void TestQCNetworkResolveOverride::requestsAreIsolated()
{
    QFETCH(int, shareFlags);
    ResolveTestServer normal(QByteArrayLiteral("normal")),
        alternate(QByteArrayLiteral("alternate"));
    QVERIFY2(normal.start(QStringLiteral("127.0.0.1")), qPrintable(normal.errorString()));
    QVERIFY2(alternate.start(QStringLiteral("127.0.0.2"), normal.serverPort()),
             qPrintable(alternate.errorString()));
    QCNetworkAccessManager manager;
    QCNetworkAccessManager::ShareHandleConfig sharing;
    sharing.setShareCookies(shareFlags & 1);
    sharing.setShareSslSession(shareFlags & 2);
    manager.setShareHandleConfig(sharing);
    QCNetworkRequest request(QUrl(QStringLiteral("http://localhost:%1/").arg(normal.serverPort())));
    QCOMPARE(request.setResolveOverride(
                 {QStringLiteral("localhost:%1:127.0.0.2").arg(normal.serverPort())}),
             QCNetworkConfigUpdateResult::Applied);
    auto *first = manager.get(request);
    QSignalSpy done(first, &QCNetworkReply::finished);
    QVERIFY(done.wait(3000));
    QCOMPARE(first->readAll().value(), QByteArrayLiteral("alternate"));
    first->deleteLater();
    QCOMPARE(request.setResolveOverride({}), QCNetworkConfigUpdateResult::Applied);
    auto *second = manager.get(request);
    QSignalSpy secondDone(second, &QCNetworkReply::finished);
    QVERIFY(secondDone.wait(3000));
    QCOMPARE(second->readAll().value(), QByteArrayLiteral("normal"));
    second->deleteLater();
    QCNetworkRequest covered = request;
    QCOMPARE(covered.setResolveOverride(
                 {QStringLiteral("localhost:%1:127.0.0.2").arg(normal.serverPort())}),
             QCNetworkConfigUpdateResult::Applied);
    auto *third  = manager.get(covered);
    auto *fourth = manager.get(request);
    QTRY_VERIFY_WITH_TIMEOUT(third->isFinished() && fourth->isFinished(), 3000);
    QCOMPARE(third->readAll().value(), QByteArrayLiteral("alternate"));
    QCOMPARE(fourth->readAll().value(), QByteArrayLiteral("normal"));
    QCOMPARE(normal.requests().size(), 2);
    QCOMPARE(alternate.requests().size(), 2);
    QCOMPARE(normal.connections(), 1);
    QCOMPARE(alternate.connections(), 1);
    const auto host = QByteArrayLiteral("Host: localhost:")
                      + QByteArray::number(normal.serverPort());
    QVERIFY(alternate.requests().first().contains(host));
    third->deleteLater();
    fourth->deleteLater();
}

void TestQCNetworkResolveOverride::tlsVerifiesOriginalHost_data()
{
    QTest::addColumn<QString>("host");
    QTest::addColumn<QString>("certificate");
    QTest::addColumn<QCNetworkResolveIsolation>("isolation");
    for (const auto isolation :
         {QCNetworkResolveIsolation::Isolated, QCNetworkResolveIsolation::NativeShared}) {
        const auto suffix = isolation == QCNetworkResolveIsolation::Isolated ? "isolated"
                                                                             : "native";
        QTest::newRow(qPrintable(QStringLiteral("ascii-%1").arg(suffix)))
            << QStringLiteral("localhost") << QStringLiteral("localhost.crt") << isolation;
        QTest::newRow(qPrintable(QStringLiteral("unicode-%1").arg(suffix)))
            << QStringLiteral("bücher.invalid") << QStringLiteral("unicode-host.crt") << isolation;
        QTest::newRow(qPrintable(QStringLiteral("ace-%1").arg(suffix)))
            << QStringLiteral("XN--BCHER-KVA.INVALID") << QStringLiteral("unicode-host.crt")
            << isolation;
    }
}

void TestQCNetworkResolveOverride::tlsVerifiesOriginalHost()
{
    QFETCH(QString, host);
    QFETCH(QString, certificate);
    QFETCH(QCNetworkResolveIsolation, isolation);
    const QString aceHost = QString::fromLatin1(QUrl::toAce(host));
    ResolveTestServer server(QByteArrayLiteral("tls-ok"), true, certificate);
    QVERIFY2(server.start(QStringLiteral("127.0.0.2")), qPrintable(server.errorString()));
    QCNetworkAccessManager manager;
    auto request = QCNetworkRequest(
        QUrl(QStringLiteral("https://%1:%2/").arg(host).arg(server.serverPort())));
    QCNetworkSslConfig tls;
    tls.setCaCertPath(QStringLiteral(QCURL_TEST_SOURCE_ROOT "/tests/qcurl/testdata/http2/")
                      + certificate);
    QVERIFY(tls.verifyPeer() && tls.verifyHost());
    request.setSslConfig(tls);
    QCOMPARE(request.setResolveIsolation(isolation), QCNetworkConfigUpdateResult::Applied);
    QCOMPARE(request.setResolveOverride(
                 {QStringLiteral("%1:%2:127.0.0.2").arg(host).arg(server.serverPort())}),
             QCNetworkConfigUpdateResult::Applied);
    auto *reply = manager.get(request);
    QSignalSpy done(reply, &QCNetworkReply::finished);
    QVERIFY(done.wait(3000));
    QCOMPARE(reply->error(), NetworkError::NoError);
    QCOMPARE(reply->url(), request.url());
    QCOMPARE(reply->readAll().value(), QByteArrayLiteral("tls-ok"));
    QVERIFY(
        server.requests().first().contains(QByteArrayLiteral("Host: ") + aceHost.toLatin1() + ':'));
    reply->deleteLater();
    request.setUrl(QUrl(QStringLiteral("https://wrong-host.invalid:%1/").arg(server.serverPort())));
    QCOMPARE(request.setResolveOverride(
                 {QStringLiteral("wrong-host.invalid:%1:127.0.0.2").arg(server.serverPort())}),
             QCNetworkConfigUpdateResult::Applied);
    auto *bad = manager.get(request);
    QSignalSpy badDone(bad, &QCNetworkReply::finished);
    QVERIFY(badDone.wait(3000));
    QVERIFY(bad->error() != NetworkError::NoError);
    QCOMPARE(server.requests().size(), 1);
    bad->deleteLater();
}

QTEST_GUILESS_MAIN(TestQCNetworkResolveOverride)
