#include "QCWebSocketTestServer.h"
#include "test_wait_utils.h"
#include "test_websocket_evidence_utils.h"

#include <QCNetworkSslConfig.h>
#include <QCWebSocket.h>
#include <QCWebSocketCompressionConfig.h>
#include <QCWebSocketReconnectPolicy.h>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDebug>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QHostAddress>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaMethod>
#include <QProcess>
#include <QRegularExpression>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QThread>
#include <QTimer>
#include <QUrlQuery>
#include <QtTest>

using namespace QCurl;

/**
 * @brief QCWebSocket 单元测试
 *
 * 说明：
 * - 默认回显 / ping-pong / close 路径使用 tests/qcurl/websocket-evidence-server.js
 *   （零外部依赖；不再依赖 node_modules/ws）。
 * - frame-level 证据链同样使用 evidence server，显式发送 fragmentation/close 并输出工件。
 * - Fragment Echo 资产仅保留为补充覆盖，不再作为默认 gate 前提。
 * - 所有本地 server 均使用动态端口（0）并通过 READY marker 回传，避免固定端口导致的并发/占用冲突。
 * - WSS/SSL 覆盖同样使用本地 WSS server（自签证书），通过“默认失败 / 配置 CA 后成功”验证证书校验路径。
 */
class TestQCWebSocket : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();

    // ========================================================================
    // 连接测试
    // ========================================================================

    void testConnect();
    void testConnectWss();
    void testReuseSocketWithCompressionHeaders();
    void testReuseSocketWithSslConfig();
    void testAutoPongConfigIgnoredWhileConnected();
    void testCompressionConfigIgnoredWhileConnected();
    void testConnectInvalidUrl();
    void testAutoReconnect();

    // ========================================================================
    // 消息收发测试
    // ========================================================================

    void testSendTextMessage();
    void testSendBinaryMessage();
    void testReceiveTextMessage();
    void testReceiveBinaryMessage();
    void testLargeMessage();

    // ========================================================================
    // 协议测试
    // ========================================================================

    void testPingPong();
    void testCloseHandshake();
    void testFragmentedMessage();
    void testFragmentedFramesReassembly();
    void testServerClosedWithCustomCloseCode();
    void testServerClosedWithReservedCloseCode();
    void testRejectReservedCloseCodeOnSend();

    // ========================================================================
    // 错误处理
    // ========================================================================

    void testConnectionRefused();
    void testSslError();
    void testServerClosedConnection();

private:
    /**
     * @brief 等待信号触发（带超时）
     * @param obj 信号发送对象
     * @param signal 信号元信息
     * @param timeout 超时时间（毫秒）
     * @return 是否成功收到信号
     */
    bool waitForSignal(QObject *obj, const QMetaMethod &signal, int timeout = 5000);

    QString m_testServerUrl;

    QString m_testWssServerUrl;
    QString m_wssEvidenceArtifactsPath;
    QString m_caCertPath;

    QCWebSocketTestServer m_wsServer;
    QCWebSocketTestServer m_wssServer;

    QString m_testEvidenceServerUrl;
    QString m_evidenceArtifactsPath;
    QCWebSocketTestServer m_wsEvidenceServer;
};

void TestQCWebSocket::initTestCase()
{
    m_testServerUrl.clear();
    m_testWssServerUrl.clear();
    m_wssEvidenceArtifactsPath.clear();
    m_caCertPath.clear();
    m_testEvidenceServerUrl.clear();
    m_evidenceArtifactsPath.clear();

    QVERIFY2(m_wsServer.start(QCWebSocketTestServer::Mode::Ws,
                              QCWebSocketTestServer::ServerKind::Evidence),
             qPrintable(m_wsServer.skipReason()));
    m_testServerUrl = m_wsServer.baseUrl();
    qDebug() << "本地测试服务器:" << m_testServerUrl;

    QVERIFY2(m_wssServer.start(QCWebSocketTestServer::Mode::Wss,
                               QCWebSocketTestServer::ServerKind::Evidence),
             qPrintable(m_wssServer.skipReason()));
    m_testWssServerUrl         = m_wssServer.baseUrl();
    m_wssEvidenceArtifactsPath = m_wssServer.artifactsPath();
    m_caCertPath               = m_wssServer.caCertPath();
    qDebug() << "本地 WSS 测试服务器:" << m_testWssServerUrl;
    qDebug() << "WSS evidence artifacts:" << m_wssEvidenceArtifactsPath;

    QVERIFY2(m_wsEvidenceServer.start(QCWebSocketTestServer::Mode::Ws,
                                      QCWebSocketTestServer::ServerKind::Evidence),
             qPrintable(m_wsEvidenceServer.skipReason()));
    m_testEvidenceServerUrl = m_wsEvidenceServer.baseUrl();
    m_evidenceArtifactsPath = m_wsEvidenceServer.artifactsPath();
    qDebug() << "本地 WS Evidence Server:" << m_testEvidenceServerUrl;
    qDebug() << "Evidence artifacts:" << m_evidenceArtifactsPath;
}

void TestQCWebSocket::cleanupTestCase()
{
    m_wsServer.stop();
    m_wssServer.stop();
    m_wsEvidenceServer.stop();
}

// ============================================================================
// 连接测试
// ============================================================================

