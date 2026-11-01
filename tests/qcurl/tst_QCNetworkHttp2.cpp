/**
 * @file tst_QCNetworkHttp2.cpp
 * @brief 覆盖 HTTP/2 协商、复用、降级和本地 TLS 场景。
 *
 * 默认使用 `tests/qcurl/http2-test-server.js` 启动本地可控 server。
 * 也可通过 `QCURL_HTTP2_TEST_BASE_URL` 和
 * `QCURL_HTTP2_TEST_HTTP1_BASE_URL` 覆盖 HTTP/2 与 HTTP/1.1 入口。
 * `QCURL_HTTP2_DISABLE_DOWNGRADE_TEST=1` 仅用于 gate/planner 侧显式排除降级用例，
 * 不应依赖 suite 内部跳过。
 */

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

// ============================================================================
// 本地 server 配置
// ============================================================================

static const char *kHttp2TestBaseUrlEnvName = "QCURL_HTTP2_TEST_BASE_URL";
static const char *kHttp1TestBaseUrlEnvName = "QCURL_HTTP2_TEST_HTTP1_BASE_URL";
static const char *kDisableDowngradeEnvName = "QCURL_HTTP2_DISABLE_DOWNGRADE_TEST";

namespace {

bool outputSuggestsLocalListenRestriction(const QString &output)
{
    const QString normalized = output.toLower();
    return normalized.contains(QStringLiteral("listen eperm"))
           || normalized.contains(QStringLiteral("operation not permitted"))
           || normalized.contains(QStringLiteral("permission denied"));
}

} // namespace

/// 固定转发到本地 TLS 夹具的 CONNECT 代理；首次 407 用于验证原生认证重发与 trace 资格。
class ConnectTestProxy final : public QObject
{
    Q_OBJECT

public:
    explicit ConnectTestProxy(quint16 upstreamPort)
        : m_upstreamPort(upstreamPort)
    {
        connect(&m_server, &QTcpServer::newConnection, this, [this] {
            while (auto *client = m_server.nextPendingConnection()) {
                acceptSocket(client);
            }
        });
    }

    bool start() { return m_server.listen(QHostAddress::LocalHost, 0); }
    quint16 port() const { return m_server.serverPort(); }
    QList<QByteArray> requests() const { return m_requests; }
    QString errorString() const { return m_errorString; }

private:
    Q_DISABLE_COPY_MOVE(ConnectTestProxy)

    void acceptSocket(QTcpSocket *client)
    {
        auto pending = QSharedPointer<QByteArray>::create();
        connect(client, &QTcpSocket::disconnected, client, &QObject::deleteLater);
        connect(client, &QTcpSocket::readyRead, this, [this, client, pending] {
            pending->append(client->readAll());
            const qsizetype end = pending->indexOf("\r\n\r\n");
            if (end < 0) {
                if (pending->size() > 16384) {
                    m_errorString = QStringLiteral("CONNECT 请求头超过夹具上限");
                    client->disconnectFromHost();
                }
                return;
            }
            disconnect(client, &QTcpSocket::readyRead, this, nullptr);
            const QByteArray request = pending->first(end + 4);
            m_requests.append(request);
            if (!request.startsWith("CONNECT ")) {
                m_errorString = QStringLiteral("代理未收到 CONNECT 请求");
                client->disconnectFromHost();
                return;
            }
            if (!request.toLower().contains("\r\nproxy-authorization:")) {
                client->write("HTTP/1.1 407 Proxy Authentication Required\r\n"
                              "Proxy-Authenticate: Basic realm=\"qcurl-fixture\"\r\n"
                              "Content-Length: 0\r\nConnection: close\r\n\r\n");
                client->disconnectFromHost();
                return;
            }
            auto *upstream = new QTcpSocket(client);
            connect(upstream, &QTcpSocket::connected, client, [client, upstream, pending, end] {
                client->write("HTTP/1.1 200 Connection established\r\n\r\n");
                upstream->write(pending->sliced(end + 4));
                upstream->write(client->readAll());
                connect(client, &QTcpSocket::readyRead, upstream, [client, upstream] {
                    upstream->write(client->readAll());
                });
            });
            connect(upstream, &QTcpSocket::readyRead, client, [client, upstream] {
                client->write(upstream->readAll());
            });
            connect(upstream, &QTcpSocket::disconnected, client, &QTcpSocket::disconnectFromHost);
            connect(upstream, &QTcpSocket::errorOccurred, client, [this, client, upstream] {
                m_errorString = upstream->errorString();
                client->disconnectFromHost();
            });
            upstream->connectToHost(QHostAddress::LocalHost, m_upstreamPort);
        });
    }

    QTcpServer m_server{this};
    quint16 m_upstreamPort;
    QList<QByteArray> m_requests;
    QString m_errorString;
};

