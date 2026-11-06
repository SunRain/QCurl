/**
 * @file tst_QCNetworkProxy.cpp
 * @brief 验证代理配置、TLS 选项和错误路径的稳定合同。
 *
 * 该套件覆盖本地 TLS/HTTPS proxy 正负控制、配置值语义、能力策略及连接失败。
 */

#include "tst_QCNetworkProxy.h"

#include "QCBlockingNetworkClient.h"
#include "QCNetworkAccessManager.h"
#include "QCNetworkError.h"
#include "QCNetworkProxyConfig.h"
#include "QCNetworkReply.h"
#include "QCNetworkReply_p.h"
#include "QCNetworkRequest.h"
#include "QCNetworkSslConfig.h"
#include "QCNetworkTimeoutConfig.h"
#include "qcnetwork_connect_test_proxy.h"
#include "qcurl_http_script_server.h"
#include "test_source_paths.h"

#include <QHostAddress>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QTcpServer>
#include <QtTest>

#include <curl/curl.h>
#include <future>

using namespace QCurl;

void TestQCNetworkProxy::initTestCase()
{
    qDebug() << "Contract: proxy 配置、真实 TLS 应用与错误路径";
    const auto *runtime = curl_version_info(CURLVERSION_NOW);
    qInfo().noquote() << "libcurl compile=" << LIBCURL_VERSION << "runtime=" << runtime->version
                      << "TLS=" << runtime->ssl_version;

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
    qDebug() << "Verifying request-level proxy config storage";

    // 创建代理配置
    QCNetworkProxyConfig proxyConfig;
    proxyConfig.setType(QCNetworkProxyConfig::ProxyType::Http);
    proxyConfig.setHostName("proxy.example.com");
    proxyConfig.setPort(8080);

    // 创建请求并设置代理
    QCNetworkRequest request(QUrl("http://httpbin.org/get"));
    request.setProxyConfig(proxyConfig);

    // 验证代理配置已设置
    QVERIFY(request.proxyConfig().has_value());

    auto proxyConfigOpt = request.proxyConfig();
    if (proxyConfigOpt) {
        QCOMPARE(proxyConfigOpt->type(), QCNetworkProxyConfig::ProxyType::Http);
        QCOMPARE(proxyConfigOpt->hostName(), QStringLiteral("proxy.example.com"));
        QCOMPARE(proxyConfigOpt->port(), quint16(8080));
    }

    qDebug() << "Request-level proxy config verified";
}

void TestQCNetworkProxy::testProxyTypeValidation()
{
    qDebug() << "Verifying supported proxy type round-trip";

    // 测试所有支持的代理类型
    QList<QCNetworkProxyConfig::ProxyType> types = {QCNetworkProxyConfig::ProxyType::None,
                                                    QCNetworkProxyConfig::ProxyType::Http,
                                                    QCNetworkProxyConfig::ProxyType::Https,
                                                    QCNetworkProxyConfig::ProxyType::Socks4,
                                                    QCNetworkProxyConfig::ProxyType::Socks4A,
                                                    QCNetworkProxyConfig::ProxyType::Socks5,
                                                    QCNetworkProxyConfig::ProxyType::Socks5Hostname};

    for (auto type : types) {
        QCNetworkProxyConfig proxyConfig;
        proxyConfig.setType(type);
        proxyConfig.setHostName("proxy.test.com");
        proxyConfig.setPort(1080);

        QCNetworkRequest request(QUrl("http://httpbin.org/get"));
        request.setProxyConfig(proxyConfig);

        QVERIFY(request.proxyConfig().has_value());
        if (request.proxyConfig()) {
            QCOMPARE(request.proxyConfig()->type(), type);
        }
    }

    qDebug() << "Supported proxy types verified";
}

