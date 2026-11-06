/**
 * @file
 * @brief 用双执行器的实际线路验证代理绕过、认证协商与 HTTPS 类型。
 */
#include "QCBlockingNetworkClient.h"
#include "QCNetworkProxyConfig.h"
#include "QCNetworkReply.h"
#include "QCNetworkRequest.h"
#include "qcurl_http_script_server.h"
#include "tst_QCNetworkProxy.h"

#include <QScopeGuard>
#include <QSignalSpy>
#include <QTcpSocket>
#include <QtTest>

#include <future>

using namespace QCurl;

namespace {

void restoreEnvironment(const char *name, const QByteArray &value)
{
    if (value.isNull()) {
        qunsetenv(name);
    } else {
        qputenv(name, value);
    }
}

QCNetworkRequest proxyRequest(const QUrl &url, quint16 port, QCNetworkProxyConfig::ProxyType type)
{
    QCNetworkRequest request(url);
    QCNetworkProxyConfig proxy;
    proxy.setType(type);
    proxy.setHostName(QStringLiteral("127.0.0.1"));
    proxy.setPort(port);
    request.setProxyConfig(proxy);
    request.setTimeout(std::chrono::seconds(3));
    return request;
}

} // namespace

void TestQCNetworkProxy::explicitProxyPolicy_data()
{
    QTest::addColumn<bool>("blocking");
    QTest::addColumn<bool>("credentials");
    for (bool blocking : {false, true}) {
        for (bool credentials : {false, true}) {
            const auto name = QByteArray(blocking ? "blocking" : "async")
                              + (credentials ? "-challenge" : "-anonymous");
            QTest::newRow(name) << blocking << credentials;
        }
    }
}

void TestQCNetworkProxy::explicitProxyPolicy()
{
    QFETCH(bool, blocking);
    QFETCH(bool, credentials);
    HttpScriptServer origin({HttpScriptServer::response(200, "origin")});
    QVERIFY(origin.start());
    QList<HttpScriptServer::Response> responses;
    if (credentials) {
        responses.append(
            HttpScriptServer::response(407, "", "Proxy-Authenticate: Basic realm=\"fixture\"\r\n"));
    }
    responses.append(HttpScriptServer::response(200, "proxy"));
    HttpScriptServer proxy(responses);
    QVERIFY(proxy.start());
    const auto lower   = qgetenv("no_proxy");
    const auto upper   = qgetenv("NO_PROXY");
    const auto forced  = qgetenv("QCURL_TEST_FORCE_SETOPT_ERROR");
    const auto restore = qScopeGuard([=]() {
        restoreEnvironment("no_proxy", lower);
        restoreEnvironment("NO_PROXY", upper);
        restoreEnvironment("QCURL_TEST_FORCE_SETOPT_ERROR", forced);
    });
    qputenv("no_proxy", "*");
    qputenv("NO_PROXY", "*");
    // 匿名请求不能触碰认证选项；带凭据时观察先挑战再发送的原生协商。
    qputenv("QCURL_TEST_FORCE_SETOPT_ERROR", credentials ? "" : "CURLOPT_PROXYAUTH");
    auto request = proxyRequest(origin.url(),
                                quint16(proxy.url().port()),
                                QCNetworkProxyConfig::ProxyType::Http);
    if (credentials) {
        auto config = *request.proxyConfig();
        config.setUserName(QStringLiteral("user"));
        config.setPassword(QStringLiteral("fixture-password"));
        request.setProxyConfig(config);
    }
    QByteArray body;
    if (blocking) {
        auto pending = std::async(std::launch::async,
                                  [request]() { return QCBlockingNetworkClient().get(request); });
        QTRY_VERIFY(pending.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready);
        const auto result = pending.get();
        QVERIFY2(result.isSuccess(), qPrintable(result.errorMessage()));
        body = result.body();
    } else {
        auto *reply = m_manager->get(request);
        QVERIFY(waitForReply(reply));
        QVERIFY2(reply->error() == NetworkError::NoError, qPrintable(reply->errorString()));
        body = reply->readAll().value();
    }
    QCOMPARE(body, QByteArray("proxy"));
    QCOMPARE(origin.connectionCount(), 0);
    QCOMPARE(proxy.requests().size(), credentials ? 2 : 1);
    QVERIFY(!proxy.requests().first().toLower().contains("\r\nproxy-authorization:"));
    if (credentials) {
        QVERIFY(proxy.requests().last().contains("Proxy-Authorization: Basic "
                                                 + QByteArray("user:fixture-password").toBase64()));
    }
}

void TestQCNetworkProxy::httpsProxyTransport_data()
{
    QTest::addColumn<bool>("blocking");
    QTest::addColumn<bool>("tls");
    for (bool blocking : {false, true}) {
        for (bool tls : {false, true}) {
            QTest::newRow(QByteArray(blocking ? "blocking" : "async") + (tls ? "-https" : "-http"))
                << blocking << tls;
        }
    }
}

void TestQCNetworkProxy::httpsProxyTransport()
{
    QFETCH(bool, blocking);
    QFETCH(bool, tls);
    QTcpServer listener;
    QVERIFY(listener.listen(QHostAddress::LocalHost, 0));
    QByteArray firstBytes;
    connect(&listener, &QTcpServer::newConnection, this, [&]() {
        auto *socket = listener.nextPendingConnection();
        connect(socket, &QTcpSocket::readyRead, socket, [&, socket]() {
            firstBytes += socket->readAll();
            socket->write("HTTP/1.1 200 OK\r\nContent-Length: 2\r\nConnection: close\r\n\r\nok");
            socket->disconnectFromHost();
        });
        connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
    });
    const auto request = proxyRequest(QUrl(QStringLiteral("http://proxy-policy.invalid/")),
                                      listener.serverPort(),
                                      tls ? QCNetworkProxyConfig::ProxyType::Https
                                          : QCNetworkProxyConfig::ProxyType::Http);
    bool success       = false;
    if (blocking) {
        auto pending = std::async(std::launch::async,
                                  [request]() { return QCBlockingNetworkClient().get(request); });
        QTRY_VERIFY(pending.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready);
        success = pending.get().isSuccess();
    } else {
        auto *reply = m_manager->get(request);
        QVERIFY(waitForReply(reply));
        success = reply->error() == NetworkError::NoError;
    }
    QVERIFY(firstBytes.size() >= 3);
    QCOMPARE(firstBytes.startsWith(QByteArray::fromHex("1603")), tls);
    QCOMPARE(firstBytes.startsWith("GET http://"), !tls);
    QCOMPARE(success, !tls);
}
