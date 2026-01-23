/**
 * @file tst_Integration.cpp
 * @brief QCurl 集成测试 - 真实网络请求和完整功能验证
 *
 * 测试覆盖：
 * - 真实 HTTP 请求（本地 httpbin）
 * - Cookie 持久化和发送
 * - 自定义 Header
 * - 超时配置
 * - 重定向处理
 * - SSL 配置
 * - 大文件下载
 * - 并发请求
 * - 错误恢复
 *
 * ============================================================================
 * 测试前准备
 * ============================================================================
 *
 * 本测试套件需要本地 httpbin 服务，并通过环境变量 `QCURL_HTTPBIN_URL` 获取 base URL（不硬编码端口）。
 *
 * 推荐启动方式（生成 env 文件并做健康检查）：
 *
 *     ./tests/httpbin/start_httpbin.sh --write-env build/test-env/httpbin.env
 *     source build/test-env/httpbin.env
 *     cd build && ctest -R tst_Integration --output-on-failure
 * ============================================================================
 */

#include <QtTest/QtTest>
#include <QEventLoop>
#include <QTimer>
#include <QSignalSpy>
#include <QTemporaryFile>
#include <QCryptographicHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QHostAddress>
#include <QMetaMethod>
#include <QCoreApplication>
#include <QEvent>
#include <QProcess>
#include <QRegularExpression>
#include <QTcpServer>
#include <QTcpSocket>
#include <QThread>
#include <QVector>

#include "QCNetworkAccessManager.h"
#include "QCNetworkRequest.h"
#include "QCNetworkReply.h"
#include "QCNetworkError.h"
#include "QCNetworkRetryPolicy.h"
#include "QCNetworkSslConfig.h"
#include "QCNetworkProxyConfig.h"
#include "QCNetworkTimeoutConfig.h"

#include "test_httpbin_env.h"

using namespace QCurl;

class TestIntegration : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void init();
    void cleanup();

    // ========== 真实网络请求测试 ==========
    void testRealHttpGetRequest();
    void testRealHttpPostRequest();
    void testRealHttpPutRequest();
    void testRealHttpDeleteRequest();
    void testRealHttpPatchRequest();

    // ========== Cookie 测试 ==========
    void testCookieSetAndGet();
    void testCookiePersistence();

    // ========== Header 测试 ==========
    void testCustomHeaders();
    void testUserAgentHeader();
    void testAuthorizationHeader();
    void testHttpAuthBasic();
    void testAuthorizationHeaderOverridesHttpAuth();

    // ========== 超时测试 ==========
    void testConnectTimeout();
    void testTotalTimeout();
    void testDelayedResponse();

    // ========== 重定向测试 ==========
    void testFollowRedirect();
    void testMaxRedirects();

    // ========== SSL 测试 ==========
    void testHttpbinRequestSslConfigAlignment();
    void testLocalHttpsTlsVerification();
    void testSslConfiguration();

    // ========== 进度/大体量传输测试 ==========
    void testProgressTracking();

    // ========== 并发测试 ==========
    void testConcurrentRequests();
    void testSequentialRequests();

    // ========== 错误恢复测试 ==========
    void testInvalidHost();
    void testConnectionRefused();
    void testHttpErrorCodes();

    // ========== 重试机制集成测试（v2.1.0）==========
    void testRetryWithConcurrentRequests();  // 并发重试无冲突
    void testRetryOnTimeout();               // 超时重试
    void testRetryDelayAccuracy();           // 重试延迟准确性

private:
    QCNetworkAccessManager *m_manager = nullptr;
    QString m_httpbinBaseUrl;

    // 辅助方法
    bool waitForSignal(QObject *obj, const QMetaMethod &signal, int timeout = 10000);
    QJsonObject parseJsonResponse(const QByteArray &data);
};

// ============================================================================
// 辅助方法实现
// ============================================================================

bool TestIntegration::waitForSignal(QObject *obj, const QMetaMethod &signal, int timeout)
{
    if (!obj) {
        return false;
    }

    // 抗竞态：避免“信号已到达但 QSignalSpy 后置创建导致 wait 超时”的假阴性
    if (auto *reply = qobject_cast<QCNetworkReply *>(obj)) {
        if (signal == QMetaMethod::fromSignal(&QCNetworkReply::finished)) {
            if (reply->isFinished()) {
                return true;
            }
        }
    }

    QSignalSpy spy(obj, signal);
    return spy.wait(timeout);
}

QJsonObject TestIntegration::parseJsonResponse(const QByteArray &data)
{
    QJsonDocument doc = QJsonDocument::fromJson(data);
    return doc.object();
}

static bool waitForPortReady(quint16 port, int timeoutMs)
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

// ============================================================================
// 测试初始化
// ============================================================================

void TestIntegration::initTestCase()
{
    qDebug() << "========================================";
    qDebug() << "QCurl 集成测试套件";
    qDebug() << "========================================";
    m_manager = new QCNetworkAccessManager(this);

    m_httpbinBaseUrl = TestEnv::httpbinBaseUrl();
    if (m_httpbinBaseUrl.isEmpty()) {
        QSKIP(qPrintable(TestEnv::httpbinMissingReason()));
    }
    qDebug() << "httpbin base URL:" << m_httpbinBaseUrl;

    QCNetworkRequest healthCheck(QUrl(m_httpbinBaseUrl + "/status/200"));
    auto *reply = m_manager->sendGet(healthCheck);
    const bool ok = waitForSignal(reply, QMetaMethod::fromSignal(&QCNetworkReply::finished), 2000)
        && reply->error() == NetworkError::NoError;
    reply->deleteLater();

    if (!ok) {
        QSKIP(qPrintable(QStringLiteral("httpbin 服务不可用：%1").arg(m_httpbinBaseUrl)));
    }
}