class TestQCNetworkHttp2 : public QObject
{
    Q_OBJECT

public:
    TestQCNetworkHttp2() = default;

private Q_SLOTS:
    void initTestCase();
    void cleanupTestCase();
    void init();
    void cleanup();

    // 协议能力与协商
    void testHttp2Support();           // 检测 HTTP/2 支持
    void testHttp2Negotiation();       // HTTP/2 协议协商（ALPN）
    void testHttp2Multiplexing();      // 多路复用（单连接多请求）
    void testHttp2HeaderCompression(); // 头部压缩（HPACK）
    void testHttp2Downgrade();         // 协议降级到 HTTP/1.1
    void testHttp2WithSsl();           // HTTP/2 over TLS (h2)
    void testHttp2ConcurrentStreams(); // 并发流限制
    void testHttp2ConnectionReuse();   // 连接复用验证

    // 顺序请求基线
    void testHttp2VsHttp1Performance(); // 性能对比（简化版）
    void testSchedulerAdmissionAndStreamLimit_data();
    void testSchedulerAdmissionAndStreamLimit();
    void testDiagnosticQueryPolicy_data();
    void testDiagnosticQueryPolicy();
    void testDiagnosticQueryPolicyThroughProxy_data();
    void testDiagnosticQueryPolicyThroughProxy();

private:
    Q_DISABLE_COPY_MOVE(TestQCNetworkHttp2)
    static constexpr const char *kJsonErrorKey = "_qcurl_test_error";

    QCNetworkAccessManager *m_manager = nullptr;

    QProcess m_localServer;
    QString m_h2BaseUrl;
    QString m_http1BaseUrl;
    QString m_localH2BaseUrl;
    QString m_localHttp1BaseUrl;
    QString m_serverError;
    bool m_disableDowngradeTest = false;

    // 辅助方法
    bool waitForSignal(QObject *obj, const QMetaMethod &signal, int timeout = 10000);
    bool checkHttp2Support(); // 检查编译期与运行期 HTTP/2 能力
    static bool waitForPortReady(quint16 port, int timeoutMs);
    bool startLocalTestServer();
    void stopLocalTestServer();
    QJsonObject requestJson(const QUrl &url,
                            QCNetworkHttpVersion httpVersion,
                            const QList<QPair<QByteArray, QByteArray>> &headers = {},
                            int timeoutMs                                       = 15000);
};

// ============================================================================
// 辅助方法实现
// ============================================================================

bool TestQCNetworkHttp2::waitForSignal(QObject *obj, const QMetaMethod &signal, int timeout)
{
    // 避免 “信号已先触发 -> 新建 QSignalSpy 永远等不到” 的假超时。
    auto *reply = qobject_cast<QCNetworkReply *>(obj);
    if (signal == QMetaMethod::fromSignal(&QCNetworkReply::finished)) {
        if (reply && reply->state() == ReplyState::Finished) {
            return true;
        }
    }

    QSignalSpy spy(obj, signal);
    if (spy.count() > 0) {
        return true;
    }
    if (spy.wait(timeout)) {
        return true;
    }

    if (signal == QMetaMethod::fromSignal(&QCNetworkReply::finished)) {
        return reply && reply->state() == ReplyState::Finished;
    }

    return false;
}

bool TestQCNetworkHttp2::checkHttp2Support()
{
    const curl_version_info_data *ver = curl_version_info(CURLVERSION_NOW);
    return QCurl::hasHttp2Support() && ver && ((ver->features & CURL_VERSION_HTTP2) != 0);
}

bool TestQCNetworkHttp2::waitForPortReady(quint16 port, int timeoutMs)
{
    QElapsedTimer timer;
    timer.start();

    while (timer.elapsed() < timeoutMs) {
        QTcpSocket probe;
        probe.connectToHost(QHostAddress::LocalHost, port);
        if (probe.waitForConnected(100)) {
            probe.disconnectFromHost();
            return true;
        }
        QThread::msleep(50);
    }

    return false;
}

