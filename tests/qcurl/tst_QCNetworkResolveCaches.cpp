#include "QCNetworkAccessManager.h"
#include "QCNetworkReply.h"
#include "QCNetworkRequest.h"
#include "qcurl_native_multi_probe.h"
#include "qcurl_resolve_test_server.h"
#include "tst_QCNetworkResolveOverride.h"

#include <QSignalSpy>
#include <QtTest>

using namespace QCurl;

namespace {

QCNetworkAccessManager::ShareHandleConfig sharing(int flags)
{
    QCNetworkAccessManager::ShareHandleConfig config;
    config.setShareCookies(flags & 1);
    config.setShareSslSession(flags & 2);
    return config;
}

QCNetworkRequest makeRequest(quint16 port, const QStringList &rules)
{
    QCNetworkRequest request(QUrl(QStringLiteral("http://localhost:%1/cache").arg(port)));
    request.setIpResolve(QCNetworkIpResolve::Ipv4);
    const auto result = request.setResolveOverride(rules);
    Q_ASSERT(result == QCNetworkConfigUpdateResult::Applied);
    return request;
}

std::optional<QByteArray> getBody(QCNetworkAccessManager *manager, const QCNetworkRequest &request)
{
    auto *reply = manager->get(request);
    QSignalSpy done(reply, &QCNetworkReply::finished);
    if ((!reply->isFinished() && !done.wait(3000)) || reply->error() != NetworkError::NoError
        || reply->httpStatusCode() != 200) {
        qWarning() << reply->errorString() << reply->httpStatusCode();
        return std::nullopt;
    }
    const auto body = reply->readAll();
    reply->deleteLater();
    return body;
}

void addShareRows()
{
    QTest::addColumn<int>("flags");
    QTest::newRow("off") << 0;
    QTest::newRow("cookies") << 1;
    QTest::newRow("ssl") << 2;
    QTest::newRow("cookies-and-ssl") << 3;
}

} // namespace

void TestQCNetworkResolveOverride::effectiveRulesReuseOnlyCompatibleDomains_data()
{
    addShareRows();
}

void TestQCNetworkResolveOverride::effectiveRulesReuseOnlyCompatibleDomains()
{
    QFETCH(int, flags);
    ResolveTestServer server(QByteArrayLiteral("same-target"));
    QVERIFY(server.start(QStringLiteral("127.0.0.1")));
    const auto key   = QStringLiteral("localhost:%1:").arg(server.serverPort());
    const auto extra = QStringLiteral("unused.invalid:80:127.0.0.1");
    const QStringList initial{key + QStringLiteral("127.0.0.1"), extra};
    const QStringList equivalent{extra,
                                 key + QStringLiteral("127.0.0.2"),
                                 key + QStringLiteral("127.0.0.1")};
    NativeMultiProbe baseline(flags & 1, flags & 2);
    NativeProbeOptions options;
    options.resolve  = initial;
    const auto first = baseline.get(makeRequest(server.serverPort(), initial).url(), options);
    QCOMPARE(first.code, CURLE_OK);
    QCOMPARE(first.newConnections, 1);
    options.resolve   = equivalent;
    const auto second = baseline.get(makeRequest(server.serverPort(), equivalent).url(), options);
    QCOMPARE(second.code, CURLE_OK);
    QCOMPARE(second.newConnections, 0);
    QCNetworkAccessManager manager;
    manager.setShareHandleConfig(sharing(flags));
    const auto connections = server.connections();
    QCOMPARE(getBody(&manager, makeRequest(server.serverPort(), initial)).value(), first.body);
    QCOMPARE(getBody(&manager, makeRequest(server.serverPort(), equivalent)).value(), second.body);
    QCOMPARE(server.connections() - connections, 1);
    // 当前 URL 相同不代表完整配置相同：不能漏掉未命中该 URL 的条目。
    auto incompatible = initial;
    incompatible.append(QStringLiteral("different.invalid:80:127.0.0.1"));
    QCOMPARE(getBody(&manager, makeRequest(server.serverPort(), incompatible)).value(), first.body);
    QCOMPARE(server.connections() - connections, 2);
    QCOMPARE(getBody(&manager, makeRequest(server.serverPort(), equivalent)).value(), first.body);
    QCOMPARE(server.connections() - connections, 2);
}