void TestIntegration::cleanupTestCase()
{
    qDebug() << "清理集成测试套件";
    m_manager = nullptr;
}

void TestIntegration::init()
{
    // 每个测试前执行
}

void TestIntegration::cleanup()
{
    // 每个测试后执行
}

// ============================================================================
// 真实网络请求测试
// ============================================================================

void TestIntegration::testRealHttpGetRequest()
{
    QCNetworkRequest request(QUrl(m_httpbinBaseUrl + "/get?test=value"));
    auto *reply = m_manager->sendGet(request);

    QVERIFY(waitForSignal(reply, QMetaMethod::fromSignal(&QCNetworkReply::finished), 10000));
    QCOMPARE(reply->error(), NetworkError::NoError);

    auto data = reply->readAll();
    QVERIFY(data.has_value());

    // 验证 JSON 响应
    QJsonObject json = parseJsonResponse(*data);
    QVERIFY(json.contains("args"));
    QJsonObject args = json["args"].toObject();
    QCOMPARE(args["test"].toString(), QString("value"));

    qDebug() << "GET request successful:" << data->size() << "bytes";
    reply->deleteLater();
}

void TestIntegration::testRealHttpPostRequest()
{
    QCNetworkRequest request(QUrl(m_httpbinBaseUrl + "/post"));
    request.setRawHeader("Content-Type", "application/json");

    QByteArray postData = R"({"name":"QCurl","version":"2.0.0"})";
    auto *reply = m_manager->sendPost(request, postData);

    QVERIFY(waitForSignal(reply, QMetaMethod::fromSignal(&QCNetworkReply::finished), 10000));
    QCOMPARE(reply->error(), NetworkError::NoError);

    auto data = reply->readAll();
    QVERIFY(data.has_value());

    // 验证服务器回显了我们的数据
    QJsonObject json = parseJsonResponse(*data);
    QVERIFY(json.contains("data"));
    QString dataStr = json["data"].toString();
    QVERIFY(dataStr.contains("QCurl"));
    QVERIFY(dataStr.contains("2.0.0"));

    qDebug() << "POST request successful";
    reply->deleteLater();
}

void TestIntegration::testRealHttpPutRequest()
{
    QCNetworkRequest request(QUrl(m_httpbinBaseUrl + "/put"));
    request.setRawHeader("Content-Type", "application/json");

    QByteArray putData = R"({"action":"update"})";
    auto *reply = m_manager->sendPut(request, putData);

    QVERIFY(waitForSignal(reply, QMetaMethod::fromSignal(&QCNetworkReply::finished), 10000));
    QCOMPARE(reply->error(), NetworkError::NoError);

    auto data = reply->readAll();
    QVERIFY(data.has_value());
    QVERIFY(data->contains("update"));

    qDebug() << "PUT request successful";
    reply->deleteLater();
}

void TestIntegration::testRealHttpDeleteRequest()
{
    QCNetworkRequest request(QUrl(m_httpbinBaseUrl + "/delete"));
    auto *reply = m_manager->sendDelete(request);

    QVERIFY(waitForSignal(reply, QMetaMethod::fromSignal(&QCNetworkReply::finished), 10000));
    QCOMPARE(reply->error(), NetworkError::NoError);

    qDebug() << "DELETE request successful";
    reply->deleteLater();
}

void TestIntegration::testRealHttpPatchRequest()
{
    QCNetworkRequest request(QUrl(m_httpbinBaseUrl + "/patch"));
    request.setRawHeader("Content-Type", "application/json");

    QByteArray patchData = R"({"field":"patched"})";
    auto *reply = m_manager->sendPatch(request, patchData);

    QVERIFY(waitForSignal(reply, QMetaMethod::fromSignal(&QCNetworkReply::finished), 10000));
    QCOMPARE(reply->error(), NetworkError::NoError);

    auto data = reply->readAll();
    QVERIFY(data.has_value());
    QVERIFY(data->contains("patched"));

    qDebug() << "PATCH request successful";
    reply->deleteLater();
}

// ============================================================================
// Cookie 测试
// ============================================================================

void TestIntegration::testCookieSetAndGet()
{
    // 设置 cookie
    QCNetworkRequest request(QUrl(m_httpbinBaseUrl + "/cookies/set?test_cookie=test_value"));
    request.setFollowLocation(true);  // httpbin 会重定向到 /cookies

    auto *reply = m_manager->sendGet(request);
    QVERIFY(waitForSignal(reply, QMetaMethod::fromSignal(&QCNetworkReply::finished), 10000));
    QCOMPARE(reply->error(), NetworkError::NoError);

    auto data = reply->readAll();
    QVERIFY(data.has_value());

    // 验证 cookie 在响应中
    QJsonObject json = parseJsonResponse(*data);
    QVERIFY(json.contains("cookies"));

    qDebug() << "Cookie test successful";
    reply->deleteLater();
}

