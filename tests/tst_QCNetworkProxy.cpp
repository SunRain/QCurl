/**
 * @file tst_QCNetworkProxy.cpp
 * @brief 代理配置功能测试
 * 
 * 简化测试方案：
 * - 无需真实代理服务器（使用无效代理地址）
 * - 重点测试配置 API 和错误处理
 * - 验证代理配置是否正确传递给 libcurl
 * 
 */

#include <QtTest>
#include <QSignalSpy>
#include "QCNetworkAccessManager.h"
#include "QCNetworkRequest.h"
#include "QCNetworkReply.h"
#include "QCNetworkReply_p.h"
#include "QCNetworkProxyConfig.h"
#include "QCNetworkSslConfig.h"
#include "QCNetworkTimeoutConfig.h"
#include "QCNetworkError.h"

using namespace QCurl;

class TestQCNetworkProxy : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void init();
    void cleanup();

    // 代理配置 API 测试
    void testProxyConfigBasic();
    void testProxyTypeValidation();
    void testProxyAuthentication();
    void testProxyWithSsl();
    void testProxyAppliedToCurlHandle();
    void testSslConfigApplied();

    // 代理错误处理测试
    void testProxyConnectionFailed();

private:
    QCNetworkAccessManager *m_manager = nullptr;
    bool waitForReply(QCNetworkReply *reply, int timeout = 5000);
};

void TestQCNetworkProxy::initTestCase()
{
    qDebug() << "========================================";
    qDebug() << "代理配置功能测试";
    qDebug() << "========================================";
    qDebug() << "测试方案：简化版（无需真实代理服务器）";
    qDebug() << "";

    m_manager = new QCNetworkAccessManager(this);
}

void TestQCNetworkProxy::cleanupTestCase()
{
    m_manager = nullptr;
}

void TestQCNetworkProxy::init()
{
    // 每个测试前的准备
}

void TestQCNetworkProxy::cleanup()
{
    // 每个测试后的清理
}

bool TestQCNetworkProxy::waitForReply(QCNetworkReply *reply, int timeout)
{
    if (!reply) {
        return false;
    }

    QSignalSpy spy(reply, &QCNetworkReply::finished);
    return spy.wait(timeout);
}

void TestQCNetworkProxy::testProxyConfigBasic()
{
    qDebug() << "测试 1：代理配置 API 验证";

    // 创建代理配置
    QCNetworkProxyConfig proxyConfig;
    proxyConfig.type = QCNetworkProxyConfig::ProxyType::Http;
    proxyConfig.hostName = "proxy.example.com";
    proxyConfig.port = 8080;

    // 创建请求并设置代理
    QCNetworkRequest request(QUrl("http://httpbin.org/get"));
    request.setProxyConfig(proxyConfig);

    // 验证代理配置已设置
    QVERIFY(request.proxyConfig().has_value());
    
    auto proxyConfigOpt = request.proxyConfig();
    if (proxyConfigOpt) {
        QCOMPARE(proxyConfigOpt->type, QCNetworkProxyConfig::ProxyType::Http);
        QCOMPARE(proxyConfigOpt->hostName, QString("proxy.example.com"));
        QCOMPARE(proxyConfigOpt->port, quint16(8080));
    }

    qDebug() << "✅ 代理配置 API 验证通过";
}

void TestQCNetworkProxy::testProxyTypeValidation()
{
    qDebug() << "测试 2：代理类型验证";

    // 测试所有支持的代理类型
    QList<QCNetworkProxyConfig::ProxyType> types = {
        QCNetworkProxyConfig::ProxyType::None,
        QCNetworkProxyConfig::ProxyType::Http,
        QCNetworkProxyConfig::ProxyType::Https,
        QCNetworkProxyConfig::ProxyType::Socks4,
        QCNetworkProxyConfig::ProxyType::Socks4A,
        QCNetworkProxyConfig::ProxyType::Socks5,
        QCNetworkProxyConfig::ProxyType::Socks5Hostname
    };

    for (auto type : types) {
        QCNetworkProxyConfig proxyConfig;
        proxyConfig.type = type;
        proxyConfig.hostName = "proxy.test.com";
        proxyConfig.port = 1080;

        QCNetworkRequest request(QUrl("http://httpbin.org/get"));
        request.setProxyConfig(proxyConfig);

        QVERIFY(request.proxyConfig().has_value());
        if (request.proxyConfig()) {
            QCOMPARE(request.proxyConfig()->type, type);
        }
    }

    qDebug() << "✅ 代理类型验证通过（7 种类型）";
}

