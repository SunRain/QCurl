#include "QCNetworkProxyConfig.h"
#include "QCNetworkSslConfig.h"
#include "QCNetworkTimeoutConfig.h"
#include "qcnetwork_retry_policy_test_helper.h"
#include "qcurl_network_path_probe.h"
#include "qcurl_resolve_test_server.h"
#include "tst_QCNetworkNetworkPath.h"

#include <QScopeGuard>
#include <QtTest>

using namespace QCurl;
using namespace std::chrono_literals;

namespace {

void addPolicies()
{
    QTest::addColumn<QCNetworkResolveIsolation>("isolation");
    QTest::newRow("isolated") << QCNetworkResolveIsolation::Isolated;
    QTest::newRow("native-shared") << QCNetworkResolveIsolation::NativeShared;
}

QCNetworkRequest requestFor(quint16 port, QCNetworkResolveIsolation isolation)
{
    QCNetworkRequest request(QUrl(QStringLiteral("http://origin.invalid:18080/lifecycle")));
    request.setConnectTo({QStringLiteral("origin.invalid:18080:localhost:%1").arg(port)});
    const auto update = request.setResolveOverride(
        {QStringLiteral("localhost:%1:127.0.0.1").arg(port)});
    const auto strategy = request.setResolveIsolation(isolation);
    Q_ASSERT(update == QCNetworkConfigUpdateResult::Applied);
    Q_ASSERT(strategy == QCNetworkConfigUpdateResult::Applied);
    QCNetworkTimeoutConfig timeout;
    timeout.setTotalTimeout(2500ms);
    request.setTimeoutConfig(timeout);
    return request;
}

} // namespace

void TestQCNetworkNetworkPath::combinedTlsIdentityMatchesNative_data()
{
    addPolicies();
}

void TestQCNetworkNetworkPath::combinedTlsIdentityMatchesNative()
{
    QFETCH(QCNetworkResolveIsolation, isolation);
    ResolveTestServer server(QByteArrayLiteral("tls-combined"), true);
    QVERIFY(server.start(QStringLiteral("127.0.0.1")));
    server.setKeepAlive(false);
    QCNetworkAccessManager manager;
    for (const auto &host : {QStringLiteral("localhost"), QStringLiteral("wrong.invalid")}) {
        QCNetworkRequest request(QUrl(QStringLiteral("https://%1:18080/identity").arg(host)));
        request.setConnectTo(
            {QStringLiteral("%1:18080:target.invalid:%2").arg(host).arg(server.serverPort())});
        QCOMPARE(request.setResolveOverride(
                     {QStringLiteral("target.invalid:%1:127.0.0.1").arg(server.serverPort())}),
                 QCNetworkConfigUpdateResult::Applied);
        QCOMPARE(request.setResolveIsolation(isolation), QCNetworkConfigUpdateResult::Applied);
        QCNetworkSslConfig ssl;
        ssl.setCaCertPath(
            QStringLiteral(QCURL_TEST_SOURCE_ROOT "/tests/qcurl/testdata/http2/localhost.crt"));
        QVERIFY(ssl.verifyPeer() && ssl.verifyHost());
        request.setSslConfig(ssl);
        NativeProbeOptions options;
        options.resolve     = request.resolveOverride().value();
        options.connectTo   = request.connectTo().value();
        options.caFile      = ssl.caCertPath().toUtf8();
        const auto baseline = NetworkPathProbe::nativeEasy(request.url(), options);
        const auto expected = host == QStringLiteral("localhost") ? CURLE_OK
                                                                  : CURLE_PEER_FAILED_VERIFICATION;
        QCOMPARE(baseline.code, expected);
        const auto asyncResult    = NetworkPathProbe::asynchronous(&manager, request);
        const auto blockingResult = NetworkPathProbe::blocking(request);
        QCOMPARE(asyncResult.code, baseline.code);
        QCOMPARE(blockingResult.code, baseline.code);
        QCOMPARE(asyncResult.body, baseline.body);
        QCOMPARE(blockingResult.body, baseline.body);
    }
    QCOMPARE(server.requests().size(), 3);
    for (const auto &wire : server.requests()) {
        QVERIFY(wire.contains("Host: localhost:18080\r\n"));
    }
}

