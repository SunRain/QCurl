#include "QCCurlHandleManager.h"
#include "QCGlobal.h"
#include "QCNetworkAccessManager.h"
#include "QCNetworkConnectionPoolManager.h"
#include "QCNetworkDefaultLogger.h"
#include "QCNetworkDiagnosticQueryPolicy.h"
#include "QCNetworkError.h"
#include "QCNetworkHttpVersion.h"
#include "QCNetworkProxyConfig.h"
#include "QCNetworkReply.h"
#include "QCNetworkRequest.h"
#include "QCNetworkSslConfig.h"
#include "qcnetwork_connect_test_proxy.h"
#include "qcnetwork_http2_test.h"
#include "qcnetwork_no_proxy_guard.h"
#include "qcnetwork_trace_mode.h"
#include "test_source_paths.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEvent>
#include <QEventLoop>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaMethod>
#include <QProcess>
#include <QRegularExpression>
#include <QScopeGuard>
#include <QSet>
#include <QSharedPointer>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QThread>
#include <QTimer>
#include <QtTest/QtTest>

#include <curl/curl.h>

using namespace QCurl;

namespace {
void verifyProxyRequests(const QList<QByteArray> &requests)
{
    QCOMPARE(requests.size(), 2);
    QVERIFY(!requests.first().toLower().contains("proxy-authorization:"));
    const QByteArray credentials = QByteArrayLiteral("synthetic-proxy-user:synthetic-proxy-secret")
                                       .toBase64();
    QVERIFY(
        requests.last().contains(QByteArrayLiteral("Proxy-Authorization: Basic ") + credentials));
}

void verifyProxyTrace(const QCNetworkDefaultLogger &capture, QCNetworkDiagnosticQueryPolicy policy)
{
    const QByteArray credentials = QByteArrayLiteral("synthetic-proxy-user:synthetic-proxy-secret")
                                       .toBase64();
    QString outgoing;
    QString allTrace;
    bool proxyAuthTextObserved = false;
    for (const auto &entry : capture.entries()) {
        if (entry.category() != QStringLiteral("Trace")) {
            continue;
        }
        const QString message = entry.message();
        allTrace += message + QLatin1Char('\n');
        QVERIFY(!message.contains(QStringLiteral("origin-header-marker")));
        QVERIFY(!message.contains(QStringLiteral("synthetic-proxy-user")));
        QVERIFY(!message.contains(QStringLiteral("synthetic-proxy-secret")));
        QVERIFY(!message.contains(QString::fromLatin1(credentials)));
        QVERIFY(!message.contains(QStringLiteral("proxy-fragment-marker")));
        QVERIFY(message.size() <= 4096);
        if (message.startsWith(QStringLiteral("HEADER_OUT:"))) {
            outgoing += message + QLatin1Char('\n');
        } else {
            QVERIFY(!message.contains(QStringLiteral("proxy-query-marker")));
            proxyAuthTextObserved |= message.startsWith(QStringLiteral("TEXT:"))
                                     && message.contains(QStringLiteral("Proxy auth using Basic"));
        }
    }
    QVERIFY2(proxyAuthTextObserved, qPrintable(allTrace));
    QCOMPARE(outgoing.count(QStringLiteral("CONNECT ")), 2);
    QVERIFY(outgoing.contains(QStringLiteral("Proxy-Authorization: [REDACTED]")));
    const QString target = policy == QCNetworkDiagnosticQueryPolicy::Full
                               ? QStringLiteral("GET /reqinfo?route=proxy-query-marker HTTP/2")
                               : QStringLiteral("GET /reqinfo?[REDACTED] HTTP/2");
    QVERIFY2(outgoing.contains(target), qPrintable(outgoing));
}

void verifyHttp2Trace(const QCNetworkDefaultLogger &capture, QCNetworkDiagnosticQueryPolicy policy)
{
    QString trace;
    QString outgoing;
    QString text;
    for (const auto &entry : capture.entries()) {
        if (entry.category() != QStringLiteral("Trace")) {
            continue;
        }
        QVERIFY(entry.message().size() <= 4096);
        trace += entry.message() + QLatin1Char('\n');
        if (entry.message().startsWith(QStringLiteral("HEADER_OUT:"))) {
            outgoing += entry.message();
        } else if (entry.message().startsWith(QStringLiteral("TEXT:"))) {
            text += entry.message();
        }
    }
    QVERIFY2(outgoing.contains(QStringLiteral("GET /reqinfo")), qPrintable(trace));
    QVERIFY2(outgoing.contains(QStringLiteral("HTTP/2")), qPrintable(outgoing));
    const QString target = policy == QCNetworkDiagnosticQueryPolicy::Full
                               ? QStringLiteral(
                                     "GET /reqinfo?route=initial-query-marker&encoded=%2B HTTP/2")
                               : QStringLiteral("GET /reqinfo?[REDACTED] HTTP/2");
    QVERIFY2(outgoing.contains(target), qPrintable(outgoing));
    QVERIFY(!text.contains(QStringLiteral("initial-query-marker")));
    for (const auto &secret : {QStringLiteral("auth-marker"),
                               QStringLiteral("cookie-marker"),
                               QStringLiteral("api-key-marker"),
                               QStringLiteral("token-marker"),
                               QStringLiteral("signature-marker"),
                               QStringLiteral("response-secret"),
                               QStringLiteral("session=response-secret"),
                               QStringLiteral("initial-fragment-marker")}) {
        QVERIFY2(!trace.contains(secret), qPrintable(trace));
    }
    QVERIFY(trace.contains(QStringLiteral("[REDACTED]")));
    QVERIFY(trace.contains(QStringLiteral("set-cookie: [REDACTED]")));
    QVERIFY(trace.contains(QStringLiteral("x-auth-token: [REDACTED]")));
    const QRegularExpression id(QStringLiteral(R"(HEADER_OUT: \[(\d+)-(\d+)\] GET )"));
    QCOMPARE(id.match(outgoing).hasMatch(), qgetenv("QCURL_TEST_LIB_IDS") == "on");
}
} // namespace