bool TestQCNetworkHttp2::startLocalTestServer()
{
    const bool usesLocalH2    = m_h2BaseUrl.isEmpty() || m_h2BaseUrl == m_localH2BaseUrl;
    const bool usesLocalHttp1 = m_http1BaseUrl.isEmpty() || m_http1BaseUrl == m_localHttp1BaseUrl;
    stopLocalTestServer();
    m_serverError.clear();
    m_localH2BaseUrl.clear();
    m_localHttp1BaseUrl.clear();

    const QString scriptPath = TestSourcePaths::sourcePath(
        QStringLiteral("tests/qcurl/http2-test-server.js"));
    if (!QFileInfo::exists(scriptPath)) {
        m_serverError = QStringLiteral(
            "未找到本地 HTTP/2 测试服务器脚本（tests/qcurl/http2-test-server.js）。");
        return false;
    }

    m_localServer.setProgram(QStringLiteral("node"));
    m_localServer.setArguments({scriptPath,
                                QStringLiteral("--h2-port"),
                                QStringLiteral("0"),
                                QStringLiteral("--http1-port"),
                                QStringLiteral("0")});
    m_localServer.setProcessChannelMode(QProcess::MergedChannels);
    m_localServer.start();

    if (!m_localServer.waitForStarted(2000)) {
        m_serverError = QStringLiteral(
            "无法启动本地 HTTP/2 测试服务器（node）。请确认 node 可用。");
        return false;
    }

    QElapsedTimer timer;
    timer.start();
    QByteArray output;

    QRegularExpression re(QStringLiteral(R"(QCURL_HTTP2_TEST_SERVER_READY\s+(\{.*\})\s*)"));
    while (timer.elapsed() < 5000) {
        if (m_localServer.waitForReadyRead(200)) {
            output += m_localServer.readAll();
            const QString outText               = QString::fromUtf8(output);
            const QRegularExpressionMatch match = re.match(outText);
            if (match.hasMatch()) {
                const QByteArray jsonBytes = match.captured(1).toUtf8();
                QJsonParseError parseError{};
                const QJsonDocument doc = QJsonDocument::fromJson(jsonBytes, &parseError);
                if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
                    m_serverError = QStringLiteral("本地 HTTP/2 测试服务器输出格式错误：%1")
                                        .arg(parseError.errorString());
                    break;
                }

                const QJsonObject obj = doc.object();
                const int h2Port      = obj.value(QStringLiteral("h2Port")).toInt(0);
                const int http1Port   = obj.value(QStringLiteral("http1Port")).toInt(0);
                if (h2Port <= 0 || http1Port <= 0) {
                    m_serverError = QStringLiteral("本地 HTTP/2 测试服务器返回端口无效。");
                    break;
                }

                if (!waitForPortReady(static_cast<quint16>(h2Port), 3000)
                    || !waitForPortReady(static_cast<quint16>(http1Port), 3000)) {
                    m_serverError = QStringLiteral(
                        "本地 HTTP/2 测试服务器未在预期时间内就绪（端口不可连接）。");
                    break;
                }

                m_localH2BaseUrl    = QStringLiteral("https://127.0.0.1:%1")
                                          .arg(QString::number(h2Port));
                m_localHttp1BaseUrl = QStringLiteral("https://127.0.0.1:%1")
                                          .arg(QString::number(http1Port));
                // 重启夹具后同步默认入口；外部覆盖的 URL 不变，不能继续连接已关闭的旧端口。
                if (usesLocalH2) {
                    m_h2BaseUrl = m_localH2BaseUrl;
                }
                if (usesLocalHttp1) {
                    m_http1BaseUrl = m_localHttp1BaseUrl;
                }
                return true;
            }
        }
    }

    const QString outText = QString::fromUtf8(output + m_localServer.readAll());
    if (!outText.isEmpty()) {
        qWarning().noquote() << "Local HTTP/2 server output:\n" << outText;
        if (outputSuggestsLocalListenRestriction(outText)) {
            m_serverError = QStringLiteral("当前执行环境禁止本地 HTTP/2 测试服务器监听 127.0.0.1");
        }
    }

    stopLocalTestServer();
    if (m_serverError.isEmpty()) {
        m_serverError = QStringLiteral("本地 HTTP/2 测试服务器未在预期时间内输出 READY 信号。");
    }
    return false;
}

void TestQCNetworkHttp2::stopLocalTestServer()
{
    if (m_localServer.state() == QProcess::NotRunning) {
        m_localH2BaseUrl.clear();
        m_localHttp1BaseUrl.clear();
        return;
    }

    m_localServer.terminate();
    if (!m_localServer.waitForFinished(1500)) {
        m_localServer.kill();
        m_localServer.waitForFinished(1500);
    }

    m_localH2BaseUrl.clear();
    m_localHttp1BaseUrl.clear();
}