void TestQCNetworkNetworkPath::combinedWarmScopeMatchesNative_data()
{
    addPolicies();
}

void TestQCNetworkNetworkPath::combinedWarmScopeMatchesNative()
{
    QFETCH(QCNetworkResolveIsolation, isolation);
    ResolveTestServer first(QByteArrayLiteral("first")), second(QByteArrayLiteral("second"));
    QVERIFY(first.start(QStringLiteral("127.0.0.1")));
    QVERIFY(second.start(QStringLiteral("127.0.0.2"), first.serverPort()));
    QCNetworkAccessManager manager;
    NativeMultiProbe baseline;
    auto request = requestFor(first.serverPort(), isolation);
    request.setConnectTo(
        {QStringLiteral("origin.invalid:18080:combined.invalid:%1").arg(first.serverPort())});
    for (int step = 0; step < 3; ++step) {
        const auto rules = step == 2 ? QStringList{}
                                     : QStringList{QStringLiteral("combined.invalid:%1:127.0.0.%2")
                                                       .arg(first.serverPort())
                                                       .arg(step + 1)};
        QCOMPARE(request.setResolveOverride(rules), QCNetworkConfigUpdateResult::Applied);
        if (step > 0 && isolation == QCNetworkResolveIsolation::Isolated) {
            QVERIFY(baseline.replaceMulti());
        }
        NativeProbeOptions options;
        options.resolve   = rules;
        options.connectTo = request.connectTo().value();
        const auto native = baseline.get(request.url(), options);
        const auto actual = NetworkPathProbe::asynchronous(&manager, request);
        QCOMPARE(actual.code, native.code);
        QCOMPARE(actual.status, native.status);
        QCOMPARE(actual.body, native.body);
        if (isolation == QCNetworkResolveIsolation::NativeShared || step == 0) {
            QCOMPARE(native.body, QByteArrayLiteral("first"));
        } else if (step == 1) {
            QCOMPARE(native.body, QByteArrayLiteral("second"));
        } else {
            QCOMPARE(native.code, CURLE_COULDNT_RESOLVE_HOST);
        }
    }
}

void TestQCNetworkNetworkPath::proxyFormsMatchNative_data()
{
    QTest::addColumn<QCNetworkResolveIsolation>("isolation");
    QTest::addColumn<bool>("connectTo");
    QTest::addColumn<bool>("acceptTunnel");
    for (const auto isolation :
         {QCNetworkResolveIsolation::Isolated, QCNetworkResolveIsolation::NativeShared}) {
        for (const bool connectTo : {false, true}) {
            for (const bool acceptTunnel : {false, true}) {
                const auto name = QStringLiteral("%1-map%2-tunnel%3")
                                      .arg(int(isolation))
                                      .arg(connectTo)
                                      .arg(acceptTunnel);
                QTest::newRow(qPrintable(name)) << isolation << connectTo << acceptTunnel;
            }
        }
    }
}