void TestQCNetworkHttp2::testDiagnosticQueryPolicy_data()
{
    QTest::addColumn<QCNetworkDiagnosticQueryPolicy>("policy");
    QTest::addColumn<QString>("path");
    for (const auto &path : {QStringLiteral("/reqinfo"), QStringLiteral("/a/./../reqinfo")}) {
        QTest::newRow(qPrintable(path + QStringLiteral("-redacted")))
            << QCNetworkDiagnosticQueryPolicy::Redacted << path;
        QTest::newRow(qPrintable(path + QStringLiteral("-full")))
            << QCNetworkDiagnosticQueryPolicy::Full << path;
    }
}

void TestQCNetworkHttp2::testDiagnosticQueryPolicy()
{
    QFETCH(QCNetworkDiagnosticQueryPolicy, policy);
    QFETCH(QString, path);
    auto logger   = QCNetworkLoggerHandle::create<QCNetworkDefaultLogger>();
    auto *capture = static_cast<QCNetworkDefaultLogger *>(logger.get());
    capture->enableConsoleOutput(false);
    capture->setMinLogLevel(NetworkLogLevel::Debug);
    m_manager->setLogger(logger);
    m_manager->setDebugTraceEnabled(true);
    const auto restore = qScopeGuard([this] {
        m_manager->setDebugTraceEnabled(false);
        m_manager->setLogger({});
    });

    QUrl url(m_h2BaseUrl + path);
    url.setQuery(QStringLiteral("route=initial-query-marker&encoded=%2B"));
    url.setFragment(QStringLiteral("initial-fragment-marker"));
    QCNetworkRequest request(url);
    request.setHttpVersion(QCNetworkHttpVersion::Http2TLS);
    request.setSslConfig(QCNetworkSslConfig::insecureConfig());
    request.setDiagnosticQueryPolicy(policy);
    request.setRawHeader(QByteArrayLiteral("Authorization"),
                         QByteArrayLiteral("Bearer auth-marker"));
    request.setRawHeader(QByteArrayLiteral("Cookie"), QByteArrayLiteral("session=cookie-marker"));
    request.setRawHeader(QByteArrayLiteral("X-Api-Key"), QByteArrayLiteral("api-key-marker"));
    request.setRawHeader(QByteArrayLiteral("X-Auth-Token"), QByteArrayLiteral("token-marker"));
    request.setRawHeader(QByteArrayLiteral("X-Signature"), QByteArrayLiteral("signature-marker"));
    auto *reply        = m_manager->get(request);
    const auto dispose = qScopeGuard([reply] { reply->deleteLater(); });
    QVERIFY(waitForSignal(reply, QMetaMethod::fromSignal(&QCNetworkReply::finished), 15000));
    QVERIFY2(reply->error() == NetworkError::NoError, qPrintable(reply->errorString()));
    const auto body = reply->readAll();
    QVERIFY(body.has_value());
    const auto echoed = QJsonDocument::fromJson(*body).object();
    QCOMPARE(echoed.value(QStringLiteral("httpVersion")).toString(), QStringLiteral("2.0"));
    const auto query = echoed.value(QStringLiteral("query")).toObject();
    QCOMPARE(query.value(QStringLiteral("route")).toString(),
             QStringLiteral("initial-query-marker"));
    QCOMPARE(query.value(QStringLiteral("encoded")).toString(), QStringLiteral("+"));
    QCOMPARE(reply->url(), url);
    QCOMPARE(reply->diagnosticQueryPolicy(), policy);

    verifyHttp2Trace(*capture, policy);
}