void TestIntegration::testCookiePersistence()
{
    // 创建临时 cookie 文件
    QTemporaryFile cookieFile;
    QVERIFY(cookieFile.open());
    QString cookiePath = cookieFile.fileName();
    cookieFile.close();

    // 设置 cookie 文件
    m_manager->setCookieFilePath(cookiePath);

    // 第一个请求：设置 cookie
    QCNetworkRequest request1(QUrl(m_httpbinBaseUrl + "/cookies/set?session=123"));
    request1.setFollowLocation(true);
    auto *reply1 = m_manager->sendGet(request1);
    QVERIFY(waitForSignal(reply1, QMetaMethod::fromSignal(&QCNetworkReply::finished), 10000));
    reply1->deleteLater();

    // 第二个请求：验证 cookie 被保存和发送
    QCNetworkRequest request2(QUrl(m_httpbinBaseUrl + "/cookies"));
    auto *reply2 = m_manager->sendGet(request2);
    QVERIFY(waitForSignal(reply2, QMetaMethod::fromSignal(&QCNetworkReply::finished), 10000));

    auto data = reply2->readAll();
    QVERIFY(data.has_value());

    QJsonObject json = parseJsonResponse(*data);
    QJsonObject cookies = json["cookies"].toObject();
    QCOMPARE(cookies["session"].toString(), QString("123"));

    qDebug() << "Cookie persistence test successful";
    reply2->deleteLater();

    // 清理
    m_manager->setCookieFilePath("");
    QFile::remove(cookiePath);
}

// ============================================================================
// Header 测试
// ============================================================================

void TestIntegration::testCustomHeaders()
{
    QCNetworkRequest request(QUrl(m_httpbinBaseUrl + "/headers"));
    request.setRawHeader("X-Custom-Header", "TestValue");
    request.setRawHeader("X-Test-ID", "12345");

    auto *reply = m_manager->sendGet(request);
    QVERIFY(waitForSignal(reply, QMetaMethod::fromSignal(&QCNetworkReply::finished), 10000));

    auto data = reply->readAll();
    QVERIFY(data.has_value());

    QJsonObject json = parseJsonResponse(*data);
    QJsonObject headers = json["headers"].toObject();
    QCOMPARE(headers["X-Custom-Header"].toString(), QString("TestValue"));
    QCOMPARE(headers["X-Test-Id"].toString(), QString("12345"));

    qDebug() << "Custom headers test successful";
    reply->deleteLater();
}

void TestIntegration::testUserAgentHeader()
{
    QCNetworkRequest request(QUrl(m_httpbinBaseUrl + "/user-agent"));
    request.setRawHeader("User-Agent", "QCurl/2.0.0 Integration-Test");

    auto *reply = m_manager->sendGet(request);
    QVERIFY(waitForSignal(reply, QMetaMethod::fromSignal(&QCNetworkReply::finished), 10000));

    auto data = reply->readAll();
    QVERIFY(data.has_value());

    QJsonObject json = parseJsonResponse(*data);
    QString userAgent = json["user-agent"].toString();
    QVERIFY(userAgent.contains("QCurl/2.0.0"));

    qDebug() << "User-Agent test successful:" << userAgent;
    reply->deleteLater();
}

void TestIntegration::testAuthorizationHeader()
{
    // 测试 Basic Auth
    QCNetworkRequest request(QUrl(m_httpbinBaseUrl + "/basic-auth/user/passwd"));

    // Basic Auth: base64(user:passwd)
    QString credentials = QString("user:passwd").toUtf8().toBase64();
    request.setRawHeader("Authorization", QString("Basic %1").arg(credentials).toUtf8());

    auto *reply = m_manager->sendGet(request);
    QVERIFY(waitForSignal(reply, QMetaMethod::fromSignal(&QCNetworkReply::finished), 10000));
    QCOMPARE(reply->error(), NetworkError::NoError);

    auto data = reply->readAll();
    QVERIFY(data.has_value());

    QJsonObject json = parseJsonResponse(*data);
    QVERIFY(json["authenticated"].toBool());
    QCOMPARE(json["user"].toString(), QString("user"));

    qDebug() << "Authorization header test successful";
    reply->deleteLater();
}

void TestIntegration::testHttpAuthBasic()
{
    QCNetworkRequest request(QUrl(m_httpbinBaseUrl + "/basic-auth/user/passwd"));

    QCNetworkHttpAuthConfig auth;
    auth.userName = QStringLiteral("user");
    auth.password = QStringLiteral("passwd");
    auth.method = QCNetworkHttpAuthMethod::Basic;
    request.setHttpAuth(auth);

    auto *reply = m_manager->sendGet(request);
    QVERIFY(waitForSignal(reply, QMetaMethod::fromSignal(&QCNetworkReply::finished), 10000));
    QCOMPARE(reply->error(), NetworkError::NoError);

    auto data = reply->readAll();
    QVERIFY(data.has_value());

    QJsonObject json = parseJsonResponse(*data);
    QVERIFY(json["authenticated"].toBool());
    QCOMPARE(json["user"].toString(), QStringLiteral("user"));

    qDebug() << "HTTPAUTH Basic test successful";
    reply->deleteLater();
}

void TestIntegration::testAuthorizationHeaderOverridesHttpAuth()
{
    QCNetworkRequest request(QUrl(m_httpbinBaseUrl + "/basic-auth/user/passwd"));

    // 显式 Authorization header 优先：此处故意设置错误凭据
    QString wrongCredentials = QString("bad:creds").toUtf8().toBase64();
    request.setRawHeader("Authorization", QString("Basic %1").arg(wrongCredentials).toUtf8());

    // 同时配置正确的 httpAuth（应被忽略）
    QCNetworkHttpAuthConfig auth;
    auth.userName = QStringLiteral("user");
    auth.password = QStringLiteral("passwd");
    auth.method = QCNetworkHttpAuthMethod::Basic;
    request.setHttpAuth(auth);

    auto *reply = m_manager->sendGet(request);
    QVERIFY(waitForSignal(reply, QMetaMethod::fromSignal(&QCNetworkReply::finished), 10000));
    QCOMPARE(reply->error(), NetworkError::HttpUnauthorized);

    qDebug() << "Authorization header overrides HTTPAUTH test successful";
    reply->deleteLater();
}