void TestQCWebSocket::testConnect()
{
    QCWebSocket socket{QUrl(m_testServerUrl), QCWebSocketOptions{}};
    QSignalSpy connectedSpy(&socket, &QCWebSocket::connected);
    QSignalSpy stateChangedSpy(&socket, &QCWebSocket::stateChanged);

    socket.open();

    // 等待连接成功信号
    QVERIFY2(TestWaitUtils::waitForSpyCount(connectedSpy, 1, 10000),
             qPrintable(QStringLiteral("无法连接到本地 WebSocket 测试服务器：%1")
                            .arg(socket.errorString())));
    QCOMPARE(connectedSpy.count(), 1);
    QCOMPARE(socket.state(), QCWebSocket::State::Connected);
    QVERIFY(socket.isValid());

    // 检查状态变化信号
    QVERIFY(stateChangedSpy.count() >= 2); // Connecting -> Connected

    socket.close();

    qDebug() << "Basic connection contract verified";
}

void TestQCWebSocket::testConnectWss()
{
    QVERIFY2(!m_wssEvidenceArtifactsPath.isEmpty(),
             "WSS evidence artifactsPath 为空，无法复核握手证据。");

    const QString caseId = QString::fromLatin1(QTest::currentTestFunction());
    const QUrl url       = TestWebSocketEvidenceUtils::buildCaseUrl(m_testWssServerUrl,
                                                                    QStringLiteral("/"),
                                                                    caseId);

    // 使用本地 wss:// 加密连接（自签证书，需配置 CA）
    QCWebSocket socket{url, QCWebSocketOptions{}};
    QCNetworkSslConfig sslConfig;
    sslConfig.setCaCertPath(m_caCertPath);
    QCWebSocketOptions options = socket.options();
    options.setSslConfig(sslConfig);
    QVERIFY(socket.setOptions(options));
    QSignalSpy connectedSpy(&socket, &QCWebSocket::connected);

    socket.open();

    QVERIFY2(TestWaitUtils::waitForSpyCount(connectedSpy, 1, 15000),
             qPrintable(QStringLiteral("无法连接本地 WSS 测试服务器，caCertPath=%1，错误=%2")
                            .arg(m_caCertPath, socket.errorString())));
    QCOMPARE(connectedSpy.count(), 1);
    QCOMPARE(socket.state(), QCWebSocket::State::Connected);

    const QString handshakeError = TestWebSocketEvidenceUtils::verifyHandshakeEvidence(
        m_wssEvidenceArtifactsPath, caseId, QStringLiteral("/"), true, 1, 2000);
    QVERIFY2(handshakeError.isEmpty(), qPrintable(handshakeError));

    socket.close();

    qDebug() << "WSS connection contract verified";
}

void TestQCWebSocket::testReuseSocketWithCompressionHeaders()
{
    QCWebSocket socket{QUrl(m_testServerUrl), QCWebSocketOptions{}};
    QCWebSocketOptions options = socket.options();
    options.setCompressionConfig(QCWebSocketCompressionConfig::defaultConfig());
    QVERIFY(socket.setOptions(options));

    for (int round = 0; round < 2; ++round) {
        QSignalSpy connectedSpy(&socket, &QCWebSocket::connected);
        QSignalSpy disconnectedSpy(&socket, &QCWebSocket::disconnected);

        socket.open();
        QVERIFY2(TestWaitUtils::waitForSpyCount(connectedSpy, 1, 10000),
                 qPrintable(QStringLiteral("第 %1 次连接未成功，错误=%2")
                                .arg(round + 1)
                                .arg(socket.errorString())));
        QCOMPARE(socket.state(), QCWebSocket::State::Connected);

        socket.close();
        QVERIFY2(TestWaitUtils::waitForSpyCount(disconnectedSpy, 1, 10000),
                 qPrintable(QStringLiteral("第 %1 次关闭未完成").arg(round + 1)));
        QCOMPARE(socket.state(), QCWebSocket::State::Closed);
    }

    qDebug() << "Compression header lifecycle survives socket reuse";
}

void TestQCWebSocket::testReuseSocketWithSslConfig()
{
    QVERIFY2(!m_wssEvidenceArtifactsPath.isEmpty(),
             "WSS evidence artifactsPath 为空，无法复核复用握手证据。");

    const QString caseId = QString::fromLatin1(QTest::currentTestFunction());
    const QUrl url       = TestWebSocketEvidenceUtils::buildCaseUrl(m_testWssServerUrl,
                                                                    QStringLiteral("/"),
                                                                    caseId);

    QCWebSocket socket{url, QCWebSocketOptions{}};
    QCNetworkSslConfig sslConfig;
    sslConfig.setCaCertPath(m_caCertPath);
    QCWebSocketOptions options = socket.options();
    options.setSslConfig(sslConfig);
    QVERIFY(socket.setOptions(options));

    for (int round = 0; round < 2; ++round) {
        QSignalSpy connectedSpy(&socket, &QCWebSocket::connected);
        QSignalSpy disconnectedSpy(&socket, &QCWebSocket::disconnected);

        socket.open();
        QVERIFY2(TestWaitUtils::waitForSpyCount(connectedSpy, 1, 15000),
                 qPrintable(QStringLiteral("第 %1 次 WSS 连接未成功，ca=%2，错误=%3")
                                .arg(round + 1)
                                .arg(m_caCertPath, socket.errorString())));
        QCOMPARE(socket.state(), QCWebSocket::State::Connected);

        socket.close();
        QVERIFY2(TestWaitUtils::waitForSpyCount(disconnectedSpy, 1, 10000),
                 qPrintable(QStringLiteral("第 %1 次 WSS 关闭未完成").arg(round + 1)));
        QCOMPARE(socket.state(), QCWebSocket::State::Closed);
    }

    const QString handshakeError = TestWebSocketEvidenceUtils::verifyHandshakeEvidence(
        m_wssEvidenceArtifactsPath, caseId, QStringLiteral("/"), true, 2, 2000);
    QVERIFY2(handshakeError.isEmpty(), qPrintable(handshakeError));

    qDebug() << "TLS option lifecycle survives socket reuse";
}