QJsonObject TestQCNetworkHttp2::requestJson(const QUrl &url,
                                            QCNetworkHttpVersion httpVersion,
                                            const QList<QPair<QByteArray, QByteArray>> &headers,
                                            int timeoutMs)
{
    QCNetworkRequest request(url);
    request.setHttpVersion(httpVersion);
    request.setSslConfig(QCNetworkSslConfig::insecureConfig());

    for (const auto &kv : headers) {
        request.setRawHeader(kv.first, kv.second);
    }

    auto *reply = m_manager->get(request);
    if (!waitForSignal(reply, QMetaMethod::fromSignal(&QCNetworkReply::finished), timeoutMs)) {
        const QString err = reply->errorString();
        reply->cancel();
        reply->deleteLater();
        return QJsonObject{
            {QString::fromUtf8(kJsonErrorKey),
             QStringLiteral("请求超时（%1ms）：%2").arg(timeoutMs).arg(err)},
        };
    }

    if (reply->error() != NetworkError::NoError) {
        const QString err = reply->errorString();
        reply->deleteLater();
        return QJsonObject{
            {QString::fromUtf8(kJsonErrorKey), QStringLiteral("请求失败：%1").arg(err)},
        };
    }

    auto data = reply->readAll();
    reply->deleteLater();
    if (!data.has_value()) {
        return QJsonObject{
            {QString::fromUtf8(kJsonErrorKey), QStringLiteral("响应体为空（std::nullopt）")},
        };
    }

    QJsonParseError parseError{};
    const QJsonDocument doc = QJsonDocument::fromJson(*data, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        return QJsonObject{
            {QString::fromUtf8(kJsonErrorKey),
             QStringLiteral("JSON 解析失败：%1").arg(parseError.errorString())},
        };
    }
    if (!doc.isObject()) {
        return QJsonObject{
            {QString::fromUtf8(kJsonErrorKey), QStringLiteral("JSON 顶层必须是 object")},
        };
    }
    return doc.object();
}

// ============================================================================
// 测试初始化
// ============================================================================

void TestQCNetworkHttp2::initTestCase()
{
    QVERIFY2(checkHttp2Support(),
             "HTTP/2 capability preflight 应已阻止无 HTTP/2 能力的环境运行该 suite。");

    m_disableDowngradeTest = !qgetenv(kDisableDowngradeEnvName).trimmed().isEmpty()
                             && qgetenv(kDisableDowngradeEnvName).trimmed() != "0";

    const QString overriddenH2    = QString::fromUtf8(qgetenv(kHttp2TestBaseUrlEnvName)).trimmed();
    const QString overriddenHttp1 = QString::fromUtf8(qgetenv(kHttp1TestBaseUrlEnvName)).trimmed();

    if (!overriddenH2.isEmpty()) {
        m_h2BaseUrl = overriddenH2;
    }
    if (!overriddenHttp1.isEmpty()) {
        m_http1BaseUrl = overriddenHttp1;
    }

    if (m_h2BaseUrl.isEmpty() || m_http1BaseUrl.isEmpty()) {
        const bool localServerReady = startLocalTestServer();
        const QString reason = QStringLiteral("本地 HTTP/2 测试服务器不可用：%1").arg(m_serverError);
        QVERIFY2(localServerReady, qPrintable(reason));

        if (m_h2BaseUrl.isEmpty()) {
            m_h2BaseUrl = m_localH2BaseUrl;
        }
        if (m_http1BaseUrl.isEmpty()) {
            m_http1BaseUrl = m_localHttp1BaseUrl;
        }
    }

    qDebug() << "HTTP/2 base URL:" << m_h2BaseUrl;
    qDebug() << "HTTP/1.1 base URL:" << m_http1BaseUrl;

    m_manager = new QCNetworkAccessManager(this);
}

void TestQCNetworkHttp2::cleanupTestCase()
{
    stopLocalTestServer();
    m_manager = nullptr;
}

void TestQCNetworkHttp2::init() {}

void TestQCNetworkHttp2::cleanup() {}

// ============================================================================
// HTTP/2 功能测试
// ============================================================================

void TestQCNetworkHttp2::testHttp2Support()
{
    // 枚举层和 runtime 能力都必须暴露 HTTP/2。
    QVERIFY(static_cast<int>(QCNetworkHttpVersion::Http2) > 0);
    QVERIFY(static_cast<int>(QCNetworkHttpVersion::Http2TLS) > 0);
    QVERIFY(checkHttp2Support());
}

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

    QString trace;
    QString outgoing;
    QString text;
    for (const auto &entry : capture->entries()) {
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
                               QStringLiteral("initial-fragment-marker")}) {
        QVERIFY2(!trace.contains(secret), qPrintable(trace));
    }
    QVERIFY(trace.contains(QStringLiteral("[REDACTED]")));
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
    const QByteArray previousNoProxy      = qgetenv("no_proxy");
    const QByteArray previousUpperNoProxy = qgetenv("NO_PROXY");
    const bool hadNoProxy                 = qEnvironmentVariableIsSet("no_proxy");
    const bool hadUpperNoProxy            = qEnvironmentVariableIsSet("NO_PROXY");
    const auto restoreEnvironment         = qScopeGuard([&] {
        if (hadNoProxy) {
            qputenv("no_proxy", previousNoProxy);
        } else {
            qunsetenv("no_proxy");
        }
        if (hadUpperNoProxy) {
            qputenv("NO_PROXY", previousUpperNoProxy);
        } else {
            qunsetenv("NO_PROXY");
        }
    });
    qunsetenv("no_proxy");
    qunsetenv("NO_PROXY");
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
    const auto requests = proxyServer.requests();
    QCOMPARE(requests.size(), 2);
    QVERIFY(!requests.first().toLower().contains("proxy-authorization:"));
    const QByteArray credentials = QByteArrayLiteral("synthetic-proxy-user:synthetic-proxy-secret")
                                       .toBase64();
    QVERIFY(
        requests.last().contains(QByteArrayLiteral("Proxy-Authorization: Basic ") + credentials));
    const auto echoed = QJsonDocument::fromJson(reply->readAll().value_or(QByteArray())).object();
    QCOMPARE(echoed.value(QStringLiteral("httpVersion")).toString(), QStringLiteral("2.0"));
    QCOMPARE(echoed.value(QStringLiteral("query"))
                 .toObject()
                 .value(QStringLiteral("route"))
                 .toString(),
             QStringLiteral("proxy-query-marker"));
    QString outgoing;
    bool proxyAuthTextObserved = false;
    for (const auto &entry : capture->entries()) {
        if (entry.category() != QStringLiteral("Trace")) {
            continue;
        }
        const QString message = entry.message();
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
    QVERIFY(proxyAuthTextObserved);
    QCOMPARE(outgoing.count(QStringLiteral("CONNECT ")), 2);
    QVERIFY(outgoing.contains(QStringLiteral("Proxy-Authorization: [REDACTED]")));
    const QString target = policy == QCNetworkDiagnosticQueryPolicy::Full
                               ? QStringLiteral("GET /reqinfo?route=proxy-query-marker HTTP/2")
                               : QStringLiteral("GET /reqinfo?[REDACTED] HTTP/2");
    QVERIFY2(outgoing.contains(target), qPrintable(outgoing));
}