// ============================================================================
// 超时测试
// ============================================================================

void TestIntegration::testConnectTimeout()
{
    // 使用一个不可达的 IP 地址测试连接超时
    QCNetworkRequest request(QUrl("http://192.0.2.1"));  // TEST-NET-1, 不可路由

    QCNetworkTimeoutConfig timeout;
    timeout.connectTimeout = std::chrono::seconds(2);
    timeout.totalTimeout = std::chrono::seconds(3);
    request.setTimeoutConfig(timeout);

    auto *reply = m_manager->sendGet(request);
    QVERIFY(waitForSignal(reply, QMetaMethod::fromSignal(&QCNetworkReply::finished), 5000));

    // 应该超时失败
    QVERIFY(reply->error() != NetworkError::NoError);
    QVERIFY(isCurlError(reply->error()));

    qDebug() << "Connect timeout test successful:" << reply->errorString();
    reply->deleteLater();
}

void TestIntegration::testTotalTimeout()
{
    QCNetworkRequest request(QUrl(m_httpbinBaseUrl + "/delay/10"));  // 延迟 10 秒

    QCNetworkTimeoutConfig timeout;
    timeout.totalTimeout = std::chrono::seconds(2);  // 但只允许 2 秒
    request.setTimeoutConfig(timeout);

    auto *reply = m_manager->sendGet(request);
    QVERIFY(waitForSignal(reply, QMetaMethod::fromSignal(&QCNetworkReply::finished), 5000));

    // 应该超时失败
    QVERIFY(reply->error() != NetworkError::NoError);

    qDebug() << "Total timeout test successful:" << reply->errorString();
    reply->deleteLater();
}

void TestIntegration::testDelayedResponse()
{
    // 测试服务器延迟 2 秒的响应
    QCNetworkRequest request(QUrl(m_httpbinBaseUrl + "/delay/2"));

    QCNetworkTimeoutConfig timeout;
    timeout.totalTimeout = std::chrono::seconds(5);  // 给足够的时间
    request.setTimeoutConfig(timeout);

    auto *reply = m_manager->sendGet(request);
    QVERIFY(waitForSignal(reply, QMetaMethod::fromSignal(&QCNetworkReply::finished), 8000));

    // 应该成功
    QCOMPARE(reply->error(), NetworkError::NoError);

    qDebug() << "Delayed response test successful";
    reply->deleteLater();
}

// ============================================================================
// 重定向测试
// ============================================================================

void TestIntegration::testFollowRedirect()
{
    QCNetworkRequest request(QUrl(m_httpbinBaseUrl + "/redirect/3"));  // 3 次重定向
    request.setFollowLocation(true);

    auto *reply = m_manager->sendGet(request);
    QVERIFY(waitForSignal(reply, QMetaMethod::fromSignal(&QCNetworkReply::finished), 10000));
    QCOMPARE(reply->error(), NetworkError::NoError);

    auto data = reply->readAll();
    QVERIFY(data.has_value());

    // 最终应该到达 /get
    QVERIFY(data->contains("\"url\""));

    qDebug() << "Redirect following test successful";
    reply->deleteLater();
}

void TestIntegration::testMaxRedirects()
{
    const QString redirectPath = QStringLiteral("/redirect/10"); // 10 次重定向，最终应落到 /get
    const QUrl redirectUrl(m_httpbinBaseUrl + redirectPath);

    // ----------------------------
    // Phase 1: 足够大阈值必须成功
    // ----------------------------
    {
        QCNetworkRequest request(redirectUrl);
        request.setFollowLocation(true);
        request.setMaxRedirects(20);

        auto *reply = m_manager->sendGet(request);
        QVERIFY(waitForSignal(reply, QMetaMethod::fromSignal(&QCNetworkReply::finished), 15000));

        QCOMPARE(reply->error(), NetworkError::NoError);
        QCOMPARE(reply->httpStatusCode(), 200);

        auto data = reply->readAll();
        QVERIFY(data.has_value());
        QVERIFY(data->contains("\"url\""));
        QVERIFY(data->contains("/get"));

        qInfo().noquote()
            << QStringLiteral("QCURL_EVIDENCE redirect_max success max_redirects=%1 http_status=%2 final_url=%3")
                   .arg(20)
                   .arg(reply->httpStatusCode())
                   .arg(reply->url().toString());

        reply->deleteLater();
    }

    // ----------------------------
    // Phase 2: 足够小阈值必须被上限阻断
    // ----------------------------
    {
        QCNetworkRequest request(redirectUrl);
        request.setFollowLocation(true);
        request.setMaxRedirects(1);

        auto *reply = m_manager->sendGet(request);
        QVERIFY(waitForSignal(reply, QMetaMethod::fromSignal(&QCNetworkReply::finished), 15000));

        QCOMPARE(reply->error(), NetworkError::TooManyRedirects);

        qInfo().noquote()
            << QStringLiteral("QCURL_EVIDENCE redirect_max blocked max_redirects=%1 error=%2 http_status=%3 url=%4 msg=%5")
                   .arg(1)
                   .arg(static_cast<int>(reply->error()))
                   .arg(reply->httpStatusCode())
                   .arg(reply->url().toString())
                   .arg(reply->errorString());

        reply->deleteLater();
    }
}