void TestQCNetworkProxy::testProxyAuthentication()
{
    qDebug() << "Verifying proxy authentication fields";

    // 创建带认证的代理配置
    QCNetworkProxyConfig proxyConfig;
    proxyConfig.setType(QCNetworkProxyConfig::ProxyType::Http);
    proxyConfig.setHostName("auth-proxy.example.com");
    proxyConfig.setPort(8080);
    proxyConfig.setUserName("testuser");
    proxyConfig.setPassword("testpass");

    QCNetworkRequest request(QUrl("http://httpbin.org/get"));
    request.setProxyConfig(proxyConfig);

    // 验证认证信息
    QVERIFY(request.proxyConfig().has_value());
    auto proxyConfigOpt = request.proxyConfig();
    if (proxyConfigOpt) {
        QCOMPARE(proxyConfigOpt->userName(), QStringLiteral("testuser"));
        QCOMPARE(proxyConfigOpt->password(), QStringLiteral("testpass"));
    }

    qDebug() << "Proxy authentication fields verified";
}

void TestQCNetworkProxy::testProxyAppliedToCurlHandle()
{
    const QString prefix = TestSourcePaths::sourcePath(
        QStringLiteral("tests/qcurl/testdata/http2/"));
    LocalTlsServer server(prefix + QStringLiteral("localhost.crt"),
                          prefix + QStringLiteral("localhost.key"));
    server.setHttpResponseEnabled(true);
    QVERIFY2(server.start(), qPrintable(server.errorString()));
    ConnectTestProxy proxy(server.port());
    QVERIFY(proxy.start());
    QCNetworkProxyConfig config;
    config.setType(QCNetworkProxyConfig::ProxyType::Http);
    config.setHostName(QStringLiteral("127.0.0.1"));
    config.setPort(proxy.port());
    config.setUserName(QStringLiteral("user"));
    config.setPassword(QStringLiteral("fixture-password"));
    QCNetworkRequest request(QUrl(QStringLiteral("https://localhost:%1/").arg(server.port())));
    QCNetworkSslConfig ssl;
    ssl.setCaCertPath(prefix + QStringLiteral("localhost.crt"));
    request.setSslConfig(ssl);
    request.setProxyConfig(config);
    auto *reply = m_manager->get(request);
    QVERIFY(waitForReply(reply));
    QCOMPARE(reply->error(), NetworkError::NoError);
    QCOMPARE(server.requests().size(), 1);
    const auto requests = proxy.requests();
    QCOMPARE(requests.size(), 2);
    QVERIFY(!requests.first().toLower().contains("\r\nproxy-authorization:"));
    const QByteArray credentials = QByteArrayLiteral("user:fixture-password").toBase64();
    QVERIFY(
        requests.last().contains(QByteArrayLiteral("Proxy-Authorization: Basic ") + credentials));
    reply->deleteLater();
}

void TestQCNetworkProxy::testProxyEnvironmentDisabledByDefault()
{
    HttpScriptServer server({HttpScriptServer::response(200, QByteArrayLiteral("direct"))});
    QVERIFY(server.start());
    HttpScriptServer unwantedProxy({HttpScriptServer::response(502, QByteArrayLiteral("proxy"))});
    QVERIFY(unwantedProxy.start());
    const QList<QByteArray> variables{QByteArrayLiteral("http_proxy"),
                                      QByteArrayLiteral("HTTP_PROXY"),
                                      QByteArrayLiteral("ALL_PROXY"),
                                      QByteArrayLiteral("all_proxy"),
                                      QByteArrayLiteral("NO_PROXY"),
                                      QByteArrayLiteral("no_proxy")};
    QList<QByteArray> previous;
    for (const auto &name : variables) {
        previous.append(qgetenv(name.constData()));
        qputenv(name.constData(),
                name.toLower() == "no_proxy" ? QByteArray("") : unwantedProxy.url().toEncoded());
    }
    const auto restore = qScopeGuard([variables, previous]() {
        for (qsizetype i = 0; i < variables.size(); ++i) {
            if (previous.at(i).isNull()) {
                qunsetenv(variables.at(i).constData());
            } else {
                qputenv(variables.at(i).constData(), previous.at(i));
            }
        }
    });
    for (int mode = 0; mode < 3; ++mode) {
        QCNetworkRequest request(server.url());
        if (mode != 0) {
            QCNetworkProxyConfig config;
            config.setType(mode == 1 ? QCNetworkProxyConfig::ProxyType::None
                                     : QCNetworkProxyConfig::ProxyType::Http);
            config.setPort(8080);
            request.setProxyConfig(config);
        }
        request.setTimeout(std::chrono::seconds(3));
        auto *reply = m_manager->get(request);
        QVERIFY(waitForReply(reply));
        auto blocking = std::async(std::launch::async,
                                   [request]() { return QCBlockingNetworkClient().get(request); });
        QTRY_VERIFY(blocking.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready);
        const auto result = blocking.get();
        if (mode == 2) {
            QCOMPARE(reply->error(), NetworkError::InvalidRequest);
            QCOMPARE(result.error(), NetworkError::InvalidRequest);
            QVERIFY(reply->errorString().contains(QStringLiteral("代理")));
        } else {
            QCOMPARE(reply->error(), NetworkError::NoError);
            QVERIFY2(result.isSuccess(), qPrintable(result.errorMessage()));
            QCOMPARE(reply->readAll().value(), QByteArrayLiteral("direct"));
            QCOMPARE(result.body(), QByteArrayLiteral("direct"));
        }
        reply->deleteLater();
    }
    QCOMPARE(server.requests().size(), 4);
    QCOMPARE(unwantedProxy.requests().size(), 0);
    QCOMPARE(unwantedProxy.connectionCount(), 0);
}