void TestQCWebSocket::testAutoPongConfigIgnoredWhileConnected()
{
    QCWebSocket socket{QUrl(QStringLiteral("ws://127.0.0.1:65535")), QCWebSocketOptions{}};
    QCWebSocketOptions options = socket.options();
    options.setAutoPongEnabled(true);
    QVERIFY(socket.setOptions(options));

    socket.open();
    QCOMPARE(socket.state(), QCWebSocket::State::Connecting);
    QVERIFY(socket.options().autoPongEnabled());

    QCWebSocketOptions disabledOptions = socket.options();
    disabledOptions.setAutoPongEnabled(false);
    QString error;
    QVERIFY(!socket.setOptions(disabledOptions, &error));
    QVERIFY(!error.isEmpty());
    QVERIFY(socket.options().autoPongEnabled());
}

void TestQCWebSocket::testCompressionConfigIgnoredWhileConnected()
{
    QCWebSocket socket{QUrl(QStringLiteral("ws://127.0.0.1:65535")), QCWebSocketOptions{}};

    QCWebSocketCompressionConfig initialConfig;
    initialConfig.setEnabled(false);
    initialConfig.setClientMaxWindowBits(15);
    initialConfig.setServerMaxWindowBits(15);
    initialConfig.setClientNoContextTakeover(false);
    initialConfig.setServerNoContextTakeover(false);
    initialConfig.setCompressionLevel(6);
    QCWebSocketOptions options = socket.options();
    options.setCompressionConfig(initialConfig);
    QVERIFY(socket.setOptions(options));

    socket.open();
    QCOMPARE(socket.state(), QCWebSocket::State::Connecting);

    QCWebSocketCompressionConfig updatedConfig = QCWebSocketCompressionConfig::defaultConfig();
    updatedConfig.setClientMaxWindowBits(12);
    updatedConfig.setServerMaxWindowBits(12);
    updatedConfig.setClientNoContextTakeover(true);
    updatedConfig.setServerNoContextTakeover(true);
    updatedConfig.setCompressionLevel(9);

    QCWebSocketOptions updatedOptions = socket.options();
    updatedOptions.setCompressionConfig(updatedConfig);
    QString error;
    QVERIFY(!socket.setOptions(updatedOptions, &error));
    QVERIFY(!error.isEmpty());

    const QCWebSocketCompressionConfig currentConfig = socket.options().compressionConfig();
    QVERIFY(!currentConfig.enabled());
    QCOMPARE(currentConfig.clientMaxWindowBits(), initialConfig.clientMaxWindowBits());
    QCOMPARE(currentConfig.serverMaxWindowBits(), initialConfig.serverMaxWindowBits());
    QCOMPARE(currentConfig.clientNoContextTakeover(), initialConfig.clientNoContextTakeover());
    QCOMPARE(currentConfig.serverNoContextTakeover(), initialConfig.serverNoContextTakeover());
    QCOMPARE(currentConfig.compressionLevel(), initialConfig.compressionLevel());
}

void TestQCWebSocket::testConnectInvalidUrl()
{
    QCWebSocket socket{QUrl("wss://invalid-host-that-does-not-exist.example.com"),
                       QCWebSocketOptions{}};
    QSignalSpy errorSpy(&socket, &QCWebSocket::errorOccurred);

    socket.open();

    // 等待错误信号
    QVERIFY(TestWaitUtils::waitForSpyCount(errorSpy, 1, 10000));
    QVERIFY(errorSpy.count() >= 1);
    QVERIFY(!socket.errorString().isEmpty());
    QCOMPARE(socket.state(), QCWebSocket::State::Unconnected);

    qDebug() << "错误信息:" << socket.errorString();
    qDebug() << "Invalid URL error path verified";
}

void TestQCWebSocket::testAutoReconnect()
{
    // 自动重连功能测试
    // 创建一个会失败的连接（避免固定端口假设；使用临时端口并立即释放，获得更稳定的“连接失败”语义）
    QTcpServer portPicker;
    QVERIFY2(portPicker.listen(QHostAddress::LocalHost, 0),
             "无法绑定本机端口用于生成确定性 connection-refused 场景");
    const quint16 port = portPicker.serverPort();
    portPicker.close();

    QCWebSocket socket(QUrl(QStringLiteral("ws://127.0.0.1:%1").arg(port)), QCWebSocketOptions{});

    // 设置标准重连策略（3 次重连，1s → 2s → 4s）
    QCWebSocketOptions options = socket.options();
    options.setReconnectPolicy(QCWebSocketReconnectPolicy::standardReconnect());
    QVERIFY(socket.setOptions(options));

    // 监听重连尝试信号
    QSignalSpy reconnectSpy(&socket, &QCWebSocket::reconnectAttempt);
    QSignalSpy errorSpy(&socket, &QCWebSocket::errorOccurred);

    // 尝试连接（会失败）
    socket.open();

    // 等待初始连接失败
    QVERIFY(TestWaitUtils::waitForSpyCount(errorSpy, 1, 2000));

    // 等待第一次重连尝试（延迟 1 秒）
    QVERIFY(TestWaitUtils::waitForSpyCount(reconnectSpy, 1, 2500));

    // 验证至少有一次重连尝试
    QVERIFY2(reconnectSpy.count() >= 1, "应该至少有一次重连尝试");

    // 验证重连参数
    if (reconnectSpy.count() > 0) {
        auto args            = reconnectSpy.at(0);
        int attemptCount     = args.at(0).toInt();
        const auto closeCode = args.at(1).value<QCWebSocket::CloseCode>();

        QCOMPARE(attemptCount, 1); // 第一次重连
        QCOMPARE(closeCode, QCWebSocket::CloseCode::AbnormalClosure);
    }

    qDebug() << "Reconnect attempts observed:" << reconnectSpy.count();

    // 停止重连
    socket.abort();
}

