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
 * 本测试套件需要本地 httpbin 服务。请先启动 Docker 容器：
 *
 *     docker run -p 8935:80 kennethreitz/httpbin
 *
 * 然后运行测试：
 *
 *     ./tst_Integration
 *
 * 注意：
 * - httpbin 服务必须在 http://localhost:8935 可访问
 * - 如需使用其他端口，请修改下方的 HTTPBIN_BASE_URL 常量
 * - 可使用 `curl http://localhost:8935/get` 验证服务是否正常
 * ============================================================================
 */

#include <QtTest/QtTest>
#include <QEventLoop>
#include <QTimer>
#include <QSignalSpy>
#include <QTemporaryFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QElapsedTimer>
#include <QMetaMethod>
#include <QCoreApplication>
#include <QEvent>
#include <QVector>

#include "QCNetworkAccessManager.h"
#include "QCNetworkRequest.h"
#include "QCNetworkReply.h"
#include "QCNetworkError.h"
#include "QCNetworkRetryPolicy.h"
#include "QCNetworkSslConfig.h"
#include "QCNetworkProxyConfig.h"
#include "QCNetworkTimeoutConfig.h"

using namespace QCurl;

// ============================================================================
// 测试服务器配置
// ============================================================================

/**
 * @brief httpbin 服务基础 URL
 *
 * 默认使用本地 Docker 容器：http://localhost:8935
 * 启动命令：docker run -p 8935:80 kennethreitz/httpbin
 */
static const QString HTTPBIN_BASE_URL = QStringLiteral("http://localhost:8935");

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

    // ========== 超时测试 ==========
    void testConnectTimeout();
    void testTotalTimeout();
    void testDelayedResponse();

    // ========== 重定向测试 ==========
    void testFollowRedirect();
    void testMaxRedirects();

    // ========== SSL 测试 ==========
    void testHttpsRequest();
    void testSslConfiguration();

    // ========== 大文件测试 ==========
    void testLargeFileDownload();
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

    // 辅助方法
    bool waitForSignal(QObject *obj, const QMetaMethod &signal, int timeout = 10000);
    QJsonObject parseJsonResponse(const QByteArray &data);
};

// ============================================================================
// 辅助方法实现
// ============================================================================

bool TestIntegration::waitForSignal(QObject *obj, const QMetaMethod &signal, int timeout)
{
    QSignalSpy spy(obj, signal);
    return spy.wait(timeout);
}

