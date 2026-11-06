#include "QCBlockingNetworkClient.h"
#include "QCNetworkProxyConfig.h"
#include "QCNetworkReply.h"
#include "QCNetworkRequest.h"
#include "QCNetworkSslConfig.h"
#include "QCNetworkTimeoutConfig.h"
#include "qcnetwork_connect_test_proxy.h"
#include "qcnetwork_no_proxy_guard.h"
#include "qcurl_tls_test_server.h"
#include "test_source_paths.h"
#include "tst_QCNetworkProxy.h"

#include <QSignalSpy>
#include <QSslCipher>
#include <QtTest>

#include <curl/curl.h>
#include <future>

using namespace QCurl;
using namespace std::chrono_literals;

namespace {

QString fixture(const QString &name)
{
    return TestSourcePaths::sourcePath(QStringLiteral("tests/qcurl/testdata/http2/") + name);
}

QCNetworkSslConfig trustedOrigin()
{
    QCNetworkSslConfig result;
    result.setCaCertPath(fixture(QStringLiteral("localhost.crt")));
    return result;
}

QCNetworkRequest requestFor(LocalTlsServer &server)
{
    QCNetworkRequest request(QUrl(QStringLiteral("https://localhost:%1/").arg(server.port())));
    request.setSslConfig(trustedOrigin());
    QCNetworkTimeoutConfig timeout;
    timeout.setTotalTimeout(3000ms);
    request.setTimeoutConfig(timeout);
    return request;
}

void verifyExchange(QCNetworkReply *reply, LocalTlsServer &server, bool succeeds)
{
    QSignalSpy finished(reply, &QCNetworkReply::finished);
    QSignalSpy errors(reply, qOverload<NetworkError>(&QCNetworkReply::error));
    QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 4000);
    QVERIFY2((reply->error() == NetworkError::NoError) == succeeds,
             qPrintable(QStringLiteral("curl=%1: %2")
                            .arg(reply->diagnosticCurlCode())
                            .arg(reply->errorString())));
    QCOMPARE(errors.count(), succeeds ? 0 : 1);
    QCOMPARE(server.requests().size(), succeeds ? 1 : 0);
    if (succeeds) {
        QCOMPARE(reply->httpStatusCode(), 200);
        QCOMPARE(reply->readAll().value(), QByteArrayLiteral("tls-ok"));
        QCOMPARE(reply->diagnosticCurlCode(), int(CURLE_OK));
    }
}

} // namespace

void TestQCNetworkProxy::endpointTlsVerification_data()
{
    QTest::addColumn<QString>("scenario");
    QTest::addColumn<bool>("succeeds");
    for (const auto &name : {"trusted",
                             "untrusted",
                             "system-ca",
                             "peer-disabled",
                             "host-mismatch",
                             "host-disabled",
                             "pin-valid",
                             "pin-mismatch"}) {
        const QString scenario = QString::fromLatin1(name);
        QTest::newRow(name) << scenario
                            << (scenario == QStringLiteral("trusted")
                                || scenario.endsWith(QStringLiteral("disabled"))
                                || scenario == QStringLiteral("pin-valid"));
    }
}

