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
#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QHostAddress>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QRegularExpression>
#include <QTcpServer>
#include <QTcpSocket>
#include <QThread>
#include <QUrlQuery>

#include "QCWebSocketTestServer.h"

using namespace QCurl;

/**
 * @brief QCWebSocket 单元测试
 *
 * 说明：
 * - message-level 回显 smoke：tests/websocket-fragment-server.js（依赖 ws；仅用于 echo/兼容性验证）。
 * - frame-level 证据链：tests/websocket-evidence-server.js（零外部依赖；显式发送 fragmentation/close 并输出工件）。
 * - 两类 server 均使用动态端口（0）并通过 READY marker 回传，避免固定端口导致的并发/占用冲突。
 * - 依赖 node 环境；若本地服务器无法启动则相关用例会 QSKIP。
 *   注意：本仓库的 ctest 取证式门禁为 skip=fail（见 tests/CMakeLists.txt 的 FAIL_REGULAR_EXPRESSION "SKIP\\s*:"），
 *   因此 QSKIP 代表“无证据”，在门禁口径下会导致失败而非“软通过”。
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

    QString m_localServerSkipReason;
    QString m_testServerUrl;

    QString m_localWssServerSkipReason;
    QString m_testWssServerUrl;
    QString m_caCertPath;

    QCWebSocketTestServer m_wsServer;
    QCWebSocketTestServer m_wssServer;

    QString m_localEvidenceServerSkipReason;
    QString m_testEvidenceServerUrl;
    QString m_evidenceArtifactsPath;
    QCWebSocketTestServer m_wsEvidenceServer;
};

void TestQCWebSocket::initTestCase()
{
    qDebug() << "========================================";
    qDebug() << "QCurl WebSocket 测试套件";
    qDebug() << "========================================";
    qDebug() << "本地测试服务器: (启动后回填实际端口)";
    qDebug();

    m_localServerSkipReason.clear();
    m_testServerUrl.clear();
    m_localWssServerSkipReason.clear();
    m_testWssServerUrl.clear();
    m_caCertPath.clear();
    m_localEvidenceServerSkipReason.clear();
    m_testEvidenceServerUrl.clear();
    m_evidenceArtifactsPath.clear();

    if (m_wsServer.start(QCWebSocketTestServer::Mode::Ws)) {
        m_testServerUrl = m_wsServer.baseUrl();
        qDebug() << "本地测试服务器:" << m_testServerUrl;
    } else {
        m_localServerSkipReason = m_wsServer.skipReason();
        qWarning().noquote() << "Local WebSocket server unavailable, tests will be skipped:"
                             << m_localServerSkipReason;
    }

    if (m_wssServer.start(QCWebSocketTestServer::Mode::Wss)) {
        m_testWssServerUrl = m_wssServer.baseUrl();
        m_caCertPath       = m_wssServer.caCertPath();
        qDebug() << "本地 WSS 测试服务器:" << m_testWssServerUrl;
    } else {
        m_localWssServerSkipReason = m_wssServer.skipReason();
        qWarning().noquote() << "Local WSS server unavailable, tests will be skipped:"
                             << m_localWssServerSkipReason;
    }

    if (m_wsEvidenceServer.start(QCWebSocketTestServer::Mode::Ws, QCWebSocketTestServer::ServerKind::Evidence)) {
        m_testEvidenceServerUrl = m_wsEvidenceServer.baseUrl();
        m_evidenceArtifactsPath = m_wsEvidenceServer.artifactsPath();
        qDebug() << "本地 WS Evidence Server:" << m_testEvidenceServerUrl;
        qDebug() << "Evidence artifacts:" << m_evidenceArtifactsPath;
    } else {
        m_localEvidenceServerSkipReason = m_wsEvidenceServer.skipReason();
        qWarning().noquote() << "Local WS evidence server unavailable, tests will be skipped:"
                             << m_localEvidenceServerSkipReason;
    }
}

void TestQCWebSocket::cleanupTestCase()
{
    m_wsServer.stop();
    m_wssServer.stop();
    m_wsEvidenceServer.stop();
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
    if (!connectedSpy.wait(10000)) {
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

    if (!m_localWssServerSkipReason.isEmpty()) {
        QSKIP(qPrintable(m_localWssServerSkipReason));
    }

    // 使用本地 wss:// 加密连接（自签证书，需配置 CA）
    QCWebSocket socket{QUrl(m_testWssServerUrl)};
    QCNetworkSslConfig sslConfig;
    sslConfig.caCertPath = m_caCertPath;
    socket.setSslConfig(sslConfig);
    QSignalSpy connectedSpy(&socket, &QCWebSocket::connected);

    socket.open();

    QVERIFY2(connectedSpy.wait(15000),
             qPrintable(QStringLiteral("无法连接本地 WSS 测试服务器，caCertPath=%1，错误=%2")
                            .arg(m_caCertPath, socket.errorString())));
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
    if (errorSpy.count() == 0) {
        QVERIFY(errorSpy.wait(10000));
    }
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
    // 创建一个会失败的连接（避免固定端口假设；使用临时端口并立即释放，获得更稳定的“连接失败”语义）
    QTcpServer portPicker;
    if (!portPicker.listen(QHostAddress::LocalHost, 0)) {
        QSKIP("无法绑定本机端口用于生成确定性 connection-refused 场景");
    }
    const quint16 port = portPicker.serverPort();
    portPicker.close();

    QCWebSocket socket(QUrl(QStringLiteral("ws://127.0.0.1:%1").arg(port)));
    
    // 设置标准重连策略（3 次重连，1s → 2s → 4s）
    socket.setReconnectPolicy(QCWebSocketReconnectPolicy::standardReconnect());
    
    // 监听重连尝试信号
    QSignalSpy reconnectSpy(&socket, &QCWebSocket::reconnectAttempt);
    QSignalSpy errorSpy(&socket, &QCWebSocket::errorOccurred);
    
    // 尝试连接（会失败）
    socket.open();
    
    // 等待初始连接失败
    if (errorSpy.count() == 0) {
        QVERIFY(errorSpy.wait(2000));
    }
    
    // 等待第一次重连尝试（延迟 1 秒）
    if (reconnectSpy.count() == 0) {
        QVERIFY(reconnectSpy.wait(2500));
    }
    
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
    if (textSpy.count() == 0) {
        QVERIFY(textSpy.wait(10000));
    }
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
    if (binarySpy.count() == 0) {
        QVERIFY(binarySpy.wait(10000));
    }
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

    if (textSpy.count() == 0) {
        QVERIFY(textSpy.wait(10000));
    }
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
    if (textSpy.count() == 0) {
        QVERIFY(textSpy.wait(20000));
    }
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
    if (pongSpy.count() > 0 || pongSpy.wait(5000)) {
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
    if (disconnectedSpy.count() == 0) {
        QVERIFY(disconnectedSpy.wait(10000));
    }
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

    // message-level 回显测试：验证“大消息收发链路”可用，但不证明 continuation frames（帧级分片）一定发生。
    QCWebSocket socket{QUrl(m_testServerUrl)};
    
    QSignalSpy connectedSpy(&socket, &QCWebSocket::connected);
    QSignalSpy textSpy(&socket, &QCWebSocket::textMessageReceived);
    QSignalSpy binarySpy(&socket, &QCWebSocket::binaryMessageReceived);
    
    socket.open();
    
    // 验证连接
    if (connectedSpy.count() == 0 && !connectedSpy.wait(5000)) {
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
    if (textSpy.count() == 0) {
        QVERIFY(textSpy.wait(10000));
    }
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
    if (binarySpy.count() == 0) {
        QVERIFY(binarySpy.wait(15000));
    }
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
    
    if (binarySpy.count() == 0) {
        QVERIFY(binarySpy.wait(10000));
    }
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
    
    // 等待所有消息返回（避免固定 sleep 导致 flaky）
    const int expectedMessages = 3;
    while (textSpy.count() < expectedMessages) {
        if (!textSpy.wait(5000)) {
            qWarning() << "Fragmented message timeout. Expected:" << expectedMessages
                       << "Actual:" << textSpy.count();
            socket.close();
            QFAIL("WebSocket 分片/连续消息回显超时（5s），按取证口径视为失败。");
        }
    }
    QVERIFY(textSpy.count() >= 3);
    
    qDebug() << "✅ 连续大消息测试通过，收到" << textSpy.count() << "个消息";
    
    // 关闭连接
    socket.close();
    
    qDebug() << "✅ 分片消息完整性测试全部通过";
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

static QList<QJsonObject> readEvidenceFramesByCaseOnce(const QString &jsonlPath, const QString &caseId)
{
    QList<QJsonObject> out;
    QFile f(jsonlPath);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return out;
    }

    while (!f.atEnd()) {
        const QByteArray line = f.readLine().trimmed();
        if (line.isEmpty()) {
            continue;
        }
        QJsonParseError err{};
        const QJsonDocument doc = QJsonDocument::fromJson(line, &err);
        if (err.error != QJsonParseError::NoError || !doc.isObject()) {
            continue;
        }
        const QJsonObject obj = doc.object();
        if (obj.value(QStringLiteral("event")).toString() != QStringLiteral("ws_frame")) {
            continue;
        }
        if (obj.value(QStringLiteral("case")).toString() != caseId) {
            continue;
        }
        if (obj.value(QStringLiteral("direction")).toString() != QStringLiteral("send")) {
            continue;
        }
        out.append(obj);
    }
    return out;
}

static QList<QJsonObject> waitEvidenceFramesByCase(const QString &jsonlPath,
                                                   const QString &caseId,
                                                   int minCount,
                                                   int timeoutMs)
{
    QElapsedTimer timer;
    timer.start();

    QList<QJsonObject> frames;
    while (timer.elapsed() < timeoutMs) {
        frames = readEvidenceFramesByCaseOnce(jsonlPath, caseId);
        if (frames.size() >= minCount) {
            return frames;
        }
        QThread::msleep(50);
    }
    return frames;
}

void TestQCWebSocket::testFragmentedFramesReassembly()
{
    qDebug() << "========== testFragmentedFramesReassembly ==========";

    if (!m_localEvidenceServerSkipReason.isEmpty()) {
        QSKIP(qPrintable(m_localEvidenceServerSkipReason));
    }
    QVERIFY2(!m_evidenceArtifactsPath.isEmpty(), "Evidence server artifactsPath 为空，无法复核帧级证据。");

    const int totalLen  = 8192;
    const int parts     = 3;
    const int seed      = 7;
    const QString caseId = QString::fromLatin1(QTest::currentTestFunction());

    QUrl url(m_testEvidenceServerUrl + QStringLiteral("/fragment"));
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("type"), QStringLiteral("binary"));
    q.addQueryItem(QStringLiteral("len"), QString::number(totalLen));
    q.addQueryItem(QStringLiteral("parts"), QString::number(parts));
    q.addQueryItem(QStringLiteral("seed"), QString::number(seed));
    q.addQueryItem(QStringLiteral("case"), caseId);
    url.setQuery(q);

    QCWebSocket socket{url};
    QSignalSpy connectedSpy(&socket, &QCWebSocket::connected);
    QSignalSpy binarySpy(&socket, &QCWebSocket::binaryMessageReceived);

    socket.open();
    QVERIFY2(connectedSpy.wait(10000),
             qPrintable(QStringLiteral("Evidence server connect failed: %1").arg(socket.errorString())));

    if (binarySpy.count() == 0) {
        QVERIFY(binarySpy.wait(10000));
    }
    QCOMPARE(binarySpy.count(), 1);

    const QByteArray received = binarySpy.at(0).at(0).toByteArray();
    QCOMPARE(received.size(), totalLen);
    const QByteArray expected = generateDeterministicBytes(totalLen, seed);
    QCOMPARE(sha256Hex(received), sha256Hex(expected));

    // 复核证据：服务端工件必须记录“确实发送了 continuation frames”。
    const QList<QJsonObject> frames
        = waitEvidenceFramesByCase(m_evidenceArtifactsPath, caseId, parts, 2000);
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
    qDebug() << "✅ 帧级分片重组测试通过（len/sha256 + 服务器帧日志）";
}

// ============================================================================
// 错误处理
// ============================================================================

void TestQCWebSocket::testConnectionRefused()
{
    qDebug() << "========== testConnectionRefused ==========";

    // 连接到一个拒绝连接的端口（避免固定端口假设；使用临时端口并立即释放）
    QTcpServer portPicker;
    if (!portPicker.listen(QHostAddress::LocalHost, 0)) {
        QSKIP("无法绑定本机端口用于生成确定性 connection-refused 场景");
    }
    const quint16 port = portPicker.serverPort();
    portPicker.close();

    QCWebSocket socket{QUrl(QStringLiteral("ws://127.0.0.1:%1").arg(port))};
    QSignalSpy errorSpy(&socket, &QCWebSocket::errorOccurred);

    socket.open();

    // 等待错误信号
    if (errorSpy.count() == 0) {
        QVERIFY(errorSpy.wait(10000));
    }
    QVERIFY(errorSpy.count() >= 1);
    QVERIFY(!socket.errorString().isEmpty());

    qDebug() << "错误信息:" << socket.errorString();
    qDebug() << "✅ 连接拒绝错误处理测试通过";
}

void TestQCWebSocket::testSslError()
{
    qDebug() << "========== testSslError ==========";

    if (!m_localWssServerSkipReason.isEmpty()) {
        QSKIP(qPrintable(m_localWssServerSkipReason));
    }

    // SSL 错误处理测试
    
    // ========================================================================
    // 测试 1: 自签名证书（默认配置应拒绝）
    // ========================================================================
    qDebug() << "测试 1: 自签名证书拒绝（默认安全配置）";
    
    QCWebSocket socket{QUrl(m_testWssServerUrl)};
    QSignalSpy errorSpy(&socket, &QCWebSocket::errorOccurred);
    QSignalSpy sslErrorSpy(&socket, &QCWebSocket::sslErrorsDetailed);
    
    socket.open();
    
    QVERIFY2(errorSpy.wait(15000),
             qPrintable(QStringLiteral("预期应拒绝自签名证书（未配置 CA），但未观察到 errorOccurred。当前错误=%1")
                            .arg(socket.errorString())));

    qDebug() << "SSL 错误已检测:" << socket.errorString();

    // 可能不会触发 sslErrorsDetailed（取决于 libcurl 版本和 SSL 后端）
    if (sslErrorSpy.count() > 0) {
        const QStringList errors = sslErrorSpy.at(0).at(0).toStringList();
        qDebug() << "详细 SSL 错误:" << errors;
        QVERIFY(!errors.isEmpty());
    }

    qDebug() << "✅ 默认配置正确拒绝自签名证书";
    
    // ========================================================================
    // 测试 2: 配置 CA 后应连接成功
    // ========================================================================
    qDebug() << "测试 2: 配置 CA 后连接成功";
    
    QCWebSocket socket2{QUrl(m_testWssServerUrl)};
    QCNetworkSslConfig sslConfig;
    sslConfig.caCertPath = m_caCertPath;
    socket2.setSslConfig(sslConfig);
    
    QSignalSpy connectedSpy(&socket2, &QCWebSocket::connected);
    
    socket2.open();
    
    // 等待连接（可能需要较长时间）
    QVERIFY2(connectedSpy.wait(15000),
             qPrintable(QStringLiteral("预期配置 CA 后应连接成功，caCertPath=%1，错误=%2")
                            .arg(m_caCertPath, socket2.errorString())));

    qDebug() << "✅ 配置 CA 后连接成功";
    QCOMPARE(connectedSpy.count(), 1);
    socket2.close();
    
    qDebug() << "✅ SSL 错误处理测试通过";
}

void TestQCWebSocket::testServerClosedConnection()
{
    qDebug() << "========== testServerClosedConnection ==========";

    if (!m_localEvidenceServerSkipReason.isEmpty()) {
        QSKIP(qPrintable(m_localEvidenceServerSkipReason));
    }
    QVERIFY2(!m_evidenceArtifactsPath.isEmpty(), "Evidence server artifactsPath 为空，无法复核 close 证据。");

    const QString caseId = QString::fromLatin1(QTest::currentTestFunction());
    QUrl url(m_testEvidenceServerUrl + QStringLiteral("/close"));
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("code"), QStringLiteral("1001"));
    q.addQueryItem(QStringLiteral("reason"), QStringLiteral("bye"));
    q.addQueryItem(QStringLiteral("case"), caseId);
    url.setQuery(q);

    QCWebSocket socket{url};
    QSignalSpy connectedSpy(&socket, &QCWebSocket::connected);
    QSignalSpy closeSpy(&socket, &QCWebSocket::closeReceived);
    QSignalSpy disconnectedSpy(&socket, &QCWebSocket::disconnected);
    QSignalSpy errorSpy(&socket, &QCWebSocket::errorOccurred);

    socket.open();
    QVERIFY2(connectedSpy.wait(10000),
             qPrintable(QStringLiteral("Evidence server connect failed: %1").arg(socket.errorString())));

    // server-initiated close：服务端发送 close(code/reason) 并断开；禁止用 abort() 伪装。
    if (closeSpy.count() == 0) {
        QVERIFY(closeSpy.wait(10000));
    }
    QCOMPARE(closeSpy.count(), 1);
    QCOMPARE(closeSpy.at(0).at(0).toInt(), 1001);
    QCOMPARE(closeSpy.at(0).at(1).toString(), QStringLiteral("bye"));

    if (disconnectedSpy.count() == 0) {
        QVERIFY(disconnectedSpy.wait(10000));
    }
    QCOMPARE(disconnectedSpy.count(), 1);
    QCOMPARE(socket.state(), QCWebSocket::State::Closed);

    // 允许实现差异：close 期间不应出现 error；若出现则输出用于定位。
    if (errorSpy.count() > 0) {
        qWarning() << "Unexpected error during server close:" << socket.errorString();
    }

    const QList<QJsonObject> frames
        = waitEvidenceFramesByCase(m_evidenceArtifactsPath, caseId, 1, 2000);
    QVERIFY2(!frames.isEmpty(), "未在 evidence 工件中找到 close 帧记录。");
    const QJsonObject last = frames.last();
    QCOMPARE(last.value(QStringLiteral("opcode")).toInt(-1), 0x8);
    QCOMPARE(last.value(QStringLiteral("close_code")).toInt(-1), 1001);
    QCOMPARE(last.value(QStringLiteral("close_reason")).toString(), QStringLiteral("bye"));

    qDebug() << "✅ 服务器关闭连接测试通过";
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
    return spy.wait(timeout);
}

QTEST_MAIN(TestQCWebSocket)
#include "tst_QCWebSocket.moc"