void TestQCNetworkProxy::testSslConfigValueSemantics()
{
    QCNetworkSslConfig sslConfig;
    sslConfig.setVerifyPeer(true);
    sslConfig.setCaCertPath(QStringLiteral("/tmp/ca.pem"));
    sslConfig.setPinnedPublicKey(QStringLiteral("sha256//first"));

    QCNetworkSslConfig copied(sslConfig);
    sslConfig.setVerifyPeer(false);
    sslConfig.setCaCertPath(QStringLiteral("/tmp/other-ca.pem"));
    sslConfig.setPinnedPublicKey(QStringLiteral("sha256//second"));

    QVERIFY(copied.verifyPeer());
    QCOMPARE(copied.caCertPath(), QStringLiteral("/tmp/ca.pem"));
    QCOMPARE(copied.pinnedPublicKey(), QStringLiteral("sha256//first"));
}

void TestQCNetworkProxy::testTimeoutConfigOptionalLifecycle()
{
    QCNetworkTimeoutConfig timeoutConfig;
    timeoutConfig.setConnectTimeout(std::chrono::seconds(3));
    timeoutConfig.setTotalTimeout(std::chrono::seconds(15));

    QCNetworkTimeoutConfig copied(timeoutConfig);
    timeoutConfig.setConnectTimeout(std::nullopt);
    timeoutConfig.setTotalTimeout(std::nullopt);

    QVERIFY(!timeoutConfig.connectTimeout().has_value());
    QVERIFY(!timeoutConfig.totalTimeout().has_value());
    QVERIFY(copied.connectTimeout().has_value());
    QVERIFY(copied.totalTimeout().has_value());
    QCOMPARE(copied.connectTimeout()->count(), std::chrono::milliseconds(3000).count());
    QCOMPARE(copied.totalTimeout()->count(), std::chrono::milliseconds(15000).count());
}

void TestQCNetworkProxy::testProxyTlsConfigOptionalLifecycle()
{
    QCNetworkProxyConfig::ProxyTlsConfig tlsConfig;
    tlsConfig.setVerifyPeer(true);
    tlsConfig.setVerifyHost(true);
    tlsConfig.setCaCertPath(QStringLiteral("/tmp/proxy-ca.pem"));
    tlsConfig.setCipherList(QStringLiteral("TLS_AES_128_GCM_SHA256"));

    QCNetworkProxyConfig proxyConfig;
    proxyConfig.setType(QCNetworkProxyConfig::ProxyType::Https);
    proxyConfig.setHostName(QStringLiteral("proxy.example.com"));
    proxyConfig.setPort(443);
    proxyConfig.setTlsConfig(tlsConfig);

    QCNetworkProxyConfig copied(proxyConfig);
    proxyConfig.clearTlsConfig();

    QVERIFY(!proxyConfig.tlsConfig().has_value());
    QVERIFY(copied.tlsConfig().has_value());
    QCOMPARE(copied.tlsConfig()->caCertPath(), QStringLiteral("/tmp/proxy-ca.pem"));
    QCOMPARE(copied.tlsConfig()->cipherList(), QStringLiteral("TLS_AES_128_GCM_SHA256"));
    QVERIFY(copied.tlsConfig()->verifyPeer());
    QVERIFY(copied.tlsConfig()->verifyHost());
}