void TestQCNetworkProxy::endpointTlsVerification()
{
    QFETCH(QString, scenario);
    QFETCH(bool, succeeds);
    LocalTlsServer server(fixture(QStringLiteral("localhost.crt")),
                          fixture(QStringLiteral("localhost.key")));
    server.setHttpResponseEnabled(true);
    QVERIFY2(server.start(), qPrintable(server.errorString()));
    auto request = requestFor(server);
    auto config  = trustedOrigin();
    if (scenario == QStringLiteral("untrusted") || scenario == QStringLiteral("peer-disabled")) {
        config.setCaCertPath(fixture(QStringLiteral("unicode-host.crt")));
        config.setVerifyPeer(scenario != QStringLiteral("peer-disabled"));
    } else if (scenario == QStringLiteral("system-ca")) {
        config = QCNetworkSslConfig{};
    } else if (scenario.startsWith(QStringLiteral("host-"))) {
        request.setUrl(QUrl(QStringLiteral("https://mismatch.invalid:%1/").arg(server.port())));
        QCOMPARE(request.setResolveOverride(
                     {QStringLiteral("mismatch.invalid:%1:127.0.0.1").arg(server.port())}),
                 QCNetworkConfigUpdateResult::Applied);
        config.setVerifyHost(scenario != QStringLiteral("host-disabled"));
    } else if (scenario.startsWith(QStringLiteral("pin-"))) {
        config.setPinnedPublicKey(
            scenario == QStringLiteral("pin-valid")
                ? QStringLiteral("sha256//c+b6Q1AtXTCPl1keWd2pfGndgov8AGKedY7U9Nk7Ib8=")
                : QStringLiteral("sha256//AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA="));
    }
    request.setSslConfig(config);
    auto *reply = m_manager->get(request);
    verifyExchange(reply, server, succeeds);
    if (!succeeds) {
        QCOMPARE(reply->diagnosticCurlCode(),
                 int(scenario == QStringLiteral("pin-mismatch") ? CURLE_SSL_PINNEDPUBKEYNOTMATCH
                                                                : CURLE_PEER_FAILED_VERIFICATION));
    }
    if (!scenario.startsWith(QStringLiteral("pin-"))) {
        auto pending = std::async(std::launch::async,
                                  [request]() { return QCBlockingNetworkClient().get(request); });
        QTRY_VERIFY(pending.wait_for(0ms) == std::future_status::ready);
        const auto result = pending.get();
        QCOMPARE(result.isSuccess(), succeeds);
        QCOMPARE(server.requests().size(), succeeds ? 2 : 0);
        if (!succeeds) {
            QCOMPARE(result.diagnosticCurlCode(), int(CURLE_PEER_FAILED_VERIFICATION));
        }
    }
}

void TestQCNetworkProxy::endpointTlsNegotiation_data()
{
    QTest::addColumn<bool>("tls13");
    QTest::addColumn<bool>("cipherControl");
    QTest::addColumn<bool>("succeeds");
    QTest::newRow("minimum-compatible") << false << false << true;
    QTest::newRow("minimum-rejects-tls12") << false << false << false;
    QTest::newRow("tls12-cipher-compatible") << false << true << true;
    QTest::newRow("tls12-cipher-no-overlap") << false << true << false;
    QTest::newRow("tls13-cipher-compatible") << true << true << true;
    QTest::newRow("tls13-cipher-no-overlap") << true << true << false;
}

void TestQCNetworkProxy::endpointTlsNegotiation()
{
    QFETCH(bool, tls13);
    QFETCH(bool, cipherControl);
    QFETCH(bool, succeeds);
    LocalTlsServer server(fixture(QStringLiteral("localhost.crt")),
                          fixture(QStringLiteral("localhost.key")));
    auto serverConfig = QSslConfiguration::defaultConfiguration();
    serverConfig.setProtocol(tls13 ? QSsl::TlsV1_3 : QSsl::TlsV1_2);
    serverConfig.setPeerVerifyMode(QSslSocket::VerifyNone);
    const QString allowed = tls13 ? QStringLiteral("TLS_AES_128_GCM_SHA256")
                                  : QStringLiteral("ECDHE-RSA-AES128-GCM-SHA256");
    const QSslCipher cipher(allowed);
    QVERIFY2(!cipher.isNull(), qPrintable(allowed));
    serverConfig.setCiphers({cipher});
    server.setTlsConfiguration(serverConfig);
    server.setHttpResponseEnabled(true);
    QVERIFY2(server.start(), qPrintable(server.errorString()));
    auto request = requestFor(server);
    auto config  = trustedOrigin();
    if (cipherControl) {
        const QString offered = succeeds ? allowed
                                : tls13  ? QStringLiteral("TLS_AES_256_GCM_SHA384")
                                         : QStringLiteral("ECDHE-RSA-AES256-GCM-SHA384");
        if (tls13) {
            config.setTls13Ciphers(offered);
        } else {
            config.setCipherList(offered);
        }
    } else {
        config.setMinTlsVersion(succeeds ? QCNetworkTlsVersion::Tls1_2
                                         : QCNetworkTlsVersion::Tls1_3);
    }
    request.setSslConfig(config);
    auto *reply = m_manager->get(request);
    verifyExchange(reply, server, succeeds);
    if (!succeeds) {
        QCOMPARE(reply->diagnosticCurlCode(), int(CURLE_SSL_CONNECT_ERROR));
    }
}

