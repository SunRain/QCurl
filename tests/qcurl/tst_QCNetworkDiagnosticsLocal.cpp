/**
 * @file tst_QCNetworkDiagnosticsLocal.cpp
 * @brief 本地 deterministic diagnostics gate
 *
 * 只依赖本进程内的 localhost fixture，覆盖 DNS、TLS、HTTP probe 和 diagnose
 * 的可复核合同，不使用公网探测。
 */

#include "QCNetworkCancelToken.h"
#include "QCNetworkDiagnostics.h"
#include "qcurl_tls_test_server.h"
#include "test_source_paths.h"

#include <QByteArray>
#include <QFile>
#include <QFuture>
#include <QFutureWatcher>
#include <QHostAddress>
#include <QJsonDocument>
#include <QSharedPointer>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <QtTest/QtTest>

#include <type_traits>

using namespace QCurl;

static_assert(std::is_same_v<decltype(QCNetworkDiagnostics::resolveDNS(
                                 QStringLiteral("localhost"), QCNetworkDiagnosticsOptions{})),
                             QFuture<DiagResult>>,
              "Diagnostics must expose QFuture<DiagResult>");

namespace {

QCNetworkDiagnosticsOptions diagnosticsOptions(int timeoutMs)
{
    QCNetworkDiagnosticsOptions options;
    QString error;
    const bool ok = options.setTimeout(std::chrono::milliseconds{timeoutMs}, &error);
    Q_ASSERT_X(ok, "diagnosticsOptions", qPrintable(error));
    return options;
}

QCNetworkDiagnosticsOptions diagnosticsOptions(int timeoutMs, int port)
{
    QCNetworkDiagnosticsOptions options = diagnosticsOptions(timeoutMs);
    QString error;
    const bool ok = options.setPort(port, &error);
    Q_ASSERT_X(ok, "diagnosticsOptions", qPrintable(error));
    return options;
}

DiagResult awaitDiagnostics(QFuture<DiagResult> future, int timeoutMs = 10000)
{
    QFutureWatcher<DiagResult> watcher;
    QSignalSpy finishedSpy(&watcher, &QFutureWatcher<DiagResult>::finished);
    watcher.setFuture(future);
    if (!future.isFinished()) {
        finishedSpy.wait(timeoutMs);
    }

    if (future.resultCount() == 1) {
        return future.result();
    }

    DiagResult failure;
    failure.setSuccess(false);
    failure.setSummary(QStringLiteral("诊断 Future 未在 watchdog 内完成"));
    failure.setErrorString(QStringLiteral("TestWatchdogTimeout"));
    return failure;
}

QString diagMessage(const DiagResult &result)
{
    QString message = result.toString().trimmed();
    if (!result.details().isEmpty()) {
        const QJsonDocument detailsJson = QJsonDocument::fromVariant(result.details());
        if (!detailsJson.isNull()) {
            message += QStringLiteral("\nJSON details: %1")
                           .arg(QString::fromUtf8(detailsJson.toJson(QJsonDocument::Compact)));
        }
    }
    return message;
}

QByteArray buildHttpResponse(int statusCode, const QByteArray &reason, const QByteArray &body)
{
    QByteArray response;
    response += "HTTP/1.1 " + QByteArray::number(statusCode) + " " + reason + "\r\n";
    response += "Content-Type: text/plain\r\n";
    response += "Content-Length: " + QByteArray::number(body.size()) + "\r\n";
    response += "Connection: close\r\n\r\n";
    response += body;
    return response;
}

class LocalHttpServer final : public QObject
{
public:
    explicit LocalHttpServer(int responseDelayMs = 0, QObject *parent = nullptr)
        : QObject(parent)
        , m_responseDelayMs(responseDelayMs)
    {
        QObject::connect(&m_server, &QTcpServer::newConnection, this, [this]() {
            while (m_server.hasPendingConnections()) {
                QTcpSocket *socket = m_server.nextPendingConnection();
                if (!socket) {
                    continue;
                }
                setupSocket(socket);
            }
        });
    }

    bool start() { return m_server.listen(QHostAddress::LocalHost, 0); }

    QUrl url(const QString &path) const
    {
        return QUrl(QStringLiteral("http://localhost:%1%2").arg(m_server.serverPort()).arg(path));
    }