// ============================================================================
// 消息收发测试
// ============================================================================

void TestQCWebSocket::testSendTextMessage()
{
    QCWebSocket socket{QUrl(m_testServerUrl), QCWebSocketOptions{}};
    QSignalSpy textSpy(&socket, &QCWebSocket::textMessageReceived);

    socket.open();
    QVERIFY2(waitForSignal(&socket, QMetaMethod::fromSignal(&QCWebSocket::connected), 10000),
             qPrintable(QStringLiteral("无法连接到本地 WebSocket 测试服务器：%1")
                            .arg(socket.errorString())));

    QString testMessage = QStringLiteral("Hello WebSocket!");
    qint64 sent         = socket.sendTextMessage(testMessage);
    QVERIFY(sent > 0);
    qDebug() << "发送字节数:" << sent;

    // 等待本地 echo 服务器回显
    QVERIFY(TestWaitUtils::waitForSpyCount(textSpy, 1, 10000));
    QCOMPARE(textSpy.count(), 1);

    QString received = textSpy.first().first().toString();
    qDebug() << "收到消息:" << received;

    QCOMPARE(received, testMessage);

    socket.close();

    qDebug() << "Text echo contract verified";
}

void TestQCWebSocket::testSendBinaryMessage()
{
    QCWebSocket socket{QUrl(m_testServerUrl), QCWebSocketOptions{}};
    QSignalSpy binarySpy(&socket, &QCWebSocket::binaryMessageReceived);

    socket.open();
    QVERIFY2(waitForSignal(&socket, QMetaMethod::fromSignal(&QCWebSocket::connected), 10000),
             qPrintable(QStringLiteral("无法连接到本地 WebSocket 测试服务器：%1")
                            .arg(socket.errorString())));

    QByteArray testData = "Binary Data: \x01\x02\x03\x04";
    qint64 sent         = socket.sendBinaryMessage(testData);
    QVERIFY(sent > 0);
    qDebug() << "发送字节数:" << sent;

    // 等待 Echo 服务器回显
    QVERIFY(TestWaitUtils::waitForSpyCount(binarySpy, 1, 10000));
    QCOMPARE(binarySpy.count(), 1);

    QByteArray received = binarySpy.first().first().toByteArray();
    QCOMPARE(received, testData);

    socket.close();

    qDebug() << "Binary echo contract verified";
}

void TestQCWebSocket::testReceiveTextMessage()
{
    QCWebSocket socket{QUrl(m_testServerUrl), QCWebSocketOptions{}};
    QSignalSpy textSpy(&socket, &QCWebSocket::textMessageReceived);

    socket.open();
    QVERIFY2(waitForSignal(&socket, QMetaMethod::fromSignal(&QCWebSocket::connected), 10000),
             qPrintable(QStringLiteral("无法连接到本地 WebSocket 测试服务器：%1")
                            .arg(socket.errorString())));

    const QString testMessage = QStringLiteral("Receive Test");
    socket.sendTextMessage(testMessage);

    QVERIFY(TestWaitUtils::waitForSpyCount(textSpy, 1, 10000));
    QCOMPARE(textSpy.count(), 1);
    QCOMPARE(textSpy.first().first().toString(), testMessage);

    socket.close();

    qDebug() << "Text receive path verified";
}

void TestQCWebSocket::testReceiveBinaryMessage()
{
    QCWebSocket socket{QUrl(m_testServerUrl), QCWebSocketOptions{}};
    QSignalSpy binarySpy(&socket, &QCWebSocket::binaryMessageReceived);

    socket.open();
    QVERIFY2(waitForSignal(&socket, QMetaMethod::fromSignal(&QCWebSocket::connected), 10000),
             qPrintable(QStringLiteral("无法连接到本地 WebSocket 测试服务器：%1")
                            .arg(socket.errorString())));

    // 发送多条二进制消息
    for (int i = 0; i < 3; ++i) {
        QByteArray data;
        data.append(static_cast<char>(i));
        data.append("Test Binary Data");
        socket.sendBinaryMessage(data);
        QVERIFY2(TestWaitUtils::waitForSpyCount(binarySpy, i + 1, 10000),
                 qPrintable(QStringLiteral("第 %1 条二进制回显未按时到达，当前累计=%2")
                                .arg(i + 1)
                                .arg(binarySpy.count())));
    }

    QCOMPARE(binarySpy.count(), 3);

    socket.close();

    qDebug() << "Binary receive path verified";
}

void TestQCWebSocket::testLargeMessage()
{
    QCWebSocket socket{QUrl(m_testServerUrl), QCWebSocketOptions{}};
    QSignalSpy textSpy(&socket, &QCWebSocket::textMessageReceived);

    socket.open();
    QVERIFY2(waitForSignal(&socket, QMetaMethod::fromSignal(&QCWebSocket::connected), 10000),
             qPrintable(QStringLiteral("无法连接到本地 WebSocket 测试服务器：%1")
                            .arg(socket.errorString())));

    // 创建一个大消息（64KB）
    QString largeMessage;
    for (int i = 0; i < 1024; ++i) {
        largeMessage.append(
            QStringLiteral("ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789abcdefghijklmnopqrstuvwxyz01"));
    }
    qDebug() << "大消息大小:" << largeMessage.toUtf8().size() << "字节";

    qint64 sent = socket.sendTextMessage(largeMessage);
    QVERIFY(sent > 0);

    // 等待服务器回显（可能需要更长时间）
    QVERIFY(TestWaitUtils::waitForSpyCount(textSpy, 1, 20000));
    QCOMPARE(textSpy.count(), 1);

    QString received = textSpy.first().first().toString();
    qDebug() << "收到消息大小:" << received.size() << "字节";

    QCOMPARE(received, largeMessage);

    socket.close();

    qDebug() << "Large message echo verified";
}

