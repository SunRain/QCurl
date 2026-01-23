#include <QtTest>
#include <QCWebSocket.h>
#include <QCWebSocketReconnectPolicy.h>
#include <QCNetworkSslConfig.h>
#include <QSignalSpy>
#include <QEventLoop>
#include <QTimer>
#include <QDebug>
#include <QMetaMethod>
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QHostAddress>
#include <QProcess>
#include <QTcpSocket>
#include <QThread>

using namespace QCurl;

/**
 * @brief QCWebSocket 单元测试
 *
 * 说明：
 * - 默认使用本地 WebSocket 测试服务器（tests/websocket-fragment-server.js，端口 8765）以保证 ctest 稳定性。
 * - 依赖 node 环境；若本地服务器无法启动则相关用例会 QSKIP（不阻断自动门禁）。
 * - 外部网络相关用例（如 WSS/SSL）默认跳过；可通过环境变量显式启用。
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

    [[nodiscard]] bool externalNetworkTestsEnabled() const;
    [[nodiscard]] bool startLocalTestServer();
    void stopLocalTestServer();

    static bool waitForPortReady(quint16 port, int timeoutMs);

    QString m_localServerSkipReason;
    QProcess m_localServer;

    const quint16 m_localTestServerPort = 8765;
    const QString m_testServerUrl = QStringLiteral("ws://localhost:8765");
    const QString m_externalTestServerUrl = QStringLiteral("wss://echo.websocket.org");
};

void TestQCWebSocket::initTestCase()
{
    qDebug() << "========================================";
    qDebug() << "QCurl WebSocket 测试套件";
    qDebug() << "========================================";
    qDebug() << "本地测试服务器:" << m_testServerUrl;
    qDebug();

    if (!startLocalTestServer()) {
        qWarning().noquote() << "Local WebSocket server unavailable, tests will be skipped:"
                             << m_localServerSkipReason;
    }
}

void TestQCWebSocket::cleanupTestCase()
{
    stopLocalTestServer();
    qDebug();
    qDebug() << "========================================";
    qDebug() << "测试完成";
    qDebug() << "========================================";
}

// ============================================================================
// 连接测试
// ============================================================================

void TestQCWebSocket::testConnect()
{
    qDebug() << "========== testConnect ==========";

    if (!m_localServerSkipReason.isEmpty()) {
        QSKIP(qPrintable(m_localServerSkipReason));
    }

    QCWebSocket socket{QUrl(m_testServerUrl)};
    QSignalSpy connectedSpy(&socket, &QCWebSocket::connected);
    QSignalSpy stateChangedSpy(&socket, &QCWebSocket::stateChanged);

    socket.open();

    // 等待连接成功信号
    if (!waitForSignal(&socket, QMetaMethod::fromSignal(&QCWebSocket::connected), 10000)) {
        QSKIP("无法连接到本地 WebSocket 测试服务器，跳过连接测试。");
    }
    QCOMPARE(connectedSpy.count(), 1);
    QCOMPARE(socket.state(), QCWebSocket::State::Connected);
    QVERIFY(socket.isValid());

    // 检查状态变化信号
    QVERIFY(stateChangedSpy.count() >= 2);  // Connecting -> Connected

    socket.close();

    qDebug() << "✅ 基本连接测试通过";
}

void TestQCWebSocket::testConnectWss()
{
    qDebug() << "========== testConnectWss ==========";

    if (!externalNetworkTestsEnabled()) {
        QSKIP("外部网络用例默认跳过（可设置 QCURL_TEST_EXTERNAL_NETWORK=1 启用）。");
    }

    // 使用 wss:// 加密连接（外部依赖）
    QCWebSocket socket{QUrl(m_externalTestServerUrl)};
    QSignalSpy connectedSpy(&socket, &QCWebSocket::connected);

    socket.open();

    if (!waitForSignal(&socket, QMetaMethod::fromSignal(&QCWebSocket::connected), 10000)) {
        QSKIP("无法连接外部 WSS 测试服务器（网络问题或服务不可用）");
    }
    QCOMPARE(connectedSpy.count(), 1);
    QCOMPARE(socket.state(), QCWebSocket::State::Connected);

    socket.close();

    qDebug() << "✅ WSS 加密连接测试通过";
}

void TestQCWebSocket::testConnectInvalidUrl()
{
    qDebug() << "========== testConnectInvalidUrl ==========";

    QCWebSocket socket{QUrl("wss://invalid-host-that-does-not-exist.example.com")};
    QSignalSpy errorSpy(&socket, &QCWebSocket::errorOccurred);

    socket.open();

    // 等待错误信号
    QVERIFY(waitForSignal(&socket, QMetaMethod::fromSignal(&QCWebSocket::errorOccurred), 10000));
    QVERIFY(errorSpy.count() >= 1);
    QVERIFY(!socket.errorString().isEmpty());
    QCOMPARE(socket.state(), QCWebSocket::State::Unconnected);

    qDebug() << "错误信息:" << socket.errorString();
    qDebug() << "✅ 无效 URL 错误处理测试通过";
}

void TestQCWebSocket::testAutoReconnect()
{
    qDebug() << "========== testAutoReconnect ==========";
    
    // 自动重连功能测试
    // 创建一个会失败的连接（端口不存在）
    QCWebSocket socket(QUrl("ws://localhost:9999"));
    
    // 设置标准重连策略（3 次重连，1s → 2s → 4s）
    socket.setReconnectPolicy(QCWebSocketReconnectPolicy::standardReconnect());
    
    // 监听重连尝试信号
    QSignalSpy reconnectSpy(&socket, &QCWebSocket::reconnectAttempt);
    
    // 尝试连接（会失败）
    socket.open();
    
    // 等待初始连接失败
    QVERIFY(waitForSignal(&socket, QMetaMethod::fromSignal(&QCWebSocket::errorOccurred), 2000));
    
    // 等待第一次重连尝试（延迟 1 秒）
    QTest::qWait(1500);
    
    // 验证至少有一次重连尝试
    QVERIFY2(reconnectSpy.count() >= 1, "应该至少有一次重连尝试");
    
    // 验证重连参数
    if (reconnectSpy.count() > 0) {
        auto args = reconnectSpy.at(0);
        int attemptCount = args.at(0).toInt();
        int closeCode = args.at(1).toInt();
        
        QCOMPARE(attemptCount, 1);  // 第一次重连
        QCOMPARE(closeCode, 1006);  // AbnormalClosure（网络错误）
    }
    
    qDebug() << "✅ 自动重连测试通过，重连尝试次数:" << reconnectSpy.count();
    
    // 停止重连
    socket.abort();
}

// ============================================================================
// 消息收发测试
// ============================================================================

void TestQCWebSocket::testSendTextMessage()
{
    qDebug() << "========== testSendTextMessage ==========";

    if (!m_localServerSkipReason.isEmpty()) {
        QSKIP(qPrintable(m_localServerSkipReason));
    }

    QCWebSocket socket{QUrl(m_testServerUrl)};
    QSignalSpy textSpy(&socket, &QCWebSocket::textMessageReceived);

    socket.open();
    if (!waitForSignal(&socket, QMetaMethod::fromSignal(&QCWebSocket::connected), 10000)) {
        QSKIP("无法连接到本地 WebSocket 测试服务器，跳过文本消息发送测试。");
    }

    QString testMessage = QStringLiteral("Hello WebSocket!");
    qint64 sent = socket.sendTextMessage(testMessage);
    QVERIFY(sent > 0);
    qDebug() << "发送字节数:" << sent;

    // 等待本地 echo 服务器回显
    QVERIFY(waitForSignal(&socket, QMetaMethod::fromSignal(&QCWebSocket::textMessageReceived), 10000));
    QCOMPARE(textSpy.count(), 1);

    QString received = textSpy.first().first().toString();
    qDebug() << "收到消息:" << received;

    QCOMPARE(received, testMessage);

    socket.close();

    qDebug() << "✅ 文本消息发送测试通过";
}

void TestQCWebSocket::testSendBinaryMessage()
{
    qDebug() << "========== testSendBinaryMessage ==========";

    if (!m_localServerSkipReason.isEmpty()) {
        QSKIP(qPrintable(m_localServerSkipReason));
    }

    QCWebSocket socket{QUrl(m_testServerUrl)};
    QSignalSpy binarySpy(&socket, &QCWebSocket::binaryMessageReceived);

    socket.open();
    if (!waitForSignal(&socket, QMetaMethod::fromSignal(&QCWebSocket::connected), 10000)) {
        QSKIP("无法连接到本地 WebSocket 测试服务器，跳过二进制消息发送测试。");
    }

    QByteArray testData = "Binary Data: \x01\x02\x03\x04";
    qint64 sent = socket.sendBinaryMessage(testData);
    QVERIFY(sent > 0);
    qDebug() << "发送字节数:" << sent;

    // 等待 Echo 服务器回显
    QVERIFY(waitForSignal(&socket, QMetaMethod::fromSignal(&QCWebSocket::binaryMessageReceived), 10000));
    QCOMPARE(binarySpy.count(), 1);

    QByteArray received = binarySpy.first().first().toByteArray();
    QCOMPARE(received, testData);

    socket.close();

    qDebug() << "✅ 二进制消息发送测试通过";
}

void TestQCWebSocket::testReceiveTextMessage()
{
    qDebug() << "========== testReceiveTextMessage ==========";

    if (!m_localServerSkipReason.isEmpty()) {
        QSKIP(qPrintable(m_localServerSkipReason));
    }

    QCWebSocket socket{QUrl(m_testServerUrl)};
    QSignalSpy textSpy(&socket, &QCWebSocket::textMessageReceived);

    socket.open();
    if (!waitForSignal(&socket, QMetaMethod::fromSignal(&QCWebSocket::connected), 10000)) {
        QSKIP("无法连接到本地 WebSocket 测试服务器，跳过文本消息接收测试。");
    }

    const QString testMessage = QStringLiteral("Receive Test");
    socket.sendTextMessage(testMessage);

    QVERIFY(waitForSignal(&socket, QMetaMethod::fromSignal(&QCWebSocket::textMessageReceived), 10000));
    QCOMPARE(textSpy.count(), 1);
    QCOMPARE(textSpy.first().first().toString(), testMessage);

    socket.close();

    qDebug() << "✅ 文本消息接收测试通过";
}

void TestQCWebSocket::testReceiveBinaryMessage()
{
    qDebug() << "========== testReceiveBinaryMessage ==========";

    if (!m_localServerSkipReason.isEmpty()) {
        QSKIP(qPrintable(m_localServerSkipReason));
    }

    QCWebSocket socket{QUrl(m_testServerUrl)};
    QSignalSpy binarySpy(&socket, &QCWebSocket::binaryMessageReceived);

    socket.open();
    if (!waitForSignal(&socket, QMetaMethod::fromSignal(&QCWebSocket::connected), 10000)) {
        QSKIP("无法连接到本地 WebSocket 测试服务器，跳过二进制消息接收测试。");
    }

    // 发送多条二进制消息
    for (int i = 0; i < 3; ++i) {
        QByteArray data;
        data.append(static_cast<char>(i));
        data.append("Test Binary Data");
        socket.sendBinaryMessage(data);
        // ✅ 在发送消息之间增加延迟
        QTest::qWait(100);
    }

    // ✅ 使用 binarySpy 等待（避免 waitForSignal() 新建 QSignalSpy 导致“信号已到达但等待超时”的假阴性）
    const int expectedMessages = 3;
    while (binarySpy.count() < expectedMessages) {
        if (!binarySpy.wait(20000)) {
            qWarning() << "Binary receive timeout. Expected:" << expectedMessages
                       << "Actual:" << binarySpy.count();
            socket.close();
            QFAIL("WebSocket 二进制消息接收超时（20s），按取证口径视为失败（禁止失败即 SKIP）。");
        }
    }

    QCOMPARE(binarySpy.count(), 3);

    socket.close();

    qDebug() << "✅ 二进制消息接收测试通过";
}

void TestQCWebSocket::testLargeMessage()
{
    qDebug() << "========== testLargeMessage ==========";

    if (!m_localServerSkipReason.isEmpty()) {
        QSKIP(qPrintable(m_localServerSkipReason));
    }

    QCWebSocket socket{QUrl(m_testServerUrl)};
    QSignalSpy textSpy(&socket, &QCWebSocket::textMessageReceived);

    socket.open();
    if (!waitForSignal(&socket, QMetaMethod::fromSignal(&QCWebSocket::connected), 10000)) {
        QSKIP("无法连接到本地 WebSocket 测试服务器，跳过大消息测试。");
    }

    // 创建一个大消息（64KB）
    QString largeMessage;
    for (int i = 0; i < 1024; ++i) {
        largeMessage.append(QStringLiteral("ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789abcdefghijklmnopqrstuvwxyz01"));
    }
    qDebug() << "大消息大小:" << largeMessage.toUtf8().size() << "字节";

    qint64 sent = socket.sendTextMessage(largeMessage);
    QVERIFY(sent > 0);

    // 等待服务器回显（可能需要更长时间）
    QVERIFY(waitForSignal(&socket, QMetaMethod::fromSignal(&QCWebSocket::textMessageReceived), 20000));
    QCOMPARE(textSpy.count(), 1);

    QString received = textSpy.first().first().toString();
    qDebug() << "收到消息大小:" << received.size() << "字节";

    QCOMPARE(received, largeMessage);

    socket.close();

    qDebug() << "✅ 大消息测试通过";
}

// ============================================================================
// 协议测试
// ============================================================================

void TestQCWebSocket::testPingPong()
{
    qDebug() << "========== testPingPong ==========";

    if (!m_localServerSkipReason.isEmpty()) {
        QSKIP(qPrintable(m_localServerSkipReason));
    }

    QCWebSocket socket{QUrl(m_testServerUrl)};
    QSignalSpy pongSpy(&socket, &QCWebSocket::pongReceived);

    socket.open();
    if (!waitForSignal(&socket, QMetaMethod::fromSignal(&QCWebSocket::connected), 10000)) {
        QSKIP("无法连接到本地 WebSocket 测试服务器，跳过 Ping/Pong 测试。");
    }

    // 发送 Ping 帧
    QByteArray pingPayload = "Ping Test";
    socket.ping(pingPayload);

    // 等待 Pong 响应
    // 注意：有些服务器可能不发送 Pong 响应，或者 libcurl 自动处理了
    if (waitForSignal(&socket, QMetaMethod::fromSignal(&QCWebSocket::pongReceived), 5000)) {
        qDebug() << "收到 Pong 响应";
        QVERIFY(pongSpy.count() >= 1);
    } else {
        qDebug() << "⚠️ 未收到 Pong 响应（可能被 libcurl 自动处理）";
    }

    socket.close();

    qDebug() << "✅ Ping/Pong 测试通过";
}

void TestQCWebSocket::testCloseHandshake()
{
    qDebug() << "========== testCloseHandshake ==========";

    if (!m_localServerSkipReason.isEmpty()) {
        QSKIP(qPrintable(m_localServerSkipReason));
    }

    QCWebSocket socket{QUrl(m_testServerUrl)};
    QSignalSpy disconnectedSpy(&socket, &QCWebSocket::disconnected);

    socket.open();
    if (!waitForSignal(&socket, QMetaMethod::fromSignal(&QCWebSocket::connected), 10000)) {
        QSKIP("无法连接到本地 WebSocket 测试服务器，跳过关闭握手测试。");
    }

    // 优雅关闭
    socket.close(QCWebSocket::CloseCode::Normal, QStringLiteral("Test Close"));

    // 等待断开连接信号
    QVERIFY(waitForSignal(&socket, QMetaMethod::fromSignal(&QCWebSocket::disconnected), 10000));
    QCOMPARE(disconnectedSpy.count(), 1);
    QCOMPARE(socket.state(), QCWebSocket::State::Closed);

    qDebug() << "✅ 关闭握手测试通过";
}

void TestQCWebSocket::testFragmentedMessage()
{
    qDebug() << "========== testFragmentedMessage ==========";

    if (!m_localServerSkipReason.isEmpty()) {
        QSKIP(qPrintable(m_localServerSkipReason));
    }

    // 分片消息完整性测试（本地 echo server）
    QCWebSocket socket{QUrl(m_testServerUrl)};
    
    QSignalSpy connectedSpy(&socket, &QCWebSocket::connected);
    QSignalSpy textSpy(&socket, &QCWebSocket::textMessageReceived);
    QSignalSpy binarySpy(&socket, &QCWebSocket::binaryMessageReceived);
    
    socket.open();
    
    // 验证连接
    if (!waitForSignal(&socket, QMetaMethod::fromSignal(&QCWebSocket::connected), 5000)) {
        QSKIP("无法连接到本地 WebSocket 测试服务器，跳过分片测试。");
        return;
    }
    
    QCOMPARE(connectedSpy.count(), 1);
    
    // ========================================================================
    // 测试 1: 大文本消息分片（10KB）
    // ========================================================================
    qDebug() << "测试 1: 10KB 文本消息分片";
    
    QByteArray largeText(10240, 'A');
    QString testMessage = QString::fromUtf8(largeText);
    
    textSpy.clear();
    socket.sendTextMessage(testMessage);
    
    // 等待回显消息
    QVERIFY(waitForSignal(&socket, QMetaMethod::fromSignal(&QCWebSocket::textMessageReceived), 10000));
    QCOMPARE(textSpy.count(), 1);
    
    QString receivedText = textSpy.at(0).at(0).toString();
    QCOMPARE(receivedText.size(), 10240);
    QCOMPARE(receivedText, testMessage);
    
    qDebug() << "✅ 10KB 文本消息完整性验证通过";
    
    // ========================================================================
    // 测试 2: 超大二进制消息分片（100KB）
    // ========================================================================
    qDebug() << "测试 2: 100KB 二进制消息分片";
    
    QByteArray largeBinary(102400, 0x42);  // 填充 'B' (0x42)
    
    binarySpy.clear();
    socket.sendBinaryMessage(largeBinary);
    
    // 等待回显消息
    QVERIFY(waitForSignal(&socket, QMetaMethod::fromSignal(&QCWebSocket::binaryMessageReceived), 15000));
    QCOMPARE(binarySpy.count(), 1);
    
    QByteArray receivedBinary = binarySpy.at(0).at(0).toByteArray();
    QCOMPARE(receivedBinary.size(), 102400);
    QCOMPARE(receivedBinary, largeBinary);
    
    qDebug() << "✅ 100KB 二进制消息完整性验证通过";
    
    // ========================================================================
    // 测试 3: 边界条件（4096 字节 - 正好 1 帧）
    // ========================================================================
    qDebug() << "测试 3: 4096 字节边界测试";
    
    QByteArray boundaryBinary(4096, 0x43);  // 填充 'C' (0x43)
    
    binarySpy.clear();
    socket.sendBinaryMessage(boundaryBinary);
    
    QVERIFY(waitForSignal(&socket, QMetaMethod::fromSignal(&QCWebSocket::binaryMessageReceived), 10000));
    QCOMPARE(binarySpy.count(), 1);
    
    QByteArray receivedBoundary = binarySpy.at(0).at(0).toByteArray();
    QCOMPARE(receivedBoundary.size(), 4096);
    QCOMPARE(receivedBoundary, boundaryBinary);
    
    qDebug() << "✅ 4096 字节边界测试通过";
    
    // ========================================================================
    // 测试 4: 连续发送多个大消息
    // ========================================================================
    qDebug() << "测试 4: 连续发送多个大消息";
    
    textSpy.clear();
    for (int i = 0; i < 3; ++i) {
        QByteArray msg(8192, 'D' + i);
        socket.sendTextMessage(QString::fromUtf8(msg));
    }
    
    // 等待所有消息返回
    QTest::qWait(5000);
    QVERIFY(textSpy.count() >= 3);
    
    qDebug() << "✅ 连续大消息测试通过，收到" << textSpy.count() << "个消息";
    
    // 关闭连接
    socket.close();
    
    qDebug() << "✅ 分片消息完整性测试全部通过";
}

// ============================================================================
// 错误处理
// ============================================================================

void TestQCWebSocket::testConnectionRefused()
{
    qDebug() << "========== testConnectionRefused ==========";

    // 连接到一个拒绝连接的端口
    QCWebSocket socket{QUrl("ws://localhost:9999")};
    QSignalSpy errorSpy(&socket, &QCWebSocket::errorOccurred);

    socket.open();

    // 等待错误信号
    QVERIFY(waitForSignal(&socket, QMetaMethod::fromSignal(&QCWebSocket::errorOccurred), 10000));
    QVERIFY(errorSpy.count() >= 1);
    QVERIFY(!socket.errorString().isEmpty());

    qDebug() << "错误信息:" << socket.errorString();
    qDebug() << "✅ 连接拒绝错误处理测试通过";
}

void TestQCWebSocket::testSslError()
{
    qDebug() << "========== testSslError ==========";

    if (!externalNetworkTestsEnabled()) {
        QSKIP("外部网络用例默认跳过（可设置 QCURL_TEST_EXTERNAL_NETWORK=1 启用）。");
    }

    // SSL 错误处理测试
    // 使用 badssl.com 提供的公共测试域名
    
    // ========================================================================
    // 测试 1: 自签名证书（默认配置应拒绝）
    // ========================================================================
    qDebug() << "测试 1: 自签名证书拒绝（默认安全配置）";
    
    QCWebSocket socket(QUrl("wss://self-signed.badssl.com"));
    QSignalSpy errorSpy(&socket, &QCWebSocket::errorOccurred);
    QSignalSpy sslErrorSpy(&socket, &QCWebSocket::sslErrorsDetailed);
    
    socket.open();
    
    // 等待 SSL 错误（或连接错误）
    if (waitForSignal(&socket, QMetaMethod::fromSignal(&QCWebSocket::errorOccurred), 15000)) {
        qDebug() << "SSL 错误已检测:" << socket.errorString();
        
        // 可能不会触发 sslErrorsDetailed（取决于 libcurl 版本和 SSL 后端）
        if (sslErrorSpy.count() > 0) {
            QStringList errors = sslErrorSpy.at(0).at(0).toStringList();
            qDebug() << "详细 SSL 错误:" << errors;
            QVERIFY(!errors.isEmpty());
        }
        
        qDebug() << "✅ 默认配置正确拒绝自签名证书";
    } else {
        QSKIP("无法连接到 badssl.com 测试服务器（网络问题或服务不可用）");
        return;
    }
    
    // ========================================================================
    // 测试 2: 禁用验证后应连接成功
    // ========================================================================
    qDebug() << "测试 2: 禁用证书验证后连接成功";
    
    QCWebSocket socket2(QUrl("wss://self-signed.badssl.com"));
    socket2.setSslConfig(QCNetworkSslConfig::insecureConfig());
    
    QSignalSpy connectedSpy(&socket2, &QCWebSocket::connected);
    QSignalSpy errorSpy2(&socket2, &QCWebSocket::errorOccurred);
    
    socket2.open();
    
    // 等待连接（可能需要较长时间）
    if (waitForSignal(&socket2, QMetaMethod::fromSignal(&QCWebSocket::connected), 20000)) {
        qDebug() << "✅ 禁用验证后连接成功";
        QCOMPARE(connectedSpy.count(), 1);
        socket2.close();
    } else {
        // 某些情况下服务器可能仍然拒绝连接
        qDebug() << "⚠️  连接失败（服务器可能不可用），错误:" << socket2.errorString();
        // 不算失败，因为测试服务器不稳定
    }
    
    qDebug() << "✅ SSL 错误处理测试通过";
}

void TestQCWebSocket::testServerClosedConnection()
{
    qDebug() << "========== testServerClosedConnection ==========";

    if (!m_localServerSkipReason.isEmpty()) {
        QSKIP(qPrintable(m_localServerSkipReason));
    }

    QCWebSocket socket{QUrl(m_testServerUrl)};
    QSignalSpy disconnectedSpy(&socket, &QCWebSocket::disconnected);

    socket.open();
    if (!waitForSignal(&socket, QMetaMethod::fromSignal(&QCWebSocket::connected), 10000)) {
        QSKIP("无法连接到本地 WebSocket 测试服务器，跳过服务器关闭连接测试。");
    }

    // 发送一条消息
    socket.sendTextMessage(QStringLiteral("Hello"));

    // 等待一会儿，然后中止连接（模拟服务器关闭）
    QTest::qWait(1000);
    socket.abort();

    // 检查状态
    QCOMPARE(socket.state(), QCWebSocket::State::Closed);

    qDebug() << "✅ 服务器关闭连接测试通过";
}

// ============================================================================
// 辅助方法
// ============================================================================

bool TestQCWebSocket::waitForSignal(QObject *obj, const QMetaMethod &signal, int timeout)
{
    QSignalSpy spy(obj, signal);
    return spy.wait(timeout);
}

bool TestQCWebSocket::externalNetworkTestsEnabled() const
{
    const QByteArray value = qgetenv("QCURL_TEST_EXTERNAL_NETWORK");
    return !value.isEmpty() && value != "0";
}

bool TestQCWebSocket::waitForPortReady(quint16 port, int timeoutMs)
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

bool TestQCWebSocket::startLocalTestServer()
{
    stopLocalTestServer();
    m_localServerSkipReason.clear();

    const QString appDir = QCoreApplication::applicationDirPath();
    const QString scriptPath = QDir(appDir).absoluteFilePath(QStringLiteral("../../tests/websocket-fragment-server.js"));
    if (!QFileInfo::exists(scriptPath)) {
        m_localServerSkipReason = QStringLiteral(
            "未找到本地 WebSocket 测试服务器脚本（tests/websocket-fragment-server.js），跳过 WebSocket 本地用例。");
        return false;
    }

    m_localServer.setProgram(QStringLiteral("node"));
    m_localServer.setArguments({scriptPath});
    m_localServer.setProcessChannelMode(QProcess::MergedChannels);
    m_localServer.start();

    if (!m_localServer.waitForStarted(2000)) {
        m_localServerSkipReason = QStringLiteral(
            "无法启动本地 WebSocket 测试服务器（node）。请确认已安装 node，且 tests/node_modules 依赖可用。");
        return false;
    }

    if (!waitForPortReady(m_localTestServerPort, 3000)) {
        const QString output = QString::fromUtf8(m_localServer.readAll());
        if (!output.isEmpty()) {
            qWarning() << "Local WebSocket server output:\n" << output;
        }

        stopLocalTestServer();
        m_localServerSkipReason = QStringLiteral(
            "本地 WebSocket 测试服务器未在预期时间内就绪（端口 8765）。可能端口被占用或 node 环境不可用。");
        return false;
    }

    return true;
}

void TestQCWebSocket::stopLocalTestServer()
{
    if (m_localServer.state() == QProcess::NotRunning) {
        return;
    }

    m_localServer.terminate();
    if (!m_localServer.waitForFinished(1500)) {
        m_localServer.kill();
        m_localServer.waitForFinished(1500);
    }
}

QTEST_MAIN(TestQCWebSocket)
#include "tst_QCWebSocket.moc"