    QString errorString() const { return m_server.errorString(); }
    int requestCount() const { return m_requestCount; }
    QByteArray lastPath() const { return m_lastPath; }

private:
    void setupSocket(QTcpSocket *socket)
    {
        auto requestBuffer = QSharedPointer<QByteArray>::create();
        auto handled       = QSharedPointer<bool>::create(false);

        QObject::connect(socket,
                         &QTcpSocket::readyRead,
                         socket,
                         [this, socket, requestBuffer, handled]() {
                             if (*handled) {
                                 return;
                             }

                             requestBuffer->append(socket->readAll());
                             const int headerEnd = requestBuffer->indexOf("\r\n\r\n");
                             if (headerEnd < 0) {
                                 return;
                             }

                             *handled = true;

                             const QByteArray requestLine
                                 = requestBuffer->left(headerEnd).split('\n').value(0).trimmed();
                             QList<QByteArray> parts  = requestLine.split(' ');
                             const QByteArray rawPath = parts.size() >= 2 ? parts.at(1).trimmed()
                                                                          : QByteArrayLiteral("/");
                             const int queryPos       = rawPath.indexOf('?');
                             m_lastPath = queryPos >= 0 ? rawPath.left(queryPos) : rawPath;
                             ++m_requestCount;

                             const QByteArray path = m_lastPath;
                             QTimer::singleShot(m_responseDelayMs, socket, [socket, path]() {
                                 const bool notFound = path == QByteArrayLiteral("/missing");
                                 const QByteArray response
                                     = notFound ? buildHttpResponse(404, "Not Found", "missing")
                                                : buildHttpResponse(200, "OK", "ok");
                                 socket->write(response);
                                 socket->flush();
                                 socket->disconnectFromHost();
                             });
                         });
        QObject::connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
    }

    QTcpServer m_server;
    int m_requestCount = 0;
    QByteArray m_lastPath;
    int m_responseDelayMs = 0;
};

QString tlsFixturePath(const QString &fileName)
{
    const QString relativePath = QStringLiteral("tests/qcurl/testdata/http2/%1").arg(fileName);
    return TestSourcePaths::sourcePath(relativePath);
}

} // namespace

class tst_QCNetworkDiagnosticsLocal : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();

    void testResolveDNSLocalhost();
    void testProbeHTTPLocalSuccess();
    void testProbeHTTPLocalNotFound();
    void testDiagnoseLocalHTTP();
    void testCheckSSLLocalFixture();
    void testHeartbeatDuringHttpProbe();
    void testTimeoutCompletesOnce();
    void testCancellationAndOwnerDestroyCompleteOnce();
    void testFutureCancellationStopsOperation();
    void testConcurrentDiagnosticsCompleteOnce();
    void testProcessFailureCompletesOnce();
    void testStaticAsyncSourceContract();

private:
    QString m_certPath;
    QString m_keyPath;
};

void tst_QCNetworkDiagnosticsLocal::initTestCase()
{
    m_certPath = tlsFixturePath(QStringLiteral("localhost.crt"));
    m_keyPath  = tlsFixturePath(QStringLiteral("localhost.key"));

    QVERIFY2(QFile::exists(m_certPath),
             qPrintable(QStringLiteral("缺少 TLS 证书: %1").arg(m_certPath)));
    QVERIFY2(QFile::exists(m_keyPath),
             qPrintable(QStringLiteral("缺少 TLS 私钥: %1").arg(m_keyPath)));
}

void tst_QCNetworkDiagnosticsLocal::testResolveDNSLocalhost()
{
    const auto result = awaitDiagnostics(
        QCNetworkDiagnostics::resolveDNS(QStringLiteral("localhost"), diagnosticsOptions(1000)));

    QVERIFY2(result.success(), qPrintable(diagMessage(result)));
    QCOMPARE(result.details().value(QStringLiteral("hostname")).toString(),
             QStringLiteral("localhost"));
    QVERIFY(result.details()
                .value(QStringLiteral("ipv4"))
                .toStringList()
                .contains(QStringLiteral("127.0.0.1")));
}

void tst_QCNetworkDiagnosticsLocal::testProbeHTTPLocalSuccess()
{
    LocalHttpServer server;
    QVERIFY2(server.start(), qPrintable(server.errorString()));

    const auto result = awaitDiagnostics(
        QCNetworkDiagnostics::probeHTTP(server.url(QStringLiteral("/health")),
                                        diagnosticsOptions(3000)));

    QVERIFY2(result.success(), qPrintable(diagMessage(result)));
    QCOMPARE(result.details().value(QStringLiteral("statusCode")).toInt(), 200);
    QCOMPARE(server.requestCount(), 1);
    QCOMPARE(server.lastPath(), QByteArrayLiteral("/health"));
}