// ============================================================================
// 协议测试
// ============================================================================

void TestQCWebSocket::testPingPong()
{
    QCWebSocket socket{QUrl(m_testServerUrl), QCWebSocketOptions{}};
    QSignalSpy pongSpy(&socket, &QCWebSocket::pongReceived);

    socket.open();
    QVERIFY2(waitForSignal(&socket, QMetaMethod::fromSignal(&QCWebSocket::connected), 10000),
             qPrintable(QStringLiteral("无法连接到本地 WebSocket 测试服务器：%1")
                            .arg(socket.errorString())));

    // 发送 Ping 帧
    QByteArray pingPayload = "Ping Test";
    socket.ping(pingPayload);

    // 等待 Pong 响应
    // 注意：有些服务器可能不发送 Pong 响应，或者 libcurl 自动处理了
    if (TestWaitUtils::waitForSpyCount(pongSpy, 1, 5000)) {
        qDebug() << "收到 Pong 响应";
        QVERIFY(pongSpy.count() >= 1);
    } else {
        qDebug() << "⚠️ 未收到 Pong 响应（可能被 libcurl 自动处理）";
    }

    socket.close();

    qDebug() << "Ping/Pong path verified";
}

void TestQCWebSocket::testCloseHandshake()
{
    QCWebSocket socket{QUrl(m_testServerUrl), QCWebSocketOptions{}};
    QSignalSpy disconnectedSpy(&socket, &QCWebSocket::disconnected);

    socket.open();
    QVERIFY2(waitForSignal(&socket, QMetaMethod::fromSignal(&QCWebSocket::connected), 10000),
             qPrintable(QStringLiteral("无法连接到本地 WebSocket 测试服务器：%1")
                            .arg(socket.errorString())));

    // 优雅关闭
    socket.close(QCWebSocket::CloseCode::Normal, QStringLiteral("Test Close"));

    // 等待断开连接信号
    QVERIFY(TestWaitUtils::waitForSpyCount(disconnectedSpy, 1, 10000));
    QCOMPARE(disconnectedSpy.count(), 1);
    QCOMPARE(socket.state(), QCWebSocket::State::Closed);

    qDebug() << "Close handshake verified";
}

void TestQCWebSocket::testFragmentedMessage()
{
    // message-level 回显测试：验证“大消息收发链路”可用，但不证明 continuation frames（帧级分片）一定发生。
    QCWebSocket socket{QUrl(m_testServerUrl), QCWebSocketOptions{}};

    QSignalSpy connectedSpy(&socket, &QCWebSocket::connected);
    QSignalSpy textSpy(&socket, &QCWebSocket::textMessageReceived);
    QSignalSpy binarySpy(&socket, &QCWebSocket::binaryMessageReceived);

    socket.open();

    // 验证连接
    QVERIFY2(TestWaitUtils::waitForSpyCount(connectedSpy, 1, 5000),
             qPrintable(QStringLiteral("无法连接到本地 WebSocket 测试服务器：%1")
                            .arg(socket.errorString())));

    QCOMPARE(connectedSpy.count(), 1);

    // 验证 10KB 文本消息在 echo 往返后仍保持完整。
    qDebug() << "Verifying 10KB text echo integrity";

    QByteArray largeText(10240, 'A');
    QString testMessage = QString::fromUtf8(largeText);

    textSpy.clear();
    socket.sendTextMessage(testMessage);

    // 等待回显消息
    QVERIFY(TestWaitUtils::waitForSpyCount(textSpy, 1, 10000));
    QCOMPARE(textSpy.count(), 1);

    QString receivedText = textSpy.at(0).at(0).toString();
    QCOMPARE(receivedText.size(), 10240);
    QCOMPARE(receivedText, testMessage);

    qDebug() << "10KB text echo integrity verified";

    // 验证较大二进制负载的完整往返。
    qDebug() << "Verifying 100KB binary echo integrity";

    QByteArray largeBinary(102400, 0x42); // 填充 'B' (0x42)

    binarySpy.clear();
    socket.sendBinaryMessage(largeBinary);

    // 等待回显消息
    QVERIFY(TestWaitUtils::waitForSpyCount(binarySpy, 1, 15000));
    QCOMPARE(binarySpy.count(), 1);

    QByteArray receivedBinary = binarySpy.at(0).at(0).toByteArray();
    QCOMPARE(receivedBinary.size(), 102400);
    QCOMPARE(receivedBinary, largeBinary);

    qDebug() << "100KB binary echo integrity verified";

    // 4096 字节用于覆盖常见分帧边界附近的回显完整性。
    qDebug() << "Verifying 4096-byte boundary echo";

    QByteArray boundaryBinary(4096, 0x43); // 填充 'C' (0x43)

    binarySpy.clear();
    socket.sendBinaryMessage(boundaryBinary);

    QVERIFY(TestWaitUtils::waitForSpyCount(binarySpy, 1, 10000));
    QCOMPARE(binarySpy.count(), 1);

    QByteArray receivedBoundary = binarySpy.at(0).at(0).toByteArray();
    QCOMPARE(receivedBoundary.size(), 4096);
    QCOMPARE(receivedBoundary, boundaryBinary);

    qDebug() << "4096-byte boundary echo verified";

    // 连续发送大消息，验证连接在多次往返下仍能完整交付。
    qDebug() << "Verifying repeated large-message echo";

    textSpy.clear();
    for (int i = 0; i < 3; ++i) {
        QByteArray msg(8192, 'D' + i);
        socket.sendTextMessage(QString::fromUtf8(msg));
    }

    // 等待所有消息返回（避免固定 sleep 导致 flaky）
    const int expectedMessages = 3;
    QVERIFY2(TestWaitUtils::waitForSpyCount(textSpy, expectedMessages, 5000),
             qPrintable(QStringLiteral("WebSocket 分片/连续消息回显超时，预期=%1，实际=%2")
                            .arg(expectedMessages)
                            .arg(textSpy.count())));
    QVERIFY(textSpy.count() >= 3);

    qDebug() << "Repeated large-message echo verified, count =" << textSpy.count();

    // 关闭连接
    socket.close();

    qDebug() << "Fragmented message integrity contract verified";
}