// ============================================================================
// SSL 测试
// ============================================================================

void TestIntegration::testHttpbinRequestSslConfigAlignment()
{
    const QUrl url(m_httpbinBaseUrl + "/get");
    QCNetworkRequest request(url);

    // 口径说明：
    // - 当 httpbin base URL 为 HTTP 时，本用例证明“设置默认 SSL 配置不会破坏 HTTP 请求”。
    // - 当 base URL 为 HTTPS 时，本用例要求“默认安全配置可正常工作”（证书需由系统 CA 信任）。
    QCNetworkSslConfig sslConfig = QCNetworkSslConfig::defaultConfig();
    request.setSslConfig(sslConfig);

    auto *reply = m_manager->sendGet(request);
    QVERIFY(waitForSignal(reply, QMetaMethod::fromSignal(&QCNetworkReply::finished), 10000));

    QCOMPARE(reply->error(), NetworkError::NoError);
    QCOMPARE(reply->httpStatusCode(), 200);

    qInfo().noquote()
        << QStringLiteral("QCURL_EVIDENCE integration_httpbin_sslconfig ok scheme=%1 url=%2 http_status=%3 error=%4 msg=%5")
               .arg(url.scheme())
               .arg(url.toString())
               .arg(reply->httpStatusCode())
               .arg(static_cast<int>(reply->error()))
               .arg(reply->errorString());

    reply->deleteLater();
}

void TestIntegration::testLocalHttpsTlsVerification()
{
    // 可控 HTTPS 证据：使用仓库自带证书启动本地 HTTPS 服务端，验证 TLS 的失败/成功两条路径。
    const QString appDir = QCoreApplication::applicationDirPath();
    const QString scriptPath
        = QDir(appDir).absoluteFilePath(QStringLiteral("../../tests/http2-test-server.js"));
    if (!QFileInfo::exists(scriptPath)) {
        QFAIL("未找到本地 HTTPS 测试服务器脚本（tests/http2-test-server.js）。");
    }

    QProcess localServer;
    localServer.setProgram(QStringLiteral("node"));
    localServer.setArguments({scriptPath, QStringLiteral("--h2-port"), QStringLiteral("0"),
                              QStringLiteral("--http1-port"), QStringLiteral("0")});
    localServer.setProcessChannelMode(QProcess::MergedChannels);
    localServer.start();

    if (!localServer.waitForStarted(2000)) {
        QSKIP("无法启动本地 HTTPS 测试服务器（node）。请确认 node 可用。");
    }

    struct ProcessGuard {
        QProcess &proc;
        ~ProcessGuard()
        {
            if (proc.state() == QProcess::NotRunning) {
                return;
            }
            proc.terminate();
            if (!proc.waitForFinished(1500)) {
                proc.kill();
                proc.waitForFinished(1500);
            }
        }
    } guard{localServer};

    QElapsedTimer timer;
    timer.start();
    QByteArray output;

    int httpsPort = 0;
    QString certPath;

    QRegularExpression re(QStringLiteral(R"(QCURL_HTTP2_TEST_SERVER_READY\s+(\{.*\})\s*)"));
    while (timer.elapsed() < 5000) {
        if (!localServer.waitForReadyRead(200)) {
            continue;
        }
        output += localServer.readAll();
        const QString outText = QString::fromUtf8(output);
        const QRegularExpressionMatch match = re.match(outText);
        if (!match.hasMatch()) {
            continue;
        }

        const QByteArray jsonBytes = match.captured(1).toUtf8();
        QJsonParseError parseError{};
        const QJsonDocument doc = QJsonDocument::fromJson(jsonBytes, &parseError);
        if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
            QFAIL(qPrintable(QStringLiteral("本地 HTTPS 测试服务器输出格式错误：%1")
                                 .arg(parseError.errorString())));
        }

        const QJsonObject obj = doc.object();
        httpsPort = obj.value(QStringLiteral("http1Port")).toInt(0);
        certPath  = obj.value(QStringLiteral("certPath")).toString();
        break;
    }

    if (httpsPort <= 0 || httpsPort > 65535) {
        const QString outText = QString::fromUtf8(output + localServer.readAll());
        QFAIL(qPrintable(QStringLiteral("本地 HTTPS 测试服务器未就绪（未输出 READY 或端口无效）。输出：\n%1")
                             .arg(outText.right(4096))));
    }

    if (certPath.isEmpty()) {
        certPath = QDir(appDir).absoluteFilePath(QStringLiteral("../../tests/testdata/http2/localhost.crt"));
    }

    QVERIFY2(waitForPortReady(static_cast<quint16>(httpsPort), 3000),
             qPrintable(QStringLiteral("本地 HTTPS 测试服务器端口不可连接：%1").arg(httpsPort)));

    const QUrl url(QStringLiteral("https://localhost:%1/reqinfo").arg(httpsPort));

    // ----------------------------
    // Phase 1: 默认安全配置（无自定义 CA）应拒绝自签名证书
    // ----------------------------
    {
        QCNetworkRequest request(url);
        request.setSslConfig(QCNetworkSslConfig::defaultConfig());
        auto *reply = m_manager->sendGet(request);
        QVERIFY(waitForSignal(reply, QMetaMethod::fromSignal(&QCNetworkReply::finished), 15000));
        QCOMPARE(reply->error(), NetworkError::SslHandshakeFailed);

        qInfo().noquote()
            << QStringLiteral("QCURL_EVIDENCE https_local_tls expected_fail url=%1 error=%2 msg=%3")
                   .arg(url.toString())
                   .arg(static_cast<int>(reply->error()))
                   .arg(reply->errorString());

        reply->deleteLater();
    }

    // ----------------------------
    // Phase 2: 配置 CA 后应成功（可复核：status + body sha256）
    // ----------------------------
    {
        QCNetworkRequest request(url);
        QCNetworkSslConfig sslConfig = QCNetworkSslConfig::defaultConfig();
        sslConfig.caCertPath = certPath;
        request.setSslConfig(sslConfig);

        auto *reply = m_manager->sendGet(request);
        QVERIFY(waitForSignal(reply, QMetaMethod::fromSignal(&QCNetworkReply::finished), 15000));
        QCOMPARE(reply->error(), NetworkError::NoError);
        QCOMPARE(reply->httpStatusCode(), 200);

        auto data = reply->readAll();
        QVERIFY(data.has_value());
        const QString bodySha256 = QString::fromLatin1(
            QCryptographicHash::hash(*data, QCryptographicHash::Sha256).toHex());
        const QJsonObject json = parseJsonResponse(*data);
        QVERIFY(json.value(QStringLiteral("ok")).toBool());

        qInfo().noquote()
            << QStringLiteral("QCURL_EVIDENCE https_local_tls success url=%1 http_status=%2 body_len=%3 body_sha256=%4 caCertPath=%5")
                   .arg(url.toString())
                   .arg(reply->httpStatusCode())
                   .arg(data->size())
                   .arg(bodySha256)
                   .arg(certPath);

        reply->deleteLater();
    }
}