void TestQCNetworkProxy::endpointClientCredentials_data()
{
    QTest::addColumn<int>("credentials");
    QTest::addColumn<bool>("succeeds");
    QTest::newRow("missing-client-certificate") << 0 << false;
    QTest::newRow("certificate-and-key") << 1 << true;
    QTest::newRow("encrypted-key-correct-password") << 2 << true;
    QTest::newRow("encrypted-key-wrong-password") << 3 << false;
    QTest::newRow("certificate-without-key") << 4 << false;
}

void TestQCNetworkProxy::endpointClientCredentials()
{
    QFETCH(int, credentials);
    QFETCH(bool, succeeds);
    LocalTlsServer server(fixture(QStringLiteral("localhost.crt")),
                          fixture(QStringLiteral("localhost.key")));
    auto serverConfig = QSslConfiguration::defaultConfiguration();
    serverConfig.setProtocol(QSsl::TlsV1_2);
    serverConfig.setCaCertificates(
        QSslCertificate::fromPath(fixture(QStringLiteral("localhost.crt"))));
    serverConfig.setPeerVerifyMode(QSslSocket::VerifyPeer);
    server.setTlsConfiguration(serverConfig);
    server.setHttpResponseEnabled(true);
    QVERIFY2(server.start(), qPrintable(server.errorString()));
    auto request = requestFor(server);
    auto config  = trustedOrigin();
    if (credentials != 0) {
        config.setClientCertPath(fixture(QStringLiteral("localhost.crt")));
        if (credentials != 4) {
            config.setClientKeyPath(fixture(credentials >= 2
                                                ? QStringLiteral("localhost-encrypted.key")
                                                : QStringLiteral("localhost.key")));
        }
        if (credentials >= 2 && credentials != 4) {
            config.setClientKeyPassword(credentials == 2 ? QStringLiteral("qcurl-test-passphrase")
                                                         : QStringLiteral("wrong-test-password"));
        }
    }
    request.setSslConfig(config);
    verifyExchange(m_manager->get(request), server, succeeds);
}

void TestQCNetworkProxy::httpsProxyTlsIsolation_data()
{
    QTest::addColumn<QString>("scenario");
    QTest::addColumn<bool>("succeeds");
    for (const auto &name : {"trusted",
                             "proxy-untrusted",
                             "origin-untrusted",
                             "proxy-peer-disabled",
                             "proxy-host-mismatch",
                             "proxy-host-disabled"}) {
        const QString scenario = QString::fromLatin1(name);
        QTest::newRow(name) << scenario
                            << (scenario == QStringLiteral("trusted")
                                || scenario.endsWith(QStringLiteral("disabled")));
    }
}

void TestQCNetworkProxy::httpsProxyTlsIsolation()
{
    QFETCH(QString, scenario);
    QFETCH(bool, succeeds);
    NoProxyGuard noProxy;
    LocalTlsServer origin(fixture(QStringLiteral("localhost.crt")),
                          fixture(QStringLiteral("localhost.key")));
    origin.setHttpResponseEnabled(true);
    QVERIFY2(origin.start(), qPrintable(origin.errorString()));
    ConnectTestProxy proxy(origin.port());
    QVERIFY2(proxy.startTls(fixture(QStringLiteral("localhost.crt")),
                            fixture(QStringLiteral("localhost.key"))),
             qPrintable(proxy.errorString()));
    auto request = requestFor(origin);
    if (scenario == QStringLiteral("origin-untrusted")) {
        auto config = trustedOrigin();
        config.setCaCertPath(fixture(QStringLiteral("unicode-host.crt")));
        request.setSslConfig(config);
    }
    QCNetworkProxyConfig proxyConfig;
    proxyConfig.setType(QCNetworkProxyConfig::ProxyType::Https);
    proxyConfig.setHostName(QStringLiteral("localhost"));
    proxyConfig.setPort(proxy.port());
    proxyConfig.setUserName(QStringLiteral("test-user"));
    proxyConfig.setPassword(QStringLiteral("test-password"));
    QCNetworkProxyConfig::ProxyTlsConfig tls;
    tls.setCaCertPath(fixture(QStringLiteral("localhost.crt")));
    if (scenario == QStringLiteral("proxy-untrusted")
        || scenario == QStringLiteral("proxy-peer-disabled")) {
        tls.setCaCertPath(fixture(QStringLiteral("unicode-host.crt")));
        tls.setVerifyPeer(scenario != QStringLiteral("proxy-peer-disabled"));
    }
    if (scenario.startsWith(QStringLiteral("proxy-host-"))) {
        proxyConfig.setHostName(QStringLiteral("proxy.invalid"));
        QCOMPARE(request.setResolveOverride(
                     {QStringLiteral("proxy.invalid:%1:127.0.0.1").arg(proxy.port())}),
                 QCNetworkConfigUpdateResult::Applied);
        tls.setVerifyHost(scenario == QStringLiteral("proxy-host-mismatch"));
    }
    proxyConfig.setTlsConfig(tls);
    request.setProxyConfig(proxyConfig);
    auto *reply = m_manager->get(request);
    verifyExchange(reply, origin, succeeds);
    if (scenario == QStringLiteral("proxy-untrusted")
        || scenario == QStringLiteral("proxy-host-mismatch")) {
        QVERIFY(proxy.requests().isEmpty());
        QCOMPARE(origin.connectionCount(), 0);
        QCOMPARE(reply->diagnosticCurlCode(), int(CURLE_PEER_FAILED_VERIFICATION));
    } else {
        QVERIFY(!proxy.requests().isEmpty());
        if (succeeds) {
            QCOMPARE(proxy.requests().size(), 2);
            QVERIFY(!proxy.requests().first().toLower().contains("\r\nproxy-authorization:"));
            QVERIFY(proxy.requests().last().contains(
                "Proxy-Authorization: Basic " + QByteArray("test-user:test-password").toBase64()));
        }
    }
}