void TestQCNetworkHttp2::testHttp2Negotiation()
{
    const QUrl url(m_h2BaseUrl + QStringLiteral("/reqinfo?case=negotiation"));
    const QJsonObject obj = requestJson(url, QCNetworkHttpVersion::Http2TLS);
    QVERIFY2(!obj.contains(QString::fromUtf8(kJsonErrorKey)),
             qPrintable(obj.value(QString::fromUtf8(kJsonErrorKey)).toString()));
    QCOMPARE(obj.value(QStringLiteral("httpVersion")).toString(), QStringLiteral("2.0"));
    QVERIFY(obj.value(QStringLiteral("sessionId")).toInt() > 0);
    QVERIFY(obj.value(QStringLiteral("streamId")).toInt() > 0);
}

void TestQCNetworkHttp2::testHttp2Multiplexing()
{
    // 同一 server 下的多请求应共享一个 session，并分配不同 streamId。
    QList<QCNetworkReply *> replies;

    for (int i = 0; i < 5; ++i) {
        const QUrl url(m_h2BaseUrl
                       + QStringLiteral("/reqinfo?id=%1&delay_ms=150").arg(QString::number(i)));
        QCNetworkRequest request(url);
        request.setHttpVersion(QCNetworkHttpVersion::Http2TLS);
        request.setSslConfig(QCNetworkSslConfig::insecureConfig());
        auto *reply = m_manager->get(request);
        replies.append(reply);
    }

    for (int i = 0; i < replies.size(); ++i) {
        auto *reply = replies[i];
        if (!waitForSignal(reply, QMetaMethod::fromSignal(&QCNetworkReply::finished), 30000)) {
            qWarning() << "HTTP/2 Multiplexing timeout on request" << i;
            for (auto *pendingReply : replies) {
                if (pendingReply) {
                    pendingReply->cancel();
                    pendingReply->deleteLater();
                }
            }
            QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
            QFAIL("HTTP/2 多路复用测试超时（单请求 30s 未完成）");
        }
    }

    QSet<int> streamIds;
    int sessionId = -1;
    for (int i = 0; i < replies.size(); ++i) {
        auto *reply = replies[i];
        QVERIFY2(reply->error() == NetworkError::NoError, qPrintable(reply->errorString()));
        const auto data = reply->readAll();
        QVERIFY(data.has_value());

        QJsonParseError parseError{};
        const QJsonDocument doc = QJsonDocument::fromJson(*data, &parseError);
        QVERIFY2(parseError.error == QJsonParseError::NoError, qPrintable(parseError.errorString()));
        const QJsonObject obj = doc.object();

        QCOMPARE(obj.value(QStringLiteral("httpVersion")).toString(), QStringLiteral("2.0"));
        const int sid      = obj.value(QStringLiteral("sessionId")).toInt();
        const int streamId = obj.value(QStringLiteral("streamId")).toInt();
        QVERIFY(sid > 0);
        QVERIFY(streamId > 0);
        if (sessionId < 0) {
            sessionId = sid;
        }
        QCOMPARE(sid, sessionId);
        QVERIFY2(!streamIds.contains(streamId),
                 qPrintable(QStringLiteral("streamId 重复：%1").arg(streamId)));
        streamIds.insert(streamId);
    }

    for (auto *reply : replies) {
        if (reply) {
            reply->deleteLater();
        }
    }
}