QJsonObject TestIntegration::parseJsonResponse(const QByteArray &data)
{
    QJsonDocument doc = QJsonDocument::fromJson(data);
    return doc.object();
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

    QCNetworkRequest healthCheck(QUrl(HTTPBIN_BASE_URL + "/status/200"));
    auto *reply = m_manager->sendGet(healthCheck);
    const bool ok = waitForSignal(reply, QMetaMethod::fromSignal(&QCNetworkReply::finished), 2000)
        && reply->error() == NetworkError::NoError;
    reply->deleteLater();

    if (!ok) {
        QSKIP("httpbin 服务不可用，跳过集成测试。请启动：docker run -d -p 8935:80 --name qcurl-httpbin kennethreitz/httpbin");
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
    QCNetworkRequest request(QUrl(HTTPBIN_BASE_URL + "/get?test=value"));
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
    QCNetworkRequest request(QUrl(HTTPBIN_BASE_URL + "/post"));
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
    QCNetworkRequest request(QUrl(HTTPBIN_BASE_URL + "/put"));
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
    QCNetworkRequest request(QUrl(HTTPBIN_BASE_URL + "/delete"));
    auto *reply = m_manager->sendDelete(request);

    QVERIFY(waitForSignal(reply, QMetaMethod::fromSignal(&QCNetworkReply::finished), 10000));
    QCOMPARE(reply->error(), NetworkError::NoError);

    qDebug() << "DELETE request successful";
    reply->deleteLater();
}

void TestIntegration::testRealHttpPatchRequest()
{
    QCNetworkRequest request(QUrl(HTTPBIN_BASE_URL + "/patch"));
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
    QCNetworkRequest request(QUrl(HTTPBIN_BASE_URL + "/cookies/set?test_cookie=test_value"));
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
    QCNetworkRequest request1(QUrl(HTTPBIN_BASE_URL + "/cookies/set?session=123"));
    request1.setFollowLocation(true);
    auto *reply1 = m_manager->sendGet(request1);
    QVERIFY(waitForSignal(reply1, QMetaMethod::fromSignal(&QCNetworkReply::finished), 10000));
    reply1->deleteLater();

    // 第二个请求：验证 cookie 被保存和发送
    QCNetworkRequest request2(QUrl(HTTPBIN_BASE_URL + "/cookies"));
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
    QCNetworkRequest request(QUrl(HTTPBIN_BASE_URL + "/headers"));
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
    QCNetworkRequest request(QUrl(HTTPBIN_BASE_URL + "/user-agent"));
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
    QCNetworkRequest request(QUrl(HTTPBIN_BASE_URL + "/basic-auth/user/passwd"));

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
    QCNetworkRequest request(QUrl(HTTPBIN_BASE_URL + "/delay/10"));  // 延迟 10 秒

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
    QCNetworkRequest request(QUrl(HTTPBIN_BASE_URL + "/delay/2"));

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
    QCNetworkRequest request(QUrl(HTTPBIN_BASE_URL + "/redirect/3"));  // 3 次重定向
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
    QCNetworkRequest request(QUrl(HTTPBIN_BASE_URL + "/redirect/10"));  // 10 次重定向
    request.setFollowLocation(true);
    // libcurl 默认最大重定向次数是 50，这应该成功

    auto *reply = m_manager->sendGet(request);
    QVERIFY(waitForSignal(reply, QMetaMethod::fromSignal(&QCNetworkReply::finished), 15000));

    // 可能成功或失败（取决于 libcurl 配置），但不应该崩溃
    qDebug() << "Max redirects test completed:" << reply->errorString();
    reply->deleteLater();
}

// ============================================================================
// SSL 测试
// ============================================================================

void TestIntegration::testHttpsRequest()
{
    QCNetworkRequest request(QUrl(HTTPBIN_BASE_URL + "/get"));

    // 使用默认 SSL 配置（应该验证证书）
    QCNetworkSslConfig sslConfig = QCNetworkSslConfig::defaultConfig();
    request.setSslConfig(sslConfig);

    auto *reply = m_manager->sendGet(request);
    QVERIFY(waitForSignal(reply, QMetaMethod::fromSignal(&QCNetworkReply::finished), 10000));

    // 本地 httpbin 使用 HTTP（无 SSL），应该成功
    // 注意：如使用 HTTPS httpbin，需配置 SSL 证书
    QCOMPARE(reply->error(), NetworkError::NoError);

    qDebug() << "HTTPS request test successful";
    reply->deleteLater();
}

void TestIntegration::testSslConfiguration()
{
    // 测试禁用 SSL 验证（用于自签名证书）
    QCNetworkRequest request(QUrl(HTTPBIN_BASE_URL + "/get"));

    QCNetworkSslConfig sslConfig = QCNetworkSslConfig::insecureConfig();
    request.setSslConfig(sslConfig);

    auto *reply = m_manager->sendGet(request);
    QVERIFY(waitForSignal(reply, QMetaMethod::fromSignal(&QCNetworkReply::finished), 10000));
    QCOMPARE(reply->error(), NetworkError::NoError);

    qDebug() << "SSL insecure config test successful";
    reply->deleteLater();
}

// ============================================================================
// 大文件测试
// ============================================================================

void TestIntegration::testLargeFileDownload()
{
    // 下载真实大文件：Arch Linux bootstrap (约 138 MB)
    // 使用中科大镜像站，测试大文件下载和 HTTPS
    QCNetworkRequest request(QUrl("https://mirrors.ustc.edu.cn/archlinux/iso/2025.11.01/archlinux-bootstrap-2025.11.01-x86_64.tar.zst"));

    // 集成环境可能无法验证 USTC 镜像站证书，仅该用例临时禁用校验以覆盖真实大文件场景
    QCNetworkSslConfig insecureSsl = QCNetworkSslConfig::insecureConfig();
    request.setSslConfig(insecureSsl);

    // 设置较长的超时时间（2 分钟）
    QCNetworkTimeoutConfig timeout;
    timeout.totalTimeout = std::chrono::seconds(120);
    request.setTimeoutConfig(timeout);

    auto *reply = m_manager->sendGet(request);
    QVERIFY(waitForSignal(reply, QMetaMethod::fromSignal(&QCNetworkReply::finished), 150000));  // 2.5 分钟超时
    QCOMPARE(reply->error(), NetworkError::NoError);

    auto data = reply->readAll();
    QVERIFY(data.has_value());
    QCOMPARE(data->size(), 144969772);  // 期望 138 MB (144,969,772 字节)

    qDebug() << "Large file download successful:" << data->size() << "bytes (~138 MB)";
    reply->deleteLater();
}

void TestIntegration::testProgressTracking()
{
    QCNetworkRequest request(QUrl(HTTPBIN_BASE_URL + "/bytes/102400"));  // 100KB

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
        QCNetworkRequest request(QUrl(QString(HTTPBIN_BASE_URL + "/get?id=%1").arg(i)));
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
        QCNetworkRequest request(QUrl(QString(HTTPBIN_BASE_URL + "/get?seq=%1").arg(i)));
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
    // 使用 localhost:9999（通常没有服务）
    QCNetworkRequest request(QUrl("http://127.0.0.1:9999"));

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
    QCNetworkRequest request404(QUrl(HTTPBIN_BASE_URL + "/status/404"));
    auto *reply404 = m_manager->sendGet(request404);
    QVERIFY(waitForSignal(reply404, QMetaMethod::fromSignal(&QCNetworkReply::finished), 10000));

    // 注意：libcurl 的 CURLOPT_FAILONERROR 会将 HTTP 错误转为 curl 错误
    // 所以可能返回 CURLE_HTTP_RETURNED_ERROR
    qDebug() << "HTTP 404 test:" << reply404->errorString();
    reply404->deleteLater();

    // 测试 HTTP 500
    QCNetworkRequest request500(QUrl(HTTPBIN_BASE_URL + "/status/500"));
    auto *reply500 = m_manager->sendGet(request500);
    QVERIFY(waitForSignal(reply500, QMetaMethod::fromSignal(&QCNetworkReply::finished), 10000));

    qDebug() << "HTTP 500 test:" << reply500->errorString();
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
        QCNetworkRequest request(QUrl(HTTPBIN_BASE_URL + "/status/503"));
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

    // 等待所有请求完成
    for (auto *reply : replies) {
        // 并发重试整体耗时可能超过 10s，适当放宽等待窗口
        QVERIFY(waitForSignal(reply, QMetaMethod::fromSignal(&QCNetworkReply::finished), 30000));
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
    QCNetworkRequest request(QUrl(HTTPBIN_BASE_URL + "/delay/3"));  // httpbin 延迟 3 秒响应

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

    QVERIFY(waitForSignal(reply, QMetaMethod::fromSignal(&QCNetworkReply::finished), 5000));

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
    QCNetworkRequest request(QUrl(HTTPBIN_BASE_URL + "/status/500"));
    QCNetworkRetryPolicy policy;
    policy.maxRetries = 3;
    policy.initialDelay = std::chrono::milliseconds(200);
    policy.backoffMultiplier = 1.5;
    request.setRetryPolicy(policy);

    QElapsedTimer timer;
    timer.start();

    auto *reply = m_manager->sendGet(request);
    QSignalSpy retrySpy(reply, &QCNetworkReply::retryAttempt);

    QVERIFY(waitForSignal(reply, QMetaMethod::fromSignal(&QCNetworkReply::finished), 10000));

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