void TestQCNetworkProxy::testProxyAuthentication()
{
    qDebug() << "测试 3：代理认证配置验证";

    // 创建带认证的代理配置
    QCNetworkProxyConfig proxyConfig;
    proxyConfig.type = QCNetworkProxyConfig::ProxyType::Http;
    proxyConfig.hostName = "auth-proxy.example.com";
    proxyConfig.port = 8080;
    proxyConfig.userName = "testuser";
    proxyConfig.password = "testpass";

    QCNetworkRequest request(QUrl("http://httpbin.org/get"));
    request.setProxyConfig(proxyConfig);

    // 验证认证信息
    QVERIFY(request.proxyConfig().has_value());
    auto proxyConfigOpt = request.proxyConfig();
    if (proxyConfigOpt) {
        QCOMPARE(proxyConfigOpt->userName, QString("testuser"));
        QCOMPARE(proxyConfigOpt->password, QString("testpass"));
    }

    qDebug() << "✅ 代理认证配置验证通过";
}

void TestQCNetworkProxy::testProxyAppliedToCurlHandle()
{
    qDebug() << "测试 4.1：代理配置是否正确写入 curl 句柄";

    QCNetworkRequest request(QUrl("https://example.com"));
    QCNetworkProxyConfig proxyConfig;
    proxyConfig.type = QCNetworkProxyConfig::ProxyType::Socks5Hostname;
    proxyConfig.hostName = "proxy.example.com";
    proxyConfig.port = 1080;
    proxyConfig.userName = "user";
    proxyConfig.password = "secret";
    request.setProxyConfig(proxyConfig);

    QCNetworkReplyPrivate replyPrivate(nullptr,
                                       request,
                                       HttpMethod::Get,
                                       ExecutionMode::Sync,
                                       QByteArray());

    QVERIFY(replyPrivate.configureCurlOptions());
    QCOMPARE(replyPrivate.proxyHostBytes, proxyConfig.hostName.toUtf8());
    QCOMPARE(replyPrivate.proxyUserBytes, proxyConfig.userName.toUtf8());
    QCOMPARE(replyPrivate.proxyPasswordBytes, proxyConfig.password.toUtf8());

    // 无效配置应被忽略
    QCNetworkRequest invalidReq(QUrl("https://example.com"));
    QCNetworkProxyConfig invalidProxy;
    invalidProxy.type = QCNetworkProxyConfig::ProxyType::Http;
    invalidProxy.port = 8080;
    invalidReq.setProxyConfig(invalidProxy);

    QCNetworkReplyPrivate invalidPrivate(nullptr,
                                         invalidReq,
                                         HttpMethod::Get,
                                         ExecutionMode::Sync,
                                         QByteArray());

    QVERIFY(invalidPrivate.configureCurlOptions());
    QVERIFY(invalidPrivate.proxyHostBytes.isEmpty());
    QVERIFY(invalidPrivate.proxyUserBytes.isEmpty());
    QVERIFY(invalidPrivate.proxyPasswordBytes.isEmpty());

    qDebug() << "✅ 代理配置已正确写入 curl 句柄";
}

