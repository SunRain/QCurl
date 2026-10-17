#include "QCNetworkHttpVersion.h"
#include "QCNetworkTimeoutConfig.h"
#include "qcurl_network_path_probe.h"
#include "qcurl_resolve_test_server.h"
#include "tst_QCNetworkNetworkPath.h"

#include <QtTest>

using namespace QCurl;

namespace {

struct PathScenario
{
    QStringList resolve;
    QStringList connectTo;
    QByteArray body = QByteArrayLiteral("B");
    CURLcode code   = CURLE_OK;
};

QList<PathScenario> scenarios(quint16 port)
{
    const auto origin = QStringLiteral("localhost:%1:").arg(port);
    const auto target = QStringLiteral("target.invalid:%1:").arg(port);
    const auto a      = QStringLiteral("127.0.0.1");
    const auto b      = QStringLiteral("127.0.0.2");
    const auto tail   = QStringLiteral(":%1").arg(port);
    return {{{}, {}, QByteArrayLiteral("A")},
            {{origin + b}, {}},
            {{}, {origin + b + tail}},
            {{target + b}, {origin + QStringLiteral("target.invalid") + tail}},
            {{origin + a, origin + b}, {}},
            {{origin + a}, {origin + b + tail, origin + a + tail}},
            {{origin + b}, {QStringLiteral("other.invalid:80:") + a + tail}},
            {{origin + b}, {origin + QStringLiteral("localhost") + tail}},
            {{origin + a}, {QStringLiteral(":%1:").arg(port) + b + tail}},
            {{origin + a}, {QStringLiteral("localhost::") + b + tail}},
            {{origin + a}, {QStringLiteral("::") + b + tail}},
            {{origin + b}, {origin + tail}},
            {{origin + a}, {origin + b + QLatin1Char(':')}},
            {{origin + b},
             {origin + QStringLiteral("missing-target.invalid") + tail},
             {},
             CURLE_COULDNT_RESOLVE_HOST},
            {{QStringLiteral("missing-port.invalid:%1:").arg(port == 65535 ? port - 1 : port + 1)
              + b},
             {origin + QStringLiteral("missing-port.invalid") + tail},
             {},
             CURLE_COULDNT_RESOLVE_HOST}};
}

QCNetworkRequest makeRequest(quint16 port,
                             const PathScenario &scenario,
                             QCNetworkResolveIsolation isolation,
                             bool reverse)
{
    QCNetworkRequest request(QUrl(QStringLiteral("http://localhost:%1/combined").arg(port)));
    request.setHttpVersion(QCNetworkHttpVersion::Http1_1);
    request.setIpResolve(QCNetworkIpResolve::Ipv4);
    QCNetworkTimeoutConfig timeout;
    timeout.setTotalTimeout(std::chrono::milliseconds(2500));
    request.setTimeoutConfig(timeout);
    if (reverse) {
        request.setConnectTo(scenario.connectTo);
    }
    const auto update   = request.setResolveOverride(scenario.resolve);
    const auto strategy = request.setResolveIsolation(isolation);
    Q_ASSERT(update == QCNetworkConfigUpdateResult::Applied);
    Q_ASSERT(strategy == QCNetworkConfigUpdateResult::Applied);
    if (!reverse) {
        request.setConnectTo(scenario.connectTo);
    }
    return request;
}

void compareResult(const NativeProbeResult &actual, const NativeProbeResult &expected)
{
    QCOMPARE(actual.code, expected.code);
    QCOMPARE(actual.status, expected.status);
    QCOMPARE(actual.body, expected.body);
}

} // namespace

void TestQCNetworkNetworkPath::optionCombinationsMatchNative_data()
{
    QTest::addColumn<int>("scenarioIndex");
    QTest::addColumn<QCNetworkResolveIsolation>("isolation");
    QTest::addColumn<bool>("reverse");
    for (int index = 0; index < scenarios(18080).size(); ++index) {
        for (const auto isolation :
             {QCNetworkResolveIsolation::Isolated, QCNetworkResolveIsolation::NativeShared}) {
            for (const bool reverse : {false, true}) {
                const auto name = QStringLiteral("case-%1-%2-order-%3")
                                      .arg(index)
                                      .arg(int(isolation))
                                      .arg(reverse);
                QTest::newRow(qPrintable(name)) << index << isolation << reverse;
            }
        }
    }
}