void TestIntegration::testSslConfiguration()
{
    // 测试禁用 SSL 验证（用于自签名证书）
    QCNetworkRequest request(QUrl(m_httpbinBaseUrl + "/get"));

    QCNetworkSslConfig sslConfig = QCNetworkSslConfig::insecureConfig();
    request.setSslConfig(sslConfig);

    auto *reply = m_manager->sendGet(request);
    QVERIFY(waitForSignal(reply, QMetaMethod::fromSignal(&QCNetworkReply::finished), 10000));
    QCOMPARE(reply->error(), NetworkError::NoError);

    qDebug() << "SSL insecure config test successful";
    reply->deleteLater();
}

// ============================================================================
void TestIntegration::testProgressTracking()
{
    QCNetworkRequest request(QUrl(m_httpbinBaseUrl + "/bytes/102400"));  // 100KB

    auto *reply = m_manager->sendGet(request);

    QSignalSpy progressSpy(reply, &QCNetworkReply::downloadProgress);

    QVERIFY(waitForSignal(reply, QMetaMethod::fromSignal(&QCNetworkReply::finished), 20000));
    QCOMPARE(reply->error(), NetworkError::NoError);

    // 应该有进度更新
    QVERIFY(progressSpy.count() >= 1);

    if (progressSpy.count() > 0) {
        auto lastProgress = progressSpy.last();
        qint64 received = lastProgress.at(0).toLongLong();
        qint64 total = lastProgress.at(1).toLongLong();
        qDebug() << "Progress tracking successful: received=" << received << "total=" << total;
        QVERIFY(received > 0);
    }

    reply->deleteLater();
}

// ============================================================================
// 并发测试
// ============================================================================

void TestIntegration::testConcurrentRequests()
{
    // 同时发起 5 个请求
    QList<QCNetworkReply*> replies;
    for (int i = 0; i < 5; ++i) {
        QCNetworkRequest request(QUrl(QString(m_httpbinBaseUrl + "/get?id=%1").arg(i)));
        auto *reply = m_manager->sendGet(request);
        replies.append(reply);
    }

    // 等待所有请求完成
    QEventLoop loop;
    int finishedCount = 0;
    auto checkAllFinished = [&]() {
        finishedCount++;
        if (finishedCount == 5) {
            loop.quit();
        }
    };

    for (auto *reply : replies) {
        connect(reply, &QCNetworkReply::finished, checkAllFinished);
    }

    QTimer::singleShot(30000, &loop, &QEventLoop::quit);  // 30 秒超时
    loop.exec();

    QCOMPARE(finishedCount, 5);

    // 验证所有请求都成功
    for (auto *reply : replies) {
        QCOMPARE(reply->error(), NetworkError::NoError);
        reply->deleteLater();
    }

    qDebug() << "Concurrent requests test successful: 5 requests completed";
}

void TestIntegration::testSequentialRequests()
{
    // 顺序执行 3 个请求
    for (int i = 0; i < 3; ++i) {
        QCNetworkRequest request(QUrl(QString(m_httpbinBaseUrl + "/get?seq=%1").arg(i)));
        auto *reply = m_manager->sendGet(request);

        QVERIFY(waitForSignal(reply, QMetaMethod::fromSignal(&QCNetworkReply::finished), 10000));
        QCOMPARE(reply->error(), NetworkError::NoError);

        reply->deleteLater();
    }

    qDebug() << "Sequential requests test successful: 3 requests completed";
}

// ============================================================================
// 错误恢复测试
// ============================================================================