void TestQCNetworkProxy::testProxyTlsConfigBraceInitSetter()
{
    QCNetworkProxyConfig proxyConfig;

    proxyConfig.setTlsConfig({});
    QVERIFY(proxyConfig.tlsConfig().has_value());
    QVERIFY(proxyConfig.tlsConfig()->verifyPeer());
    QVERIFY(proxyConfig.tlsConfig()->verifyHost());

    proxyConfig.clearTlsConfig();
    QVERIFY(!proxyConfig.tlsConfig().has_value());
}

void TestQCNetworkProxy::testProxyWithSsl()
{
    qDebug() << "Verifying proxy and SSL config can coexist on one request";

    // HTTPS 代理配置
    QCNetworkProxyConfig proxyConfig;
    proxyConfig.setType(QCNetworkProxyConfig::ProxyType::Https);
    proxyConfig.setHostName("secure-proxy.example.com");
    proxyConfig.setPort(443);

    // SSL 配置
    QCNetworkSslConfig sslConfig = QCNetworkSslConfig::defaultConfig();
    sslConfig.setVerifyPeer(true);
    sslConfig.setVerifyHost(true);

    // 同时设置代理和 SSL
    QCNetworkRequest request(QUrl("https://httpbin.org/get"));
    request.setProxyConfig(proxyConfig);
    request.setSslConfig(sslConfig);

    // 验证两者都已设置
    QVERIFY(request.proxyConfig().has_value());
    QVERIFY(request.sslConfig().verifyPeer());
    QVERIFY(request.sslConfig().verifyHost());

    auto proxyConfigOpt = request.proxyConfig();
    if (proxyConfigOpt) {
        QCOMPARE(proxyConfigOpt->type(), QCNetworkProxyConfig::ProxyType::Https);
    }

    qDebug() << "Proxy + SSL request contract verified";
}

void TestQCNetworkProxy::testProxyTlsUnsupportedFail()
{
    qDebug() << "Verifying proxy TLS capability failure path";

    const QByteArray oldEnv = qgetenv("QCURL_TEST_FORCE_CAPABILITY_ERROR");
    qputenv("QCURL_TEST_FORCE_CAPABILITY_ERROR", QByteArrayLiteral("CURLOPT_PROXY_SSL_CIPHER_LIST"));

    QCNetworkRequest request(QUrl("https://example.com"));
    QCNetworkProxyConfig proxy;
    proxy.setType(QCNetworkProxyConfig::ProxyType::Https);
    proxy.setHostName("proxy.example.com");
    proxy.setPort(443);

    QCNetworkProxyConfig::ProxyTlsConfig tls;
    tls.setCipherList("TLS_AES_128_GCM_SHA256");
    tls.setUnsupportedSecurityPolicy(QCUnsupportedSecurityOptionPolicy::Fail);
    proxy.setTlsConfig(tls);
    request.setProxyConfig(proxy);

    QCNetworkReplyPrivate replyPrivate(nullptr,
                                       request,
                                       HttpMethod::Get,
                                       Internal::makeEmptyRequestBody(),
                                       QByteArray());

    QVERIFY(!replyPrivate.configureCurlOptions());
    QCOMPARE(replyPrivate.transferState->lifecycle.error(), NetworkError::InvalidRequest);
    QVERIFY(replyPrivate.transferState->lifecycle.errorMessage().contains(
        "CURLOPT_PROXY_SSL_CIPHER_LIST"));
    QVERIFY(replyPrivate.transferState->lifecycle.errorMessage().contains(
        QStringLiteral("libcurl 不支持")));

    if (oldEnv.isEmpty()) {
        qunsetenv("QCURL_TEST_FORCE_CAPABILITY_ERROR");
    } else {
        qputenv("QCURL_TEST_FORCE_CAPABILITY_ERROR", oldEnv);
    }
}