void tst_QCNetworkDiagnosticsLocal::testProbeHTTPLocalNotFound()
{
    LocalHttpServer server;
    QVERIFY2(server.start(), qPrintable(server.errorString()));

    const auto result = awaitDiagnostics(
        QCNetworkDiagnostics::probeHTTP(server.url(QStringLiteral("/missing")),
                                        diagnosticsOptions(3000)));

    QVERIFY2(!result.success(), qPrintable(diagMessage(result)));
    QVERIFY2(result.summary().contains(QStringLiteral("HTTP 探测失败")),
             qPrintable(diagMessage(result)));
    QCOMPARE(result.details().value(QStringLiteral("statusCode")).toInt(), 404);
    QCOMPARE(server.requestCount(), 1);
    QCOMPARE(server.lastPath(), QByteArrayLiteral("/missing"));
}

void tst_QCNetworkDiagnosticsLocal::testDiagnoseLocalHTTP()
{
    LocalHttpServer server;
    QVERIFY2(server.start(), qPrintable(server.errorString()));

    const auto result = awaitDiagnostics(
        QCNetworkDiagnostics::diagnose(server.url(QStringLiteral("/diagnose")),
                                       diagnosticsOptions(3000)));

    QVERIFY2(result.success(), qPrintable(diagMessage(result)));
    QCOMPARE(result.details().value(QStringLiteral("overallHealth")).toString(),
             QStringLiteral("excellent"));
    QVERIFY(result.details().contains(QStringLiteral("dns")));
    QVERIFY(result.details().contains(QStringLiteral("connection")));
    QVERIFY(result.details().contains(QStringLiteral("http")));
    QVERIFY(!result.details().contains(QStringLiteral("ssl")));

    const QVariantMap dnsResult = result.details().value(QStringLiteral("dns")).toMap();
    const QVariantMap connectionResult = result.details().value(QStringLiteral("connection")).toMap();
    const QVariantMap httpResult = result.details().value(QStringLiteral("http")).toMap();
    QVERIFY2(dnsResult.value(QStringLiteral("success")).toBool(), qPrintable(diagMessage(result)));
    QVERIFY2(connectionResult.value(QStringLiteral("success")).toBool(),
             qPrintable(diagMessage(result)));
    QVERIFY2(httpResult.value(QStringLiteral("success")).toBool(), qPrintable(diagMessage(result)));
    QCOMPARE(httpResult.value(QStringLiteral("statusCode")).toInt(), 200);
    QCOMPARE(server.lastPath(), QByteArrayLiteral("/diagnose"));
}

void tst_QCNetworkDiagnosticsLocal::testCheckSSLLocalFixture()
{
    LocalTlsServer server(m_certPath, m_keyPath);
    QVERIFY2(server.start(), qPrintable(server.errorString()));

    const auto result = awaitDiagnostics(
        QCNetworkDiagnostics::checkSSL(QStringLiteral("localhost"),
                                       diagnosticsOptions(4000, server.port())));

    QVERIFY2(server.connectionCount() > 0, "本地 TLS fixture 未收到任何连接");
    QVERIFY(!result.details().value(QStringLiteral("timedOut")).toBool());

    if (result.success()) {
        QVERIFY2(result.details().value(QStringLiteral("verified")).toBool(),
                 qPrintable(diagMessage(result)));
        QVERIFY(!result.details().value(QStringLiteral("issuer")).toString().isEmpty());
        QVERIFY(!result.details().value(QStringLiteral("subject")).toString().isEmpty());
        return;
    }

    QVERIFY2(result.summary().contains(QStringLiteral("SSL 握手失败")),
             qPrintable(diagMessage(result)));
    QVERIFY(result.details().contains(QStringLiteral("sslErrors"))
            || !result.errorString().isEmpty());
}

void tst_QCNetworkDiagnosticsLocal::testHeartbeatDuringHttpProbe()
{
    LocalHttpServer server(120);
    QVERIFY2(server.start(), qPrintable(server.errorString()));

    int heartbeatCount = 0;
    QTimer heartbeat;
    heartbeat.setInterval(5);
    connect(&heartbeat, &QTimer::timeout, this, [&heartbeatCount]() { ++heartbeatCount; });
    heartbeat.start();

    auto future       = QCNetworkDiagnostics::probeHTTP(server.url(QStringLiteral("/heartbeat")),
                                                        diagnosticsOptions(1000));
    const auto result = awaitDiagnostics(future, 3000);
    heartbeat.stop();

    QVERIFY2(result.success(), qPrintable(diagMessage(result)));
    QVERIFY2(heartbeatCount >= 5, "异步 HTTP 诊断期间 GUI heartbeat 未持续运行");
    QCOMPARE(future.resultCount(), 1);
}