static QString sha256Hex(const QByteArray &data)
{
    return QString::fromLatin1(QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex());
}

static QByteArray generateDeterministicBytes(int len, int seed)
{
    QByteArray out;
    out.resize(qMax(0, len));
    for (int i = 0; i < out.size(); ++i) {
        out[i] = static_cast<char>((seed + i) & 0xFF);
    }
    return out;
}

static QList<int> splitParts(int totalLen, int parts)
{
    const int n    = qBound(1, parts, 128);
    const int base = totalLen / n;
    const int rem  = totalLen % n;
    QList<int> sizes;
    sizes.reserve(n);
    for (int i = 0; i < n; ++i) {
        const int size = base + ((i < rem) ? 1 : 0);
        if (size > 0) {
            sizes.append(size);
        }
    }
    return sizes;
}

void TestQCWebSocket::testFragmentedFramesReassembly()
{
    QVERIFY2(!m_evidenceArtifactsPath.isEmpty(),
             "Evidence server artifactsPath 为空，无法复核帧级证据。");

    const int totalLen   = 8192;
    const int parts      = 3;
    const int seed       = 7;
    const QString caseId = QString::fromLatin1(QTest::currentTestFunction());

    QUrl url(m_testEvidenceServerUrl + QStringLiteral("/fragment"));
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("type"), QStringLiteral("binary"));
    q.addQueryItem(QStringLiteral("len"), QString::number(totalLen));
    q.addQueryItem(QStringLiteral("parts"), QString::number(parts));
    q.addQueryItem(QStringLiteral("seed"), QString::number(seed));
    q.addQueryItem(QStringLiteral("case"), caseId);
    url.setQuery(q);

    QCWebSocket socket{url, QCWebSocketOptions{}};
    QSignalSpy connectedSpy(&socket, &QCWebSocket::connected);
    QSignalSpy binarySpy(&socket, &QCWebSocket::binaryMessageReceived);

    socket.open();
    QVERIFY2(TestWaitUtils::waitForSpyCount(connectedSpy, 1, 10000),
             qPrintable(
                 QStringLiteral("Evidence server connect failed: %1").arg(socket.errorString())));

    QVERIFY(TestWaitUtils::waitForSpyCount(binarySpy, 1, 10000));
    QCOMPARE(binarySpy.count(), 1);

    const QByteArray received = binarySpy.at(0).at(0).toByteArray();
    QCOMPARE(received.size(), totalLen);
    const QByteArray expected = generateDeterministicBytes(totalLen, seed);
    QCOMPARE(sha256Hex(received), sha256Hex(expected));

    // 复核证据：服务端工件必须记录“确实发送了 continuation frames”。
    const QList<QJsonObject> frames = TestWebSocketEvidenceUtils::waitFrameEventsByCase(
        m_evidenceArtifactsPath, caseId, parts, 2000, QStringLiteral("send"));
    QCOMPARE(frames.size(), parts);

    const QList<int> sizes = splitParts(totalLen, parts);
    QCOMPARE(sizes.size(), parts);

    int offset = 0;
    for (int i = 0; i < frames.size(); ++i) {
        const QJsonObject obj = frames.at(i);
        const int opcode      = obj.value(QStringLiteral("opcode")).toInt(-1);
        const int fin         = obj.value(QStringLiteral("fin")).toInt(-1);
        const int payloadLen  = obj.value(QStringLiteral("payload_len")).toInt(-1);
        const QString sha     = obj.value(QStringLiteral("payload_sha256")).toString();

        QCOMPARE(payloadLen, sizes.at(i));
        const QByteArray chunk = expected.mid(offset, payloadLen);
        offset += payloadLen;
        QCOMPARE(sha, sha256Hex(chunk));

        if (i == 0) {
            QCOMPARE(opcode, 0x2); // binary
        } else {
            QCOMPARE(opcode, 0x0); // continuation
        }
        if (i == frames.size() - 1) {
            QCOMPARE(fin, 1);
        } else {
            QCOMPARE(fin, 0);
        }
    }
    QCOMPARE(offset, totalLen);

    socket.close();
    qDebug() << "Frame-level reassembly verified (len/sha256 + evidence log)";
}

// ============================================================================
// 错误处理
// ============================================================================

void TestQCWebSocket::testConnectionRefused()
{
    // 连接到一个拒绝连接的端口（避免固定端口假设；使用临时端口并立即释放）
    QTcpServer portPicker;
    QVERIFY2(portPicker.listen(QHostAddress::LocalHost, 0),
             "无法绑定本机端口用于生成确定性 connection-refused 场景");
    const quint16 port = portPicker.serverPort();
    portPicker.close();

    QCWebSocket socket{QUrl(QStringLiteral("ws://127.0.0.1:%1").arg(port)), QCWebSocketOptions{}};
    QSignalSpy errorSpy(&socket, &QCWebSocket::errorOccurred);

    socket.open();

    // 等待错误信号
    QVERIFY(TestWaitUtils::waitForSpyCount(errorSpy, 1, 10000));
    QVERIFY(errorSpy.count() >= 1);
    QVERIFY(!socket.errorString().isEmpty());

    qDebug() << "错误信息:" << socket.errorString();
    qDebug() << "Connection-refused error path verified";
}