void TestQCNetworkProxy::httpsProxyTlsNegotiation_data()
{
    endpointTlsNegotiation_data();
}

void TestQCNetworkProxy::httpsProxyTlsNegotiation()
{
    QFETCH(bool, tls13);
    QFETCH(bool, cipherControl);
    QFETCH(bool, succeeds);
    NoProxyGuard noProxy;
    LocalTlsServer origin(fixture(QStringLiteral("localhost.crt")),
                          fixture(QStringLiteral("localhost.key")));
    origin.setHttpResponseEnabled(true);
    QVERIFY2(origin.start(), qPrintable(origin.errorString()));
    auto configuration = QSslConfiguration::defaultConfiguration();
    configuration.setProtocol(tls13 ? QSsl::TlsV1_3 : QSsl::TlsV1_2);
    const QString allowed = tls13 ? QStringLiteral("TLS_AES_128_GCM_SHA256")
                                  : QStringLiteral("ECDHE-RSA-AES128-GCM-SHA256");
    const QSslCipher cipher(allowed);
    QVERIFY2(!cipher.isNull(), qPrintable(allowed));
    configuration.setCiphers({cipher});
    ConnectTestProxy proxy(origin.port());
    QVERIFY2(proxy.startTls(fixture(QStringLiteral("localhost.crt")),
                            fixture(QStringLiteral("localhost.key")),
                            configuration),
             qPrintable(proxy.errorString()));
    auto request = requestFor(origin);
    QCNetworkProxyConfig proxyConfig;
    proxyConfig.setType(QCNetworkProxyConfig::ProxyType::Https);
    proxyConfig.setHostName(QStringLiteral("localhost"));
    proxyConfig.setPort(proxy.port());
    proxyConfig.setUserName(QStringLiteral("test-user"));
    proxyConfig.setPassword(QStringLiteral("test-password"));
    QCNetworkProxyConfig::ProxyTlsConfig tls;
    tls.setCaCertPath(fixture(QStringLiteral("localhost.crt")));
    if (cipherControl) {
        const QString offered = succeeds ? allowed
                                : tls13  ? QStringLiteral("TLS_AES_256_GCM_SHA384")
                                         : QStringLiteral("ECDHE-RSA-AES256-GCM-SHA384");
        if (tls13) {
            tls.setTls13Ciphers(offered);
        } else {
            tls.setCipherList(offered);
        }
    } else {
        tls.setMinTlsVersion(succeeds ? QCNetworkTlsVersion::Tls1_2 : QCNetworkTlsVersion::Tls1_3);
    }
    proxyConfig.setTlsConfig(tls);
    request.setProxyConfig(proxyConfig);
    auto *reply = m_manager->get(request);
    verifyExchange(reply, origin, succeeds);
    if (succeeds) {
        QCOMPARE(proxy.requests().size(), 2);
    } else {
        QVERIFY(proxy.requests().isEmpty());
        QCOMPARE(origin.connectionCount(), 0);
        QCOMPARE(reply->diagnosticCurlCode(), int(CURLE_SSL_CONNECT_ERROR));
    }
}