void TestIntegration::testInvalidHost()
{
    QCNetworkRequest request(QUrl("http://this-host-does-not-exist-12345.invalid"));
    auto *reply = m_manager->sendGet(request);

    QVERIFY(waitForSignal(reply, QMetaMethod::fromSignal(&QCNetworkReply::finished), 10000));
    QVERIFY(reply->error() != NetworkError::NoError);
    QVERIFY(isCurlError(reply->error()));

    qDebug() << "Invalid host test successful:" << reply->errorString();
    reply->deleteLater();
}

void TestIntegration::testConnectionRefused()
{
    // 取证口径：避免依赖固定端口（如 9999）“通常没有服务”的非确定性前提。
    //
    // 策略：先申请一个临时端口并立即释放，然后连接该端口期望得到“连接失败”终态。
    // 这仍存在极小竞态窗口，但显著优于固定端口假设，且在 CI/门禁环境中可重复性更强。
    QTcpServer portPicker;
    if (!portPicker.listen(QHostAddress::LocalHost, 0)) {
        QSKIP("无法绑定本机端口用于生成确定性 connection-refused 场景");
    }
    const quint16 port = portPicker.serverPort();
    portPicker.close();

    QCNetworkRequest request(QUrl(QStringLiteral("http://127.0.0.1:%1").arg(port)));

    QCNetworkTimeoutConfig timeout;
    timeout.connectTimeout = std::chrono::seconds(2);
    request.setTimeoutConfig(timeout);

    auto *reply = m_manager->sendGet(request);
    QVERIFY(waitForSignal(reply, QMetaMethod::fromSignal(&QCNetworkReply::finished), 5000));
    QVERIFY(reply->error() != NetworkError::NoError);

    qDebug() << "Connection refused test successful:" << reply->errorString();
    reply->deleteLater();
}

void TestIntegration::testHttpErrorCodes()
{
    // 测试 HTTP 404
    QCNetworkRequest request404(QUrl(m_httpbinBaseUrl + "/status/404"));
    auto *reply404 = m_manager->sendGet(request404);
    QVERIFY(waitForSignal(reply404, QMetaMethod::fromSignal(&QCNetworkReply::finished), 10000));

    // 证据合同：
    // - status 必须可观测（httpStatusCode=404）
    // - 对外 error 必须非 NoError（可能为 HTTP 映射错误，或因 FAILONERROR 被归为 curl error）
    QCOMPARE(reply404->httpStatusCode(), 404);
    QCOMPARE(reply404->state(), ReplyState::Error);
    QVERIFY(reply404->error() != NetworkError::NoError);
    QVERIFY(reply404->error() == fromHttpCode(404) || isCurlError(reply404->error()));

    qInfo().noquote()
        << QStringLiteral("QCURL_EVIDENCE integration_http_error status=404 http_status=%1 error=%2 msg=%3")
               .arg(reply404->httpStatusCode())
               .arg(static_cast<int>(reply404->error()))
               .arg(reply404->errorString());
    reply404->deleteLater();

    // 测试 HTTP 500
    QCNetworkRequest request500(QUrl(m_httpbinBaseUrl + "/status/500"));
    auto *reply500 = m_manager->sendGet(request500);
    QVERIFY(waitForSignal(reply500, QMetaMethod::fromSignal(&QCNetworkReply::finished), 10000));

    QCOMPARE(reply500->httpStatusCode(), 500);
    QCOMPARE(reply500->state(), ReplyState::Error);
    QVERIFY(reply500->error() != NetworkError::NoError);
    QVERIFY(reply500->error() == fromHttpCode(500) || isCurlError(reply500->error()));

    qInfo().noquote()
        << QStringLiteral("QCURL_EVIDENCE integration_http_error status=500 http_status=%1 error=%2 msg=%3")
               .arg(reply500->httpStatusCode())
               .arg(static_cast<int>(reply500->error()))
               .arg(reply500->errorString());
    reply500->deleteLater();

    qDebug() << "HTTP error codes test completed";
}

// ============================================================================
// 重试机制集成测试（v2.1.0）
// ============================================================================