void TestQCNetworkProxy::testProxyTlsUnsupportedWarn()
{
    qDebug() << "Verifying proxy TLS capability warning path";

    const QByteArray oldEnv = qgetenv("QCURL_TEST_FORCE_CAPABILITY_ERROR");
    qputenv("QCURL_TEST_FORCE_CAPABILITY_ERROR", QByteArrayLiteral("CURLOPT_PROXY_SSL_CIPHER_LIST"));

    QCNetworkRequest request(QUrl("https://example.com"));
    QCNetworkProxyConfig proxy;
    proxy.setType(QCNetworkProxyConfig::ProxyType::Https);
    proxy.setHostName("proxy.example.com");
    proxy.setPort(443);

    QCNetworkProxyConfig::ProxyTlsConfig tls;
    tls.setCipherList("TLS_AES_128_GCM_SHA256");
    tls.setUnsupportedSecurityPolicy(QCUnsupportedSecurityOptionPolicy::Warn);
    proxy.setTlsConfig(tls);
    request.setProxyConfig(proxy);

    QCNetworkReplyPrivate replyPrivate(nullptr,
                                       request,
                                       HttpMethod::Get,
                                       Internal::makeEmptyRequestBody(),
                                       QByteArray());

    QVERIFY(replyPrivate.configureCurlOptions());
    QCOMPARE(replyPrivate.transferState->lifecycle.error(), NetworkError::NoError);
    const auto warnings = replyPrivate.transferState->lifecycle.warnings().join("\n");
    QVERIFY(warnings.contains("CURLOPT_PROXY_SSL_CIPHER_LIST"));
    QVERIFY(warnings.contains(QStringLiteral("libcurl 不支持")));

    if (oldEnv.isEmpty()) {
        qunsetenv("QCURL_TEST_FORCE_CAPABILITY_ERROR");
    } else {
        qputenv("QCURL_TEST_FORCE_CAPABILITY_ERROR", oldEnv);
    }
}

void TestQCNetworkProxy::testProxyConnectionFailed()
{
    qDebug() << "Verifying proxy connection failure error path";

    // 取证口径：避免依赖固定端口/外网 DNS 的非确定性前提。
    // 使用本机临时端口并立即释放，获得更稳定的“连接失败”场景。
    QTcpServer portPicker;
    QVERIFY2(portPicker.listen(QHostAddress::LocalHost, 0),
             "无法绑定本机端口用于生成确定性 proxy connection failure 场景");
    const quint16 port = portPicker.serverPort();
    portPicker.close();

    // 使用本地无 listener 的 proxy endpoint（确保失败）
    QCNetworkProxyConfig proxyConfig;
    proxyConfig.setType(QCNetworkProxyConfig::ProxyType::Http);
    proxyConfig.setHostName("127.0.0.1");
    proxyConfig.setPort(port);

    // 目标 URL 在 proxy 连接失败前不应成为变量；使用本机 URL 避免外网依赖。
    QCNetworkRequest request(QUrl("http://127.0.0.1/"));
    request.setProxyConfig(proxyConfig);

    // 设置短超时，快速失败
    QCNetworkTimeoutConfig timeoutConfig;
    timeoutConfig.setConnectTimeout(std::chrono::seconds(2));
    timeoutConfig.setTotalTimeout(std::chrono::seconds(3));
    request.setTimeoutConfig(timeoutConfig);

    auto *reply = m_manager->get(request);
    QVERIFY(reply != nullptr);

    // 等待请求完成（应该失败）
    bool finished = waitForReply(reply, 5000);
    QVERIFY(finished); // 应该超时或连接失败

    // 验证错误码
    NetworkError error = reply->error();
    qDebug() << "  错误码:" << static_cast<int>(error);
    qDebug() << "  错误信息:" << reply->errorString();

    // 应该是连接失败、超时或主机不可达
    QVERIFY(error != NetworkError::NoError);
    QVERIFY(error == NetworkError::ConnectionRefused
            || static_cast<int>(error) >= 1000); // CURLcode 错误（包含代理错误）

    reply->deleteLater();

    qDebug() << "Proxy connection failure path verified";
}

QTEST_MAIN(TestQCNetworkProxy)
// 测试类的元对象由独立头文件生成。