void TestQCNetworkHttp2::testHttp2HeaderCompression()
{
    // 构造高 header 密度请求，验证服务端仍能正确回显。
    QCNetworkRequest request(QUrl(m_h2BaseUrl + QStringLiteral("/reqinfo?case=headers")));
    request.setHttpVersion(QCNetworkHttpVersion::Http2TLS);
    request.setSslConfig(QCNetworkSslConfig::insecureConfig());

    for (int i = 0; i < 10; ++i) {
        request.setRawHeader(QStringLiteral("X-Custom-Header-%1").arg(i).toUtf8(),
                             QStringLiteral("Value-%1-With-Long-Content-For-Compression-Test")
                                 .arg(i)
                                 .toUtf8());
    }

    auto *reply = m_manager->get(request);
    if (!waitForSignal(reply, QMetaMethod::fromSignal(&QCNetworkReply::finished), 15000)) {
        const QString err = reply->errorString();
        reply->cancel();
        reply->deleteLater();
        QFAIL(qPrintable(QStringLiteral("HTTP/2 头部压缩测试 15s 内未完成：%1").arg(err)));
    }

    if (reply->error() != NetworkError::NoError) {
        const QString err = reply->errorString();
        reply->deleteLater();
        QFAIL(qPrintable(QStringLiteral("HTTP/2 头部压缩测试失败：%1").arg(err)));
    }

    QCOMPARE(reply->error(), NetworkError::NoError);

    const auto data = reply->readAll();
    QVERIFY(data.has_value());

    QJsonParseError parseError{};
    const QJsonDocument doc = QJsonDocument::fromJson(*data, &parseError);
    QVERIFY2(parseError.error == QJsonParseError::NoError, qPrintable(parseError.errorString()));
    const QJsonObject obj = doc.object();
    QCOMPARE(obj.value(QStringLiteral("httpVersion")).toString(), QStringLiteral("2.0"));

    const QJsonObject echoed = obj.value(QStringLiteral("headers")).toObject();
    for (int i = 0; i < 10; ++i) {
        const QString k = QStringLiteral("x-custom-header-%1").arg(QString::number(i));
        QVERIFY2(echoed.contains(k), qPrintable(QStringLiteral("未回显 header: %1").arg(k)));
    }

    reply->deleteLater();
}

void TestQCNetworkHttp2::testHttp2Downgrade()
{
    QVERIFY2(!m_disableDowngradeTest,
             "QCURL_HTTP2_DISABLE_DOWNGRADE_TEST 已设置；请在 gate/planner "
             "侧显式排除降级用例，而不是依赖 suite 内跳过。");

    QVERIFY2(
        !m_http1BaseUrl.isEmpty(),
        "HTTP/1.1 base URL 为空：请设置 QCURL_HTTP2_TEST_HTTP1_BASE_URL 或使用默认本地 server。");

    const QUrl url(m_http1BaseUrl + QStringLiteral("/reqinfo?case=downgrade"));
    const QJsonObject obj = requestJson(url, QCNetworkHttpVersion::Http2TLS);
    QVERIFY2(!obj.contains(QString::fromUtf8(kJsonErrorKey)),
             qPrintable(obj.value(QString::fromUtf8(kJsonErrorKey)).toString()));
    QCOMPARE(obj.value(QStringLiteral("httpVersion")).toString(), QStringLiteral("1.1"));
}

void TestQCNetworkHttp2::testHttp2WithSsl()
{
    const QUrl url(m_h2BaseUrl + QStringLiteral("/reqinfo?case=h2tls"));
    const QJsonObject obj = requestJson(url, QCNetworkHttpVersion::Http2TLS);
    QVERIFY2(!obj.contains(QString::fromUtf8(kJsonErrorKey)),
             qPrintable(obj.value(QString::fromUtf8(kJsonErrorKey)).toString()));
    QCOMPARE(obj.value(QStringLiteral("httpVersion")).toString(), QStringLiteral("2.0"));
}