void TestQCNetworkResolveOverride::nativeSharedPreservesWarmState_data()
{
    QTest::addColumn<int>("flags");
    QTest::addColumn<bool>("closeConnections");
    for (int flags = 0; flags < 4; ++flags) {
        QTest::newRow(qPrintable(QStringLiteral("connection-%1").arg(flags))) << flags << false;
        QTest::newRow(qPrintable(QStringLiteral("dns-%1").arg(flags))) << flags << true;
    }
}

void TestQCNetworkResolveOverride::nativeSharedPreservesWarmState()
{
    QFETCH(int, flags);
    QFETCH(bool, closeConnections);
    ResolveTestServer normal(QByteArrayLiteral("A")), alternate(QByteArrayLiteral("B"));
    QVERIFY(normal.start(QStringLiteral("127.0.0.1")));
    QVERIFY(alternate.start(QStringLiteral("127.0.0.2"), normal.serverPort()));
    normal.setKeepAlive(!closeConnections);
    alternate.setKeepAlive(!closeConnections);
    const auto key = QStringLiteral("localhost:%1:").arg(normal.serverPort());
    auto request   = makeRequest(normal.serverPort(), {key + QStringLiteral("127.0.0.2")});
    QCOMPARE(request.setResolveIsolation(QCNetworkResolveIsolation::NativeShared),
             QCNetworkConfigUpdateResult::Applied);
    NativeMultiProbe baseline(flags & 1, flags & 2);
    NativeProbeOptions options;
    options.resolve   = request.resolveOverride().value();
    options.ipResolve = CURL_IPRESOLVE_V4;
    const auto first  = baseline.get(request.url(), options);
    QCOMPARE(first.code, CURLE_OK);
    QCOMPARE(first.body, QByteArrayLiteral("B"));
    options.resolve.clear();
    const auto cleared = baseline.get(request.url(), options);
    QCOMPARE(cleared.code, CURLE_OK);
    QCOMPARE(cleared.body, first.body);
    QCOMPARE(cleared.newConnections, closeConnections ? 1 : 0);
    options.resolve    = {key + QStringLiteral("127.0.0.1")};
    const auto changed = baseline.get(request.url(), options);
    QCOMPARE(changed.code, CURLE_OK);
    QCNetworkAccessManager one, other;
    one.setShareHandleConfig(sharing(flags));
    other.setShareHandleConfig(sharing(flags));
    QCOMPARE(getBody(&one, request).value(), first.body);
    QCOMPARE(request.setResolveOverride({}), QCNetworkConfigUpdateResult::Applied);
    // 跨 manager 仍是同线程固定 NativeShared 域，但显式 store 不跨 manager。
    QCOMPARE(getBody(&other, request).value(), cleared.body);
    QCOMPARE(request.setResolveOverride(options.resolve), QCNetworkConfigUpdateResult::Applied);
    QCOMPARE(getBody(&one, request).value(), changed.body);
}

void TestQCNetworkResolveOverride::cookieSharingStaysPerManagerAcrossDomains_data()
{
    addShareRows();
}

void TestQCNetworkResolveOverride::cookieSharingStaysPerManagerAcrossDomains()
{
    QFETCH(int, flags);
    ResolveTestServer one(QByteArrayLiteral("one")), two(QByteArrayLiteral("two"));
    QVERIFY(one.start(QStringLiteral("127.0.0.1")));
    QVERIFY(two.start(QStringLiteral("127.0.0.2"), one.serverPort()));
    one.setResponseHeaders(QByteArrayLiteral("Set-Cookie: test_identity=one; Path=/\r\n"));
    QCNetworkAccessManager firstManager, secondManager;
    firstManager.setShareHandleConfig(sharing(flags));
    secondManager.setShareHandleConfig(sharing(flags));
    const auto key    = QStringLiteral("localhost:%1:").arg(one.serverPort());
    const auto first  = makeRequest(one.serverPort(), {key + QStringLiteral("127.0.0.1")});
    const auto second = makeRequest(one.serverPort(), {key + QStringLiteral("127.0.0.2")});
    QCOMPARE(getBody(&firstManager, first).value(), QByteArrayLiteral("one"));
    QCOMPARE(getBody(&firstManager, second).value(), QByteArrayLiteral("two"));
    QCOMPARE(two.requests().last().contains("Cookie: test_identity=one"), bool(flags & 1));
    QCOMPARE(getBody(&secondManager, second).value(), QByteArrayLiteral("two"));
    QVERIFY(!two.requests().last().contains("Cookie:"));
    QCOMPARE(getBody(&firstManager, first).value(), QByteArrayLiteral("one"));
    QCOMPARE(one.requests().last().contains("Cookie: test_identity=one"), bool(flags & 1));
}