void TestQCNetworkNetworkPath::optionCombinationsMatchNative()
{
    QFETCH(int, scenarioIndex);
    QFETCH(QCNetworkResolveIsolation, isolation);
    QFETCH(bool, reverse);
    ResolveTestServer a(QByteArrayLiteral("A")), b(QByteArrayLiteral("B"));
    QVERIFY(a.start(QStringLiteral("127.0.0.1")));
    QVERIFY(b.start(QStringLiteral("127.0.0.2"), a.serverPort()));
    const auto scenario = scenarios(a.serverPort()).at(scenarioIndex);
    auto request        = makeRequest(a.serverPort(), scenario, isolation, reverse);
    NativeProbeOptions options;
    options.resolve     = scenario.resolve;
    options.connectTo   = scenario.connectTo;
    options.ipResolve   = CURL_IPRESOLVE_V4;
    const auto baseline = NetworkPathProbe::nativeEasy(request.url(), options);
    QCOMPARE(baseline.code, scenario.code);
    QCOMPARE(baseline.body, scenario.body);
    QCNetworkAccessManager manager;
    compareResult(NetworkPathProbe::asynchronous(&manager, request), baseline);
    compareResult(NetworkPathProbe::blocking(request), baseline);
    const auto expectedRequests = scenario.code == CURLE_OK ? 3 : 0;
    QCOMPARE(a.requests().size() + b.requests().size(), expectedRequests);
    for (const auto &wire : a.requests() + b.requests()) {
        QVERIFY(wire.startsWith("GET /combined HTTP/1.1\r\n"));
        QVERIFY(wire.contains(QByteArrayLiteral("Host: localhost:")
                              + QByteArray::number(a.serverPort()) + "\r\n"));
    }
    // 单独清除/替换列表，不改另一个配置或策略；新传输不得保留旧列表。
    request.setConnectTo({});
    QCOMPARE(request.resolveOverride().value_or(QStringList{}), scenario.resolve);
    QCOMPARE(request.setResolveOverride({}), QCNetworkConfigUpdateResult::Applied);
    QCOMPARE(request.resolveIsolation(), isolation);
    NativeProbeOptions cleared;
    cleared.ipResolve = CURL_IPRESOLVE_V4;
    const auto clean  = NetworkPathProbe::nativeEasy(request.url(), cleared);
    compareResult(NetworkPathProbe::blocking(request), clean);
}

void TestQCNetworkNetworkPath::addressFamiliesMatchNative_data()
{
    QTest::addColumn<QCNetworkIpResolve>("family");
    QTest::addColumn<long>("nativeFamily");
    QTest::addColumn<QByteArray>("body");
    QTest::newRow("any") << QCNetworkIpResolve::Any << long(CURL_IPRESOLVE_WHATEVER)
                         << QByteArray("v4");
    QTest::newRow("v4") << QCNetworkIpResolve::Ipv4 << long(CURL_IPRESOLVE_V4) << QByteArray("v4");
    QTest::newRow("v6") << QCNetworkIpResolve::Ipv6 << long(CURL_IPRESOLVE_V6) << QByteArray("v6");
}

void TestQCNetworkNetworkPath::addressFamiliesMatchNative()
{
    QFETCH(QCNetworkIpResolve, family);
    QFETCH(long, nativeFamily);
    QFETCH(QByteArray, body);
    ResolveTestServer v4(QByteArrayLiteral("v4")), v6(QByteArrayLiteral("v6"));
    QVERIFY(v4.start(QStringLiteral("127.0.0.1")));
    QVERIFY2(v6.start(QStringLiteral("::1"), v4.serverPort()), qPrintable(v6.errorString()));
    v4.setKeepAlive(false);
    v6.setKeepAlive(false);
    const auto input = PathScenario{{QStringLiteral("localhost:%1:127.0.0.1").arg(v4.serverPort())},
                                    {}};
    for (const auto isolation :
         {QCNetworkResolveIsolation::Isolated, QCNetworkResolveIsolation::NativeShared}) {
        auto request = makeRequest(v4.serverPort(), input, isolation, false);
        request.setIpResolve(family);
        NativeProbeOptions options;
        options.resolve     = input.resolve;
        options.ipResolve   = nativeFamily;
        const auto baseline = NetworkPathProbe::nativeEasy(request.url(), options);
        QCOMPARE(baseline.code, CURLE_OK);
        QCOMPARE(baseline.body, body);
        QCNetworkAccessManager manager;
        compareResult(NetworkPathProbe::asynchronous(&manager, request), baseline);
        compareResult(NetworkPathProbe::blocking(request), baseline);
    }
}