void TestQCNetworkHttp2::testHttp2ConcurrentStreams()
{
    // 共享同一 session 的多请求应获得不同的 streamId。

    QElapsedTimer timer;
    timer.start();

    QList<QCNetworkReply *> replies;
    for (int i = 0; i < 10; ++i) {
        const QUrl url(m_h2BaseUrl
                       + QStringLiteral("/reqinfo?stream=%1&delay_ms=200").arg(QString::number(i)));
        QCNetworkRequest request(url);
        request.setHttpVersion(QCNetworkHttpVersion::Http2TLS);
        request.setSslConfig(QCNetworkSslConfig::insecureConfig());

        auto *reply = m_manager->get(request);
        replies.append(reply);
    }

    // 慢环境下保留更宽松的等待窗口，避免把环境抖动误判为协议缺陷。
    for (int i = 0; i < replies.size(); ++i) {
        auto *reply = replies[i];
        if (!waitForSignal(reply, QMetaMethod::fromSignal(&QCNetworkReply::finished), 30000)) {
            qWarning() << "HTTP/2 ConcurrentStreams timeout on request" << i;
            for (auto *pendingReply : replies) {
                if (pendingReply) {
                    pendingReply->cancel();
                    pendingReply->deleteLater();
                }
            }
            QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
            QFAIL("HTTP/2 并发流测试超时（单请求 30s 未完成）");
        }
    }

    qint64 elapsed = timer.elapsed();

    QSet<int> streamIds;
    int sessionId = -1;
    for (auto *reply : replies) {
        QVERIFY(reply->error() == NetworkError::NoError);
        const auto data = reply->readAll();
        QVERIFY(data.has_value());

        QJsonParseError parseError{};
        const QJsonDocument doc = QJsonDocument::fromJson(*data, &parseError);
        QVERIFY2(parseError.error == QJsonParseError::NoError, qPrintable(parseError.errorString()));
        const QJsonObject obj = doc.object();
        QCOMPARE(obj.value(QStringLiteral("httpVersion")).toString(), QStringLiteral("2.0"));

        const int sid      = obj.value(QStringLiteral("sessionId")).toInt();
        const int streamId = obj.value(QStringLiteral("streamId")).toInt();
        QVERIFY(sid > 0);
        QVERIFY(streamId > 0);
        if (sessionId < 0) {
            sessionId = sid;
        }
        QCOMPARE(sid, sessionId);
        QVERIFY(!streamIds.contains(streamId));
        streamIds.insert(streamId);
    }

    qDebug() << "总耗时：" << elapsed << "ms";

    for (auto *reply : replies) {
        if (reply) {
            reply->deleteLater();
        }
    }
}

void TestQCNetworkHttp2::testHttp2ConnectionReuse()
{
    // 顺序请求也应复用同一 session，但 streamId 仍需递增且不重复。

    int sessionId = -1;
    QSet<int> streamIds;

    for (int i = 0; i < 5; ++i) {
        QCNetworkRequest request(
            QUrl(m_h2BaseUrl + QStringLiteral("/reqinfo?seq=%1").arg(QString::number(i))));
        request.setHttpVersion(QCNetworkHttpVersion::Http2TLS);
        request.setSslConfig(QCNetworkSslConfig::insecureConfig());

        auto *reply = m_manager->get(request);
        if (!waitForSignal(reply, QMetaMethod::fromSignal(&QCNetworkReply::finished), 15000)) {
            const QString err = reply->errorString();
            reply->cancel();
            reply->deleteLater();
            QFAIL(qPrintable(QStringLiteral("HTTP/2 连接复用测试 15s 内未完成：%1").arg(err)));
        }

        QVERIFY2(reply->error() == NetworkError::NoError, qPrintable(reply->errorString()));
        const auto data = reply->readAll();
        QVERIFY(data.has_value());

        QJsonParseError parseError{};
        const QJsonDocument doc = QJsonDocument::fromJson(*data, &parseError);
        QVERIFY2(parseError.error == QJsonParseError::NoError, qPrintable(parseError.errorString()));
        const QJsonObject obj = doc.object();
        QCOMPARE(obj.value(QStringLiteral("httpVersion")).toString(), QStringLiteral("2.0"));
        const int sid      = obj.value(QStringLiteral("sessionId")).toInt();
        const int streamId = obj.value(QStringLiteral("streamId")).toInt();
        QVERIFY(sid > 0);
        QVERIFY(streamId > 0);

        if (sessionId < 0) {
            sessionId = sid;
        }
        QCOMPARE(sid, sessionId);
        QVERIFY(!streamIds.contains(streamId));
        streamIds.insert(streamId);

        reply->deleteLater();
    }
}