void TestQCNetworkNetworkPath::proxyFormsMatchNative()
{
    QFETCH(QCNetworkResolveIsolation, isolation);
    QFETCH(bool, connectTo);
    QFETCH(bool, acceptTunnel);
    ResolveTestServer proxy(QByteArrayLiteral("remote-proxy-answer"));
    QVERIFY(proxy.start(QStringLiteral("127.0.0.1")));
    proxy.setKeepAlive(false);
    proxy.setAcceptTunnels(acceptTunnel);
    auto request = requestFor(18081, isolation);
    QCOMPARE(request.setResolveOverride({QStringLiteral("origin.invalid:18080:127.0.0.2"),
                                         QStringLiteral("remote-target.invalid:18081:127.0.0.2")}),
             QCNetworkConfigUpdateResult::Applied);
    request.setConnectTo(
        connectTo ? QStringList{QStringLiteral("origin.invalid:18080:remote-target.invalid:18081")}
                  : QStringList{});
    QCNetworkProxyConfig config;
    config.setType(QCNetworkProxyConfig::ProxyType::Http);
    config.setHostName(QStringLiteral("127.0.0.1"));
    config.setPort(proxy.serverPort());
    request.setProxyConfig(config);
    NativeProbeOptions options;
    options.proxy = QByteArrayLiteral("http://127.0.0.1:") + QByteArray::number(proxy.serverPort());
    options.resolve     = request.resolveOverride().value();
    options.connectTo   = request.connectTo().value_or(QStringList{});
    const auto baseline = NetworkPathProbe::nativeEasy(request.url(), options);
    QCOMPARE(baseline.code, connectTo && !acceptTunnel ? CURLE_RECV_ERROR : CURLE_OK);
    const auto firstWire = proxy.requests();
    QCNetworkAccessManager manager;
    const auto asyncResult = NetworkPathProbe::asynchronous(&manager, request);
    const auto blocking    = NetworkPathProbe::blocking(request);
    QCOMPARE(asyncResult.code, baseline.code);
    QCOMPARE(blocking.code, baseline.code);
    QCOMPARE(asyncResult.status, baseline.status);
    QCOMPARE(blocking.status, baseline.status);
    QCOMPARE(asyncResult.body, baseline.body);
    QCOMPARE(blocking.body, baseline.body);
    QCOMPARE(proxy.requests().size(), firstWire.size() * 3);
    for (int index = 0; index < firstWire.size(); ++index) {
        const auto line = firstWire[index].split('\n').first();
        QCOMPARE(proxy.requests()[index + firstWire.size()].split('\n').first(), line);
        QCOMPARE(proxy.requests()[index + firstWire.size() * 2].split('\n').first(), line);
    }
    QVERIFY(firstWire.first().startsWith(connectTo ? "CONNECT remote-target.invalid:18081 "
                                                   : "GET http://origin.invalid:18080/lifecycle "));
}

void TestQCNetworkNetworkPath::requiredPathOptionFailureDoesNotSend_data()
{
    QTest::addColumn<QByteArray>("variable");
    QTest::addColumn<QByteArray>("option");
    for (const auto &option : {QByteArray("CURLOPT_RESOLVE"),
                               QByteArray("CURLOPT_CONNECT_TO"),
                               QByteArray("CURLOPT_IPRESOLVE")}) {
        QTest::newRow(option.constData()) << QByteArray("QCURL_TEST_FORCE_SETOPT_ERROR") << option;
    }
    QTest::newRow("partial-resolve-then-connect-list")
        << QByteArray("QCURL_TEST_FORCE_SLIST_APPEND_ERROR") << QByteArray("CURLOPT_CONNECT_TO");
}

void TestQCNetworkNetworkPath::requiredPathOptionFailureDoesNotSend()
{
    QFETCH(QByteArray, variable);
    QFETCH(QByteArray, option);
    ResolveTestServer server(QByteArrayLiteral("control"));
    QVERIFY(server.start(QStringLiteral("127.0.0.1")));
    auto request = requestFor(server.serverPort(), QCNetworkResolveIsolation::Isolated);
    request.setIpResolve(QCNetworkIpResolve::Ipv4);
    QCNetworkAccessManager manager;
    QCOMPARE(NetworkPathProbe::asynchronous(&manager, request).body, QByteArrayLiteral("control"));
    QCOMPARE(NetworkPathProbe::blocking(request).body, QByteArrayLiteral("control"));
    const auto previous = qgetenv(variable.constData());
    const auto restore  = qScopeGuard([variable, previous]() {
        if (previous.isNull()) {
            qunsetenv(variable.constData());
        } else {
            qputenv(variable.constData(), previous);
        }
    });
    qputenv(variable.constData(), option);
    auto *rejected = manager.get(request);
    QTRY_VERIFY_WITH_TIMEOUT(rejected->isFinished(), 2000);
    QCOMPARE(rejected->error(), NetworkError::InvalidRequest);
    QVERIFY2(rejected->errorString().contains(QString::fromLatin1(option)),
             qPrintable(rejected->errorString()));
    auto future = std::async(std::launch::async,
                             [request]() { return QCBlockingNetworkClient().get(request); });
    QVERIFY(NetworkPathProbe::waitWithEvents(future));
    const auto blocking = future.get();
    QCOMPARE(blocking.error(), NetworkError::InvalidRequest);
    QVERIFY2(blocking.errorMessage().contains(QString::fromLatin1(option)),
             qPrintable(blocking.errorMessage()));
    QCOMPARE(server.requests().size(), 2);
    rejected->deleteLater();
}