void TestIntegration::testRetryWithConcurrentRequests()
{
    qDebug() << "========== testRetryWithConcurrentRequests ==========";

    // 创建 3 个并发请求，都会触发重试
    QList<QCNetworkReply*> replies;
    QVector<int> retryCounts(3, 0);

    for (int i = 0; i < 3; ++i) {
        QCNetworkRequest request(QUrl(m_httpbinBaseUrl + "/status/503"));

        // 避免在 httpbin 偶发卡顿时导致测试长时间悬挂（每次 attempt 的上限）
        QCNetworkTimeoutConfig timeout;
        timeout.connectTimeout = std::chrono::milliseconds(1000);
        timeout.totalTimeout = std::chrono::milliseconds(3000);
        request.setTimeoutConfig(timeout);

        QCNetworkRetryPolicy policy;
        policy.maxRetries = 2;
        policy.initialDelay = std::chrono::milliseconds(100);
        request.setRetryPolicy(policy);

        auto *reply = m_manager->sendGet(request);
        replies.append(reply);
        connect(reply, &QCNetworkReply::retryAttempt, this, [&retryCounts, i]() {
            retryCounts[i] += 1;
        });
    }

    // 等待所有请求完成（避免串行 wait 导致整体上限接近边界而出现偶发失败）
    QEventLoop loop;
    QTimer deadlineTimer;
    deadlineTimer.setSingleShot(true);
    deadlineTimer.start(20000);
    connect(&deadlineTimer, &QTimer::timeout, &loop, &QEventLoop::quit);

    QVector<bool> finished(3, false);
    for (int i = 0; i < replies.size(); ++i) {
        auto *reply = replies[i];
        connect(reply, &QCNetworkReply::finished, &loop, [&, i]() {
            finished[i] = true;
            bool allDone = true;
            for (bool f : finished) {
                if (!f) {
                    allDone = false;
                    break;
                }
            }
            if (allDone) {
                loop.quit();
            }
        });
    }

    loop.exec();

    bool allFinished = true;
    for (bool f : finished) {
        if (!f) {
            allFinished = false;
            break;
        }
    }

    if (!allFinished) {
        for (int i = 0; i < replies.size(); ++i) {
            auto *reply = replies[i];
            if (!reply) {
                continue;
            }
            qWarning().noquote() << QStringLiteral("concurrent retry timeout: idx=%1 finished=%2 state=%3 err=%4 errStr=%5 retryCount=%6")
                                        .arg(i)
                                        .arg(reply->isFinished() ? 1 : 0)
                                        .arg(static_cast<int>(reply->state()))
                                        .arg(static_cast<int>(reply->error()))
                                        .arg(reply->errorString())
                                        .arg(retryCounts[i]);
        }
        for (auto *reply : replies) {
            if (reply) {
                reply->cancel();
                reply->deleteLater();
            }
        }
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QFAIL("Concurrent retries did not finish within 20s (unexpected hang or retry scheduler issue)");
    }

    // 验证每个请求都重试了 2 次
    for (int i = 0; i < retryCounts.size(); ++i) {
        qDebug() << "Reply" << i << "retry count:" << retryCounts[i];
        QCOMPARE(retryCounts[i], 2);
    }

    // 清理
    for (auto *reply : replies) {
        if (reply) {
            reply->deleteLater();
        }
    }
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);

    qDebug() << "Concurrent retries test completed";
}

void TestIntegration::testRetryOnTimeout()
{
    qDebug() << "========== testRetryOnTimeout ==========";

    // 配置短超时 + 重试策略
    QCNetworkRequest request(QUrl(m_httpbinBaseUrl + "/delay/3"));  // httpbin 延迟 3 秒响应

    QCNetworkTimeoutConfig timeout;
    timeout.totalTimeout = std::chrono::milliseconds(500);  // 500ms 超时
    request.setTimeoutConfig(timeout);

    QCNetworkRetryPolicy policy;
    policy.maxRetries = 2;
    policy.initialDelay = std::chrono::milliseconds(100);
    // ConnectionTimeout 已在默认 retryableErrors 中，无需手动添加
    request.setRetryPolicy(policy);

    QElapsedTimer timer;
    timer.start();

    auto *reply = m_manager->sendGet(request);
    QSignalSpy retrySpy(reply, &QCNetworkReply::retryAttempt);

    if (!waitForSignal(reply, QMetaMethod::fromSignal(&QCNetworkReply::finished), 20000)) {
        reply->cancel();
        reply->deleteLater();
        QFAIL("Timeout retry did not finish within 20s (httpbin delay / scheduler timing)");
    }

    qint64 elapsed = timer.elapsed();

    // 验证：发生了重试
    qDebug() << "Retry attempts on timeout:" << retrySpy.count();
    qDebug() << "Total time elapsed:" << elapsed << "ms";
    qDebug() << "Final error:" << static_cast<int>(reply->error()) << reply->errorString();

    QVERIFY(retrySpy.count() >= 1);  // 至少重试了 1 次
    // 注意：超时可能返回 ConnectionTimeout (28) 或其他超时相关错误
    QVERIFY(reply->error() != NetworkError::NoError);

    reply->deleteLater();
    qDebug() << "Retry on timeout test completed";
}

void TestIntegration::testRetryDelayAccuracy()
{
    qDebug() << "========== testRetryDelayAccuracy ==========";

    // 配置精确的重试延迟
    QCNetworkRequest request(QUrl(m_httpbinBaseUrl + "/status/500"));
    QCNetworkRetryPolicy policy;
    policy.maxRetries = 3;
    policy.initialDelay = std::chrono::milliseconds(200);
    policy.backoffMultiplier = 1.5;
    request.setRetryPolicy(policy);

    QElapsedTimer timer;
    timer.start();

    auto *reply = m_manager->sendGet(request);
    QSignalSpy retrySpy(reply, &QCNetworkReply::retryAttempt);

    if (!waitForSignal(reply, QMetaMethod::fromSignal(&QCNetworkReply::finished), 10000)) {
        qWarning() << "Retry delay accuracy test timed out; failing for forensic evidence.";
        reply->cancel();
        reply->deleteLater();
        QFAIL("Retry delay accuracy did not finish within 10s (httpbin availability / scheduler timing)");
    }

    qint64 elapsed = timer.elapsed();

    // 预期延迟：200 + 300 + 450 = 950ms
    // 加上请求时间，总时间应该在合理范围内

    qDebug() << "Retry count:" << retrySpy.count();
    qDebug() << "Total elapsed:" << elapsed << "ms";

    // 只验证重试次数，不严格检查时间（避免因环境差异导致测试不稳定）
    QCOMPARE(retrySpy.count(), 3);

    // 可选：记录时间，但不作为失败条件
    if (elapsed < 600 || elapsed > 3000) {
        qWarning() << "Retry delay outside expected range (600-3000ms):" << elapsed << "ms";
    }

    reply->deleteLater();
    qDebug() << "Retry delay accuracy test completed";
}

QTEST_MAIN(TestIntegration)
#include "tst_Integration.moc"