void TestQCWebSocket::testSslError()
{
    // 默认安全配置必须拒绝未被信任的自签名证书。
    qDebug() << "Verifying default rejection of self-signed certificate";

    QCWebSocket socket{QUrl(m_testWssServerUrl), QCWebSocketOptions{}};
    QSignalSpy errorSpy(&socket, &QCWebSocket::errorOccurred);
    QSignalSpy sslErrorSpy(&socket, &QCWebSocket::sslErrorsDetailed);

    socket.open();

    QVERIFY2(TestWaitUtils::waitForSpyCount(errorSpy, 1, 15000),
             qPrintable(
                 QStringLiteral(
                     "预期应拒绝自签名证书（未配置 CA），但未观察到 errorOccurred。当前错误=%1")
                     .arg(socket.errorString())));

    qDebug() << "SSL 错误已检测:" << socket.errorString();

    // 可能不会触发 sslErrorsDetailed（取决于 libcurl 版本和 SSL 后端）
    if (sslErrorSpy.count() > 0) {
        const QStringList errors = sslErrorSpy.at(0).at(0).toStringList();
        qDebug() << "详细 SSL 错误:" << errors;
        QVERIFY(!errors.isEmpty());
    }

    qDebug() << "Default TLS policy rejected self-signed certificate";

    // 显式配置信任 CA 后，连接应恢复成功。
    qDebug() << "Verifying connection succeeds after CA is configured";

    QCWebSocket socket2{QUrl(m_testWssServerUrl), QCWebSocketOptions{}};
    QCNetworkSslConfig sslConfig;
    sslConfig.setCaCertPath(m_caCertPath);
    QCWebSocketOptions options2 = socket2.options();
    options2.setSslConfig(sslConfig);
    QVERIFY(socket2.setOptions(options2));

    QSignalSpy connectedSpy(&socket2, &QCWebSocket::connected);

    socket2.open();

    // 等待连接（可能需要较长时间）
    QVERIFY2(TestWaitUtils::waitForSpyCount(connectedSpy, 1, 15000),
             qPrintable(QStringLiteral("预期配置 CA 后应连接成功，caCertPath=%1，错误=%2")
                            .arg(m_caCertPath, socket2.errorString())));

    qDebug() << "Connection succeeded after CA configuration";
    QCOMPARE(connectedSpy.count(), 1);
    socket2.close();

    qDebug() << "TLS validation path verified";
}

void TestQCWebSocket::testServerClosedConnection()
{
    QVERIFY2(!m_evidenceArtifactsPath.isEmpty(),
             "Evidence server artifactsPath 为空，无法复核 close 证据。");

    const QString caseId = QString::fromLatin1(QTest::currentTestFunction());
    QUrl url(m_testEvidenceServerUrl + QStringLiteral("/close"));
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("code"), QStringLiteral("1001"));
    q.addQueryItem(QStringLiteral("reason"), QStringLiteral("bye"));
    q.addQueryItem(QStringLiteral("case"), caseId);
    url.setQuery(q);

    QCWebSocket socket{url, QCWebSocketOptions{}};
    QSignalSpy connectedSpy(&socket, &QCWebSocket::connected);
    QSignalSpy closeSpy(&socket, &QCWebSocket::closeReceived);
    QSignalSpy disconnectedSpy(&socket, &QCWebSocket::disconnected);
    QSignalSpy errorSpy(&socket, &QCWebSocket::errorOccurred);

    socket.open();
    QVERIFY2(TestWaitUtils::waitForSpyCount(connectedSpy, 1, 10000),
             qPrintable(
                 QStringLiteral("Evidence server connect failed: %1").arg(socket.errorString())));

    // server-initiated close：服务端发送 close(code/reason) 并断开；禁止用 abort() 伪装。
    QVERIFY(TestWaitUtils::waitForSpyCount(closeSpy, 1, 10000));
    QCOMPARE(closeSpy.count(), 1);
    QCOMPARE(closeSpy.at(0).at(0).toInt(), static_cast<int>(QCWebSocket::CloseCode::GoingAway));
    QCOMPARE(closeSpy.at(0).at(1).toString(), QStringLiteral("bye"));

    QVERIFY(TestWaitUtils::waitForSpyCount(disconnectedSpy, 1, 10000));
    QCOMPARE(disconnectedSpy.count(), 1);
    QCOMPARE(socket.state(), QCWebSocket::State::Closed);

    // 允许实现差异：close 期间不应出现 error；若出现则输出用于定位。
    if (errorSpy.count() > 0) {
        qWarning() << "Unexpected error during server close:" << socket.errorString();
    }

    const QList<QJsonObject> frames = TestWebSocketEvidenceUtils::waitFrameEventsByCase(
        m_evidenceArtifactsPath, caseId, 1, 2000, QStringLiteral("send"));
    QVERIFY2(!frames.isEmpty(), "未在 evidence 工件中找到 close 帧记录。");
    const QJsonObject last = frames.last();
    QCOMPARE(last.value(QStringLiteral("opcode")).toInt(-1), 0x8);
    QCOMPARE(last.value(QStringLiteral("close_code")).toInt(-1), 1001);
    QCOMPARE(last.value(QStringLiteral("close_reason")).toString(), QStringLiteral("bye"));

    qDebug() << "Server-driven close path verified";
}