void TestQCNetworkProxy::testSslConfigApplied()
{
    qDebug() << "测试 4.2：SSL 配置是否正确写入 curl 句柄";

    QCNetworkRequest request(QUrl("https://secure.example.com"));
    QCNetworkSslConfig sslConfig;
    sslConfig.verifyPeer = false;
    sslConfig.verifyHost = true;
    sslConfig.caCertPath = "/etc/ssl/custom-ca.pem";
    sslConfig.clientCertPath = "/tmp/client.crt";
    sslConfig.clientKeyPath = "/tmp/client.key";
    sslConfig.clientKeyPassword = "passphrase";
    request.setSslConfig(sslConfig);

    QCNetworkReplyPrivate replyPrivate(nullptr,
                                       request,
                                       HttpMethod::Get,
                                       ExecutionMode::Sync,
                                       QByteArray());

    QVERIFY(replyPrivate.configureCurlOptions());
    QCOMPARE(replyPrivate.sslCaCertPathBytes, sslConfig.caCertPath.toUtf8());
    QCOMPARE(replyPrivate.sslClientCertPathBytes, sslConfig.clientCertPath.toUtf8());
    QCOMPARE(replyPrivate.sslClientKeyPathBytes, sslConfig.clientKeyPath.toUtf8());
    QCOMPARE(replyPrivate.sslClientKeyPasswordBytes, sslConfig.clientKeyPassword.toUtf8());

    // 空路径应清空缓存
    QCNetworkRequest defaultSslRequest(QUrl("https://example.com"));
    defaultSslRequest.setSslConfig(QCNetworkSslConfig::defaultConfig());
    QCNetworkReplyPrivate defaultPrivate(nullptr,
                                         defaultSslRequest,
                                         HttpMethod::Get,
                                         ExecutionMode::Sync,
                                         QByteArray());

    QVERIFY(defaultPrivate.configureCurlOptions());
    QVERIFY(defaultPrivate.sslCaCertPathBytes.isEmpty());
    QVERIFY(defaultPrivate.sslClientCertPathBytes.isEmpty());
    QVERIFY(defaultPrivate.sslClientKeyPathBytes.isEmpty());
    QVERIFY(defaultPrivate.sslClientKeyPasswordBytes.isEmpty());

    qDebug() << "✅ SSL 配置已正确传递";
}

void TestQCNetworkProxy::testProxyWithSsl()
{
    qDebug() << "测试 4：代理与 SSL 结合配置";

    // HTTPS 代理配置
    QCNetworkProxyConfig proxyConfig;
    proxyConfig.type = QCNetworkProxyConfig::ProxyType::Https;
    proxyConfig.hostName = "secure-proxy.example.com";
    proxyConfig.port = 443;

    // SSL 配置
    QCNetworkSslConfig sslConfig = QCNetworkSslConfig::defaultConfig();
    sslConfig.verifyPeer = true;
    sslConfig.verifyHost = true;

    // 同时设置代理和 SSL
    QCNetworkRequest request(QUrl("https://httpbin.org/get"));
    request.setProxyConfig(proxyConfig);
    request.setSslConfig(sslConfig);

    // 验证两者都已设置
    QVERIFY(request.proxyConfig().has_value());
    QVERIFY(request.sslConfig().verifyPeer);
    QVERIFY(request.sslConfig().verifyHost);

    auto proxyConfigOpt = request.proxyConfig();
    if (proxyConfigOpt) {
        QCOMPARE(proxyConfigOpt->type, QCNetworkProxyConfig::ProxyType::Https);
    }

    qDebug() << "✅ 代理与 SSL 结合配置验证通过";
}

void TestQCNetworkProxy::testProxyConnectionFailed()
{
    qDebug() << "测试 5：无效代理错误处理";

    // 使用无效的代理地址（确保失败）
    QCNetworkProxyConfig proxyConfig;
    proxyConfig.type = QCNetworkProxyConfig::ProxyType::Http;
    proxyConfig.hostName = "invalid-proxy-host.nonexistent";
    proxyConfig.port = 9999;

    QCNetworkRequest request(QUrl("http://httpbin.org/get"));
    request.setProxyConfig(proxyConfig);

    // 设置短超时，快速失败
    QCNetworkTimeoutConfig timeoutConfig;
    timeoutConfig.connectTimeout = std::chrono::seconds(2);
    timeoutConfig.totalTimeout = std::chrono::seconds(3);
    request.setTimeoutConfig(timeoutConfig);

    auto *reply = m_manager->sendGet(request);
    QVERIFY(reply != nullptr);

    // 等待请求完成（应该失败）
    bool finished = waitForReply(reply, 5000);
    QVERIFY(finished);  // 应该超时或连接失败

    // 验证错误码
    NetworkError error = reply->error();
    qDebug() << "  错误码:" << static_cast<int>(error);
    qDebug() << "  错误信息:" << reply->errorString();

    // 应该是连接失败、超时或主机不可达
    QVERIFY(error != NetworkError::NoError);
    QVERIFY(error == NetworkError::ConnectionRefused ||
            error == NetworkError::ConnectionTimeout ||
            error == NetworkError::HostNotFound ||
            static_cast<int>(error) >= 1000);  // CURLcode 错误（包含代理错误）

    reply->deleteLater();

    qDebug() << "✅ 无效代理错误处理验证通过";
}

QTEST_MAIN(TestQCNetworkProxy)
#include "tst_QCNetworkProxy.moc"