void TestQCNetworkHttp2::testDiagnosticQueryPolicyThroughProxy_data()
{
    testDiagnosticQueryPolicy_data();
}

void TestQCNetworkHttp2::testDiagnosticQueryPolicyThroughProxy()
{
    QFETCH(QCNetworkDiagnosticQueryPolicy, policy);
    QFETCH(QString, path);
    QVERIFY2(startLocalTestServer(), qPrintable(m_serverError));
    ConnectTestProxy proxyServer(static_cast<quint16>(QUrl(m_localH2BaseUrl).port()));
    QVERIFY(proxyServer.start());
    const NoProxyGuard environment;
    QCNetworkAccessManager manager;
    QCNetworkDefaultLogger *capture = nullptr;
    const auto logger               = QCNetworkLoggerHandle::createWithBorrow(&capture);
    capture->enableConsoleOutput(false);
    capture->setMinLogLevel(NetworkLogLevel::Debug);
    manager.setLogger(logger);
    manager.setDebugTraceEnabled(true);
    QCNetworkProxyConfig proxy;
    proxy.setType(QCNetworkProxyConfig::ProxyType::Http);
    proxy.setHostName(QStringLiteral("127.0.0.1"));
    proxy.setPort(proxyServer.port());
    proxy.setUserName(QStringLiteral("synthetic-proxy-user"));
    proxy.setPassword(QStringLiteral("synthetic-proxy-secret"));
    QUrl url(m_localH2BaseUrl + path + QStringLiteral("?route=proxy-query-marker"));
    url.setFragment(QStringLiteral("proxy-fragment-marker"));
    QCNetworkRequest request(url);
    request.setProxyConfig(proxy);
    request.setHttpVersion(QCNetworkHttpVersion::Http2TLS);
    request.setSslConfig(QCNetworkSslConfig::insecureConfig());
    request.setDiagnosticQueryPolicy(policy);
    request.setRawHeader("Authorization", "Bearer origin-header-marker");
    auto *reply = manager.get(request);
    QSignalSpy finished(reply, &QCNetworkReply::finished);
    QVERIFY(finished.wait(15000));
    QVERIFY2(reply->error() == NetworkError::NoError, qPrintable(reply->errorString()));
    QVERIFY2(proxyServer.errorString().isEmpty(), qPrintable(proxyServer.errorString()));
    verifyProxyRequests(proxyServer.requests());
    const auto echoed = QJsonDocument::fromJson(reply->readAll().value_or(QByteArray())).object();
    QCOMPARE(echoed.value(QStringLiteral("httpVersion")).toString(), QStringLiteral("2.0"));
    QCOMPARE(echoed.value(QStringLiteral("query"))
                 .toObject()
                 .value(QStringLiteral("route"))
                 .toString(),
             QStringLiteral("proxy-query-marker"));
    verifyProxyTrace(*capture, policy);
}