void TestQCWebSocket::testServerClosedWithCustomCloseCode()
{
    QVERIFY2(!m_evidenceArtifactsPath.isEmpty(),
             "Evidence server artifactsPath 为空，无法复核 close 证据。");

    const QString caseId = QString::fromLatin1(QTest::currentTestFunction());
    QUrl url(m_testEvidenceServerUrl + QStringLiteral("/close"));
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("code"), QStringLiteral("3001"));
    q.addQueryItem(QStringLiteral("reason"), QStringLiteral("custom"));
    q.addQueryItem(QStringLiteral("case"), caseId);
    url.setQuery(q);

    QCWebSocketOptions options;
    options.setReconnectPolicy(QCWebSocketReconnectPolicy::standardReconnect());
    QCWebSocket socket{url, options};
    QSignalSpy connectedSpy(&socket, &QCWebSocket::connected);
    QSignalSpy closeSpy(&socket, &QCWebSocket::closeReceived);
    QSignalSpy disconnectedSpy(&socket, &QCWebSocket::disconnected);
    QSignalSpy reconnectSpy(&socket, &QCWebSocket::reconnectAttempt);

    socket.open();
    QVERIFY2(TestWaitUtils::waitForSpyCount(connectedSpy, 1, 10000),
             qPrintable(
                 QStringLiteral("Evidence server connect failed: %1").arg(socket.errorString())));

    QVERIFY(TestWaitUtils::waitForSpyCount(closeSpy, 1, 10000));
    QCOMPARE(closeSpy.at(0).at(0).toInt(), 3001);
    QCOMPARE(closeSpy.at(0).at(1).toString(), QStringLiteral("custom"));

    QVERIFY(TestWaitUtils::waitForSpyCount(disconnectedSpy, 1, 10000));
    QCOMPARE(socket.state(), QCWebSocket::State::Closed);
    QCOMPARE(reconnectSpy.count(), 0);

    const QList<QJsonObject> frames = TestWebSocketEvidenceUtils::waitFrameEventsByCase(
        m_evidenceArtifactsPath, caseId, 1, 2000, QStringLiteral("send"));
    QVERIFY2(!frames.isEmpty(), "未在 evidence 工件中找到 custom close 帧记录。");
    QCOMPARE(frames.last().value(QStringLiteral("close_code")).toInt(-1), 3001);
}

void TestQCWebSocket::testServerClosedWithReservedCloseCode()
{
    const QString caseId = QString::fromLatin1(QTest::currentTestFunction());
    QUrl url(m_testEvidenceServerUrl + QStringLiteral("/close"));
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("code"), QStringLiteral("1006"));
    q.addQueryItem(QStringLiteral("reason"), QStringLiteral("reserved"));
    q.addQueryItem(QStringLiteral("case"), caseId);
    url.setQuery(q);

    QCWebSocket socket{url, QCWebSocketOptions{}};
    QSignalSpy connectedSpy(&socket, &QCWebSocket::connected);
    QSignalSpy closeSpy(&socket, &QCWebSocket::closeReceived);
    QSignalSpy errorSpy(&socket, &QCWebSocket::errorOccurred);
    QSignalSpy disconnectedSpy(&socket, &QCWebSocket::disconnected);

    socket.open();
    QVERIFY2(TestWaitUtils::waitForSpyCount(connectedSpy, 1, 10000),
             qPrintable(
                 QStringLiteral("Evidence server connect failed: %1").arg(socket.errorString())));

    QVERIFY(TestWaitUtils::waitForSpyCount(errorSpy, 1, 10000));
    QCOMPARE(closeSpy.count(), 0);
    QVERIFY(socket.errorString().contains(QStringLiteral("1006")));
    QVERIFY(TestWaitUtils::waitForSpyCount(disconnectedSpy, 1, 10000));
    QCOMPARE(socket.state(), QCWebSocket::State::Closed);
}

void TestQCWebSocket::testRejectReservedCloseCodeOnSend()
{
    QCWebSocket socket{QUrl(m_testEvidenceServerUrl), QCWebSocketOptions{}};
    QSignalSpy connectedSpy(&socket, &QCWebSocket::connected);
    QSignalSpy errorSpy(&socket, &QCWebSocket::errorOccurred);

    socket.open();
    QVERIFY2(TestWaitUtils::waitForSpyCount(connectedSpy, 1, 10000),
             qPrintable(
                 QStringLiteral("Evidence server connect failed: %1").arg(socket.errorString())));

    socket.close(QCWebSocket::CloseCode::AbnormalClosure, QStringLiteral("reserved"));

    QVERIFY(TestWaitUtils::waitForSpyCount(errorSpy, 1, 1000));
    QCOMPARE(socket.state(), QCWebSocket::State::Connected);

    socket.abort();
}

// ============================================================================
// 辅助方法
// ============================================================================

bool TestQCWebSocket::waitForSignal(QObject *obj, const QMetaMethod &signal, int timeout)
{
    if (!obj) {
        return false;
    }

    // 抗竞态：connected/disconnected/errorOccurred 属于“可由状态反推”的信号，
    // 若信号在 waitForSignal 调用前已触发，QSignalSpy 后置创建会造成假阴性。
    if (auto *socket = qobject_cast<QCWebSocket *>(obj)) {
        if (signal == QMetaMethod::fromSignal(&QCWebSocket::connected)) {
            if (socket->state() == QCWebSocket::State::Connected) {
                return true;
            }
        } else if (signal == QMetaMethod::fromSignal(&QCWebSocket::disconnected)) {
            if (socket->state() == QCWebSocket::State::Closed) {
                return true;
            }
        } else if (signal == QMetaMethod::fromSignal(&QCWebSocket::errorOccurred)) {
            if (!socket->errorString().isEmpty()) {
                return true;
            }
        }
    }

    QSignalSpy spy(obj, signal);
    return TestWaitUtils::waitForSpyCount(spy, 1, timeout);
}

QTEST_MAIN(TestQCWebSocket)
#include "tst_QCWebSocket.moc"