void TestQCNetworkNetworkPath::retryKeepsOriginalSnapshot_data()
{
    addPolicies();
}

void TestQCNetworkNetworkPath::retryKeepsOriginalSnapshot()
{
    QFETCH(QCNetworkResolveIsolation, isolation);
    ResolveTestServer server(QByteArrayLiteral("retry-body"));
    QVERIFY(server.start(QStringLiteral("127.0.0.1")));
    server.setResponseStatus(503);
    QCNetworkAccessManager manager;
    auto request = requestFor(server.serverPort(), isolation);
    request.setRetryPolicy(TestSupport::makeRetryPolicyOrFail(1, 10ms));
    auto *reply = manager.get(request);
    connect(reply, &QCNetworkReply::retryAttempt, &server, [&server](int, NetworkError) {
        server.setResponseStatus(200);
    });
    QCOMPARE(request.setResolveOverride(
                 {QStringLiteral("localhost:%1:127.0.0.2").arg(server.serverPort())}),
             QCNetworkConfigUpdateResult::Applied);
    request.setConnectTo({QStringLiteral("::127.0.0.2:1")});
    QCOMPARE(request.setResolveIsolation(isolation == QCNetworkResolveIsolation::Isolated
                                             ? QCNetworkResolveIsolation::NativeShared
                                             : QCNetworkResolveIsolation::Isolated),
             QCNetworkConfigUpdateResult::Applied);
    QTRY_VERIFY_WITH_TIMEOUT(reply->isFinished(), 3000);
    QCOMPARE(reply->error(), NetworkError::NoError);
    QCOMPARE(reply->retryAttemptCount(), 1);
    QCOMPARE(reply->readAll().value(), QByteArrayLiteral("retry-body"));
    QCOMPARE(server.requests().size(), 2);
    QCOMPARE(server.connections(), 1);
    reply->deleteLater();
}

void TestQCNetworkNetworkPath::redirectMatchesEachHop_data()
{
    addPolicies();
}

void TestQCNetworkNetworkPath::redirectMatchesEachHop()
{
    QFETCH(QCNetworkResolveIsolation, isolation);
    ResolveTestServer start(QByteArrayLiteral("redirect")), target(QByteArrayLiteral("target"));
    QVERIFY(start.start(QStringLiteral("127.0.0.1")));
    QVERIFY(target.start(QStringLiteral("127.0.0.2")));
    start.setResponseStatus(302);
    start.setResponseHeaders(
        QByteArrayLiteral("Location: http://next.invalid:18081/redirected\r\n"));
    auto request = requestFor(start.serverPort(), isolation);
    auto resolve = request.resolveOverride().value();
    resolve.append(QStringLiteral("mapped.invalid:%1:127.0.0.2").arg(target.serverPort()));
    QCOMPARE(request.setResolveOverride(resolve), QCNetworkConfigUpdateResult::Applied);
    auto mappings = request.connectTo().value();
    mappings.append(QStringLiteral("next.invalid:18081:mapped.invalid:%1").arg(target.serverPort()));
    request.setConnectTo(mappings);
    request.setFollowLocation(true);
    NativeProbeOptions options;
    options.resolve     = resolve;
    options.connectTo   = mappings;
    const auto baseline = NetworkPathProbe::nativeEasy(request.url(), options);
    QCOMPARE(baseline.code, CURLE_OK);
    QCOMPARE(baseline.body, QByteArrayLiteral("target"));
    QCNetworkAccessManager manager;
    QCOMPARE(NetworkPathProbe::asynchronous(&manager, request).body, baseline.body);
    QCOMPARE(NetworkPathProbe::blocking(request).body, baseline.body);
    QCOMPARE(start.requests().size(), 3);
    QCOMPARE(target.requests().size(), 3);
    for (const auto &wire : target.requests()) {
        QVERIFY(wire.startsWith("GET /redirected HTTP/1.1\r\n"));
        QVERIFY(wire.contains("Host: next.invalid:18081\r\n"));
    }
}