void tst_QCNetworkDiagnosticsLocal::testTimeoutCompletesOnce()
{
    LocalHttpServer server(300);
    QVERIFY2(server.start(), qPrintable(server.errorString()));

    auto future       = QCNetworkDiagnostics::probeHTTP(server.url(QStringLiteral("/timeout")),
                                                        diagnosticsOptions(40));
    const auto result = awaitDiagnostics(future, 2000);

    QVERIFY(!result.success());
    QVERIFY(result.details().value(QStringLiteral("timedOut")).toBool());
    QCOMPARE(result.errorString(), QStringLiteral("Timeout"));
    QCOMPARE(future.resultCount(), 1);
}

void tst_QCNetworkDiagnosticsLocal::testCancellationAndOwnerDestroyCompleteOnce()
{
    LocalHttpServer server(300);
    QVERIFY2(server.start(), qPrintable(server.errorString()));

    auto *owner       = new QObject;
    auto *cancelToken = new QCNetworkCancelToken(owner);
    auto future       = QCNetworkDiagnostics::probeHTTP(server.url(QStringLiteral("/cancel")),
                                                        diagnosticsOptions(1000),
                                                        cancelToken);
    delete owner;

    const auto result = awaitDiagnostics(future, 2000);
    QVERIFY(!result.success());
    QVERIFY(result.details().value(QStringLiteral("cancelled")).toBool());
    QCOMPARE(result.errorString(), QStringLiteral("Cancelled"));
    QCOMPARE(future.resultCount(), 1);
}

void tst_QCNetworkDiagnosticsLocal::testFutureCancellationStopsOperation()
{
    LocalHttpServer server(300);
    QVERIFY2(server.start(), qPrintable(server.errorString()));

    auto future = QCNetworkDiagnostics::probeHTTP(server.url(QStringLiteral("/future-cancel")),
                                                  diagnosticsOptions(1000));
    future.cancel();

    QFutureWatcher<DiagResult> watcher;
    QSignalSpy finishedSpy(&watcher, &QFutureWatcher<DiagResult>::finished);
    watcher.setFuture(future);
    if (!future.isFinished()) {
        QVERIFY(finishedSpy.wait(2000));
    }
    QVERIFY(future.isCanceled());
    QVERIFY(future.isFinished());
    QCOMPARE(future.resultCount(), 0);
}

void tst_QCNetworkDiagnosticsLocal::testConcurrentDiagnosticsCompleteOnce()
{
    QList<QFuture<DiagResult>> futures;
    for (int index = 0; index < 8; ++index) {
        futures.append(QCNetworkDiagnostics::resolveDNS(QStringLiteral("localhost"),
                                                        diagnosticsOptions(1000)));
    }

    for (QFuture<DiagResult> &future : futures) {
        const auto result = awaitDiagnostics(future, 3000);
        QVERIFY2(result.success(), qPrintable(diagMessage(result)));
        QCOMPARE(future.resultCount(), 1);
    }
}

void tst_QCNetworkDiagnosticsLocal::testProcessFailureCompletesOnce()
{
    const QByteArray previousPath = qgetenv("PATH");
    qputenv("PATH", QByteArrayLiteral("/definitely-missing-qcurl-diagnostics"));
    auto future = QCNetworkDiagnostics::ping(QStringLiteral("localhost"), diagnosticsOptions(100));
    const auto result = awaitDiagnostics(future, 3000);
    qputenv("PATH", previousPath);

    QVERIFY(!result.success());
    QVERIFY(!result.errorString().isEmpty());
    QCOMPARE(future.resultCount(), 1);
}

void tst_QCNetworkDiagnosticsLocal::testStaticAsyncSourceContract()
{
    const QStringList sourceFiles = {
        QStringLiteral("src/QCNetworkDiagnostics.cpp"),
        QStringLiteral("src/QCNetworkDiagnosticsConnectivity.cpp"),
        QStringLiteral("src/QCNetworkDiagnosticsProcess.cpp"),
        QStringLiteral("src/private/QCNetworkDiagnosticsOperation.cpp"),
    };

    QByteArray implementation;
    for (const QString &relativePath : sourceFiles) {
        QFile source(TestSourcePaths::sourcePath(relativePath));
        QVERIFY2(source.open(QIODevice::ReadOnly), qPrintable(source.errorString()));
        implementation.append(source.readAll());
    }

    QVERIFY(!implementation.contains("QEventLoop"));
    QVERIFY(!implementation.contains("waitFor"));
    QVERIFY(!implementation.contains("QNetworkAccessManager"));
    QVERIFY(implementation.contains("QCNetworkAccessManager"));
    QVERIFY(implementation.contains("QHostInfo::lookupHost"));
    QVERIFY(implementation.contains("QProcess::finished"));
}

QTEST_MAIN(tst_QCNetworkDiagnosticsLocal)
#include "tst_QCNetworkDiagnosticsLocal.moc"