void TestQCNetworkHttp2::testHttp2VsHttp1Performance()
{
    const int requestCount = 10;

    // 顺序请求基线：HTTP/1.1
    QElapsedTimer http1Timer;
    http1Timer.start();

    for (int i = 0; i < requestCount; ++i) {
        QCNetworkRequest request(
            QUrl(m_http1BaseUrl + QStringLiteral("/reqinfo?http1=%1").arg(QString::number(i))));
        request.setHttpVersion(QCNetworkHttpVersion::Http1_1);
        request.setSslConfig(QCNetworkSslConfig::insecureConfig());

        auto *reply = m_manager->get(request);
        if (!waitForSignal(reply, QMetaMethod::fromSignal(&QCNetworkReply::finished), 15000)) {
            const QString err = reply->errorString();
            reply->cancel();
            reply->deleteLater();
            QFAIL(qPrintable(QStringLiteral("HTTP/1.1 基准请求 15s 内未完成：%1").arg(err)));
        }
        if (reply->error() != NetworkError::NoError) {
            const QString errorString = reply->errorString();
            reply->deleteLater();
            QFAIL(qPrintable(QStringLiteral("HTTP/1.1 基准请求失败：%1").arg(errorString)));
        }
        reply->deleteLater();
    }

    qint64 http1Elapsed = http1Timer.elapsed();

    // 顺序请求基线：HTTP/2
    QElapsedTimer http2Timer;
    http2Timer.start();

    for (int i = 0; i < requestCount; ++i) {
        QCNetworkRequest request(
            QUrl(m_h2BaseUrl + QStringLiteral("/reqinfo?http2=%1").arg(QString::number(i))));
        request.setHttpVersion(QCNetworkHttpVersion::Http2TLS);
        request.setSslConfig(QCNetworkSslConfig::insecureConfig());

        auto *reply = m_manager->get(request);
        if (!waitForSignal(reply, QMetaMethod::fromSignal(&QCNetworkReply::finished), 15000)) {
            const QString err = reply->errorString();
            reply->cancel();
            reply->deleteLater();
            QFAIL(qPrintable(QStringLiteral("HTTP/2 基准请求 15s 内未完成：%1").arg(err)));
        }
        if (reply->error() != NetworkError::NoError) {
            const QString errorString = reply->errorString();
            reply->deleteLater();
            QFAIL(qPrintable(QStringLiteral("HTTP/2 基准请求失败：%1").arg(errorString)));
        }
        reply->deleteLater();
    }

    qint64 http2Elapsed = http2Timer.elapsed();
    qDebug() << "顺序请求耗时对比(ms): http1=" << http1Elapsed << "http2=" << http2Elapsed;

    // 不强制要求 HTTP/2 更快（因为测试环境差异），只验证流程可用
}

void TestQCNetworkHttp2::testSchedulerAdmissionAndStreamLimit_data()
{
    QTest::addColumn<int>("streamLimit");
    QTest::newRow("one-stream") << 1;
    QTest::newRow("two-streams") << 2;
}

void TestQCNetworkHttp2::testSchedulerAdmissionAndStreamLimit()
{
    QFETCH(int, streamLimit);
    // 使用新连接隔离各行的 HTTP/2 SETTINGS，不把复用旧连接的协商状态当作新配额。
    QVERIFY2(startLocalTestServer(), qPrintable(m_serverError));
    auto *pool          = QCNetworkConnectionPoolManager::instance();
    const auto original = pool->config();
    const auto restore  = qScopeGuard([&]() {
        QCOMPARE(pool->setConfig(original), QCNetworkConnectionPoolManager::UpdateResult::Applied);
    });
    QCNetworkConnectionPoolConfig config;
    config.setMultiMaxTotalConnections(1);
    config.setMultiMaxHostConnections(1);
    config.setMultiMaxConcurrentStreams(streamLimit);
    config.setMultiplexingEnabled(true);
    QCOMPARE(pool->setConfig(config), QCNetworkConnectionPoolManager::UpdateResult::Applied);
    QCNetworkAccessManager manager;
    manager.enableRequestScheduler(true);
    auto policy = manager.schedulerPolicy();
    policy.setMaxConcurrentRequests(3);
    policy.setMaxRequestsPerHost(3);
    QVERIFY(manager.setSchedulerPolicy(policy));
    QList<QCNetworkReply *> replies;
    for (int index = 0; index < 3; ++index) {
        QCNetworkRequest request(QUrl(m_localH2BaseUrl + QStringLiteral("/reqinfo?delay_ms=200")));
        request.setHttpVersion(QCNetworkHttpVersion::Http2TLS);
        request.setSslConfig(QCNetworkSslConfig::insecureConfig());
        replies.append(manager.get(request));
    }
    QCOMPARE(manager.schedulerStatistics().runningRequests(), 3);
    int peak = 0;
    QSet<int> sessions;
    for (auto *reply : replies) {
        QTRY_VERIFY_WITH_TIMEOUT(reply->isFinished(), 5000);
        QCOMPARE(reply->error(), NetworkError::NoError);
        const auto document = QJsonDocument::fromJson(reply->readAll().value_or(QByteArray()));
        QVERIFY(document.isObject());
        const auto object = document.object();
        QCOMPARE(object.value(QStringLiteral("httpVersion")).toString(), QStringLiteral("2.0"));
        peak = qMax(peak, object.value(QStringLiteral("activeStreams")).toInt());
        sessions.insert(object.value(QStringLiteral("sessionId")).toInt());
    }
    QCOMPARE(sessions.size(), 1);
    QVERIFY(!sessions.contains(0));
    QCOMPARE(peak, streamLimit);
}

QTEST_MAIN(TestQCNetworkHttp2)
#include "tst_QCNetworkHttp2.moc"
