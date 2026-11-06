#include "QCCurlMultiManager.h"
#include "QCNetworkConnectionPoolManager.h"
#include "QCNetworkHttpVersion.h"
#include "QCNetworkProxyConfig.h"
#include "QCNetworkReply.h"
#include "QCNetworkRequest.h"
#include "QCNetworkSslConfig.h"
#include "tst_QCNetworkProxy.h"

#include <QScopeGuard>
#include <QSignalSpy>
#include <QtTest>

using namespace QCurl;

namespace {
QStringList s_messages;

void captureTlsMessage(QtMsgType, const QMessageLogContext &, const QString &message)
{
    s_messages.append(message);
}
} // namespace

void TestQCNetworkProxy::tlsWarningsPrecedeFatal_data()
{
    QTest::addColumn<QByteArray>("fatalOption");
    QTest::newRow("proxy-warnings-then-http-version") << QByteArrayLiteral("CURLOPT_HTTP_VERSION");
    QTest::newRow("proxy-warnings-then-endpoint") << QByteArrayLiteral("CURLOPT_CAINFO");
}

void TestQCNetworkProxy::tlsWarningsPrecedeFatal()
{
    QFETCH(QByteArray, fatalOption);
    s_messages.clear();
    const auto previous = qInstallMessageHandler(captureTlsMessage);
    const auto restore  = qScopeGuard([previous]() {
        qInstallMessageHandler(previous);
        qunsetenv("QCURL_TEST_FORCE_CAPABILITY_ERROR");
        qunsetenv("QCURL_TEST_FORCE_SETOPT_ERROR");
    });
    qputenv("QCURL_TEST_FORCE_CAPABILITY_ERROR",
            "CURLOPT_PROXY_SSL_CIPHER_LIST,CURLOPT_PROXY_TLS13_CIPHERS");
    qputenv("QCURL_TEST_FORCE_SETOPT_ERROR", fatalOption);
    QCNetworkRequest request(QUrl(QStringLiteral("https://localhost:1/")));
    request.setHttpVersion(QCNetworkHttpVersion::Http1_1);
    QCNetworkProxyConfig proxy;
    proxy.setType(QCNetworkProxyConfig::ProxyType::Https);
    proxy.setHostName(QStringLiteral("localhost"));
    proxy.setPort(1);
    proxy.setPassword(QStringLiteral("credential-marker"));
    QCNetworkProxyConfig::ProxyTlsConfig tls;
    tls.setCipherList(QStringLiteral("cipher-marker"));
    tls.setTls13Ciphers(QStringLiteral("tls13-marker"));
    tls.setUnsupportedSecurityPolicy(QCUnsupportedSecurityOptionPolicy::Warn);
    proxy.setTlsConfig(tls);
    request.setProxyConfig(proxy);
    QCNetworkSslConfig endpoint;
    endpoint.setCaCertPath(QStringLiteral("ca-marker"));
    request.setSslConfig(endpoint);
    const auto requestsBefore = QCNetworkConnectionPoolManager::instance()->statistics();
    auto *reply               = m_manager->get(request);
    QSignalSpy errors(reply, qOverload<NetworkError>(&QCNetworkReply::error));
    QSignalSpy finished(reply, &QCNetworkReply::finished);
    QStringList warningsAtError;
    connect(reply, qOverload<NetworkError>(&QCNetworkReply::error), this, [&]() {
        warningsAtError = reply->capabilityWarnings();
    });
    QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 1000);
    QCOMPARE(errors.count(), 1);
    QCOMPARE(QCCurlMultiManager::instance()->runningRequestsCount(), 0);
    QCOMPARE(QCNetworkConnectionPoolManager::instance()->statistics().totalRequests(),
             requestsBefore.totalRequests());
    QCOMPARE(reply->error(), NetworkError::InvalidRequest);
    QVERIFY(reply->errorString().contains(QString::fromLatin1(fatalOption)));
    const auto warnings = reply->capabilityWarnings();
    QCOMPARE(warnings.size(), 2);
    QVERIFY(warnings[0].contains(QStringLiteral("CURLOPT_PROXY_SSL_CIPHER_LIST")));
    QVERIFY(warnings[1].contains(QStringLiteral("CURLOPT_PROXY_TLS13_CIPHERS")));
    QCOMPARE(warningsAtError, warnings);
    const auto logs = s_messages.filter(QStringLiteral("QCNetworkReply capability warning:"));
    QCOMPARE(logs.size(), 2);
    QVERIFY(logs[0].contains(warnings[0]));
    QVERIFY(logs[1].contains(warnings[1]));
    const auto diagnostic = s_messages.join(QLatin1Char('\n')) + reply->errorString();
    for (const auto &secret : {"credential-marker", "cipher-marker", "tls13-marker", "ca-marker"}) {
        QVERIFY2(!diagnostic.contains(QString::fromLatin1(secret)), qPrintable(diagnostic));
    }
}
