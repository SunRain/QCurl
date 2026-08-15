#include "QCWebSocketTestServer.h"
#include "QCWebSocket_p.h"
#include "test_wait_utils.h"
#include "test_websocket_evidence_utils.h"

#include <QCNetworkSslConfig.h>
#include <QCWebSocket.h>
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
#include <QPointer>
#include <QProcess>
#include <QRegularExpression>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QThread>
#include <QTimer>
#include <QUrlQuery>
#include <QtTest>

#include <algorithm>
#include <limits>
#include <memory>

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

private Q_SLOTS:
    void initTestCase();
    void cleanupTestCase();

    // ========================================================================
    // 连接测试
    // ========================================================================

    void testConnect();
    void testDestructorTeardownIsSilent();
    void testDeleteParentFromPublicSignal_data();
    void testDeleteParentFromPublicSignal();
    void testDeleteParentFromPartialPingSuppressesAutomaticPong();
    void testOpenIsNonBlocking();
    void testConnectWss();
    void testReuseSocket();
    void testReuseSocketWithSslConfig();
    void testAutoPongConfigIgnoredWhileConnected();
    void testOptionLimitsRejectInvalidValues();
    void testConnectInvalidUrl();
    void testAutoReconnect();
    void testCommandAdmissionRejectsInvalidStateWithoutMutation();
    void testOpenRejectsDuplicateCommandWithoutMutation();
    void testCommandsRejectWrongThreadWithoutMutation();

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
    void testControlFrameCommandsRejectOversizedPayloadWithoutMutation();
    void testSendCommandReportsQueueLimitWithoutConnectionError();
    void testCloseHandshake();
    void testFragmentedMessage();
    void testFragmentedFramesReassembly();
    void testPartialControlFramesReassembleIndependently();
    void testPartialPingDoesNotAutoPongWhenDisabled();
    void testEmptyClosePayloadReports1005();
    void testCloseReasonTruncatesAtUtf8Boundary();
    void testInvalidUtf8ClosesWith1007();
    void testFrameLimitClosesWith1009();
    void testMessageLimitClosesWith1009();
    void testReceiveBufferLimitClosesWith1009();
    void testCloseHandshakeTimeout();
    void testReconnectAfterRemoteClose();
    void testServerClosedWithCustomCloseCode();
    void testServerClosedWithReservedCloseCode();
    void testRejectReservedCloseCodeOnSend();

    // ========================================================================
    // 错误处理
    // ========================================================================

    void testConnectionRefused();
    void testSslError();
    /**
     * @brief 验证 TLS 主错误不受 SSL 验证诊断查询失败影响。
     */
    void testSslVerifyInfoFailurePreservesCurlError();
    void testServerClosedConnection();

    /**
     * @brief 验证 HTTP header 解绑失败时仍保留 backing storage。
     */
    void testHeaderUnbindFailureRetainsBackingStorage();

    /**
     * @brief 验证析构期 header 解绑失败时 backing storage 进入进程期 quarantine。
     */
    void testHeaderUnbindFailureDuringTeardownQuarantinesBackingStorage();

    /**
     * @brief 验证 persistent transfer 的 header storage 只清理一次。
     */
    void testPersistentTransferHeaderCleanupExactlyOnce();

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
    QSignalSpy disconnectedSpy(&socket, &QCWebSocket::disconnected);
    QSignalSpy stateChangedSpy(&socket, &QCWebSocket::stateChanged);
    QSignalSpy isValidChangedSpy(&socket, &QCWebSocket::isValidChanged);

    static_cast<void>(socket.open());

    // 等待连接成功信号
    QVERIFY2(TestWaitUtils::waitForSpyCount(connectedSpy, 1, 10000),
             qPrintable(QStringLiteral("无法连接到本地 WebSocket 测试服务器：%1")
                            .arg(socket.errorString())));
    QCOMPARE(connectedSpy.count(), 1);
    QCOMPARE(socket.state(), QCWebSocket::State::Connected);
    QVERIFY(socket.isValid());
    QCOMPARE(isValidChangedSpy.count(), 1);
    QCOMPARE(isValidChangedSpy.at(0).at(0).toBool(), true);

    // 检查状态变化信号
    QVERIFY(stateChangedSpy.count() >= 2); // Connecting -> Connected

    static_cast<void>(socket.close());
    QVERIFY(TestWaitUtils::waitForSpyCount(disconnectedSpy, 1, 10000));
    QCOMPARE(isValidChangedSpy.count(), 2);
    QCOMPARE(isValidChangedSpy.at(1).at(0).toBool(), false);
    QVERIFY(!socket.isValid());

    qDebug() << "Basic connection contract verified";
}

/// @brief 验证析构只执行内部 teardown，不发射业务信号或启动重连。
void TestQCWebSocket::testDestructorTeardownIsSilent()
{
    QObject observer;
    int stateChangedCount               = 0;
    int isValidChangedCount             = 0;
    int disconnectedCount               = 0;
    int reconnectAttemptCount           = 0;
    int stateChangedBeforeDestruction   = 0;
    int isValidChangedBeforeDestruction = 0;

    {
        QCWebSocketOptions options;
        options.setReconnectPolicy(QCWebSocketReconnectPolicy::standardReconnect());
        QCWebSocket socket{QUrl(m_testServerUrl), options};
        QSignalSpy connectedSpy(&socket, &QCWebSocket::connected);

        QObject::connect(&socket, &QCWebSocket::stateChanged, &observer, [&stateChangedCount]() {
            ++stateChangedCount;
        });
        QObject::connect(&socket, &QCWebSocket::isValidChanged, &observer, [&isValidChangedCount]() {
            ++isValidChangedCount;
        });
        QObject::connect(&socket, &QCWebSocket::disconnected, &observer, [&disconnectedCount]() {
            ++disconnectedCount;
        });
        QObject::connect(&socket,
                         &QCWebSocket::reconnectAttempt,
                         &observer,
                         [&reconnectAttemptCount]() { ++reconnectAttemptCount; });

        static_cast<void>(socket.open());
        QVERIFY2(TestWaitUtils::waitForSpyCount(connectedSpy, 1, 10000),
                 qPrintable(socket.errorString()));
        QCOMPARE(socket.state(), QCWebSocket::State::Connected);

        stateChangedBeforeDestruction   = stateChangedCount;
        isValidChangedBeforeDestruction = isValidChangedCount;
    }

    QCoreApplication::processEvents();
    QCOMPARE(stateChangedCount, stateChangedBeforeDestruction);
    QCOMPARE(isValidChangedCount, isValidChangedBeforeDestruction);
    QCOMPARE(disconnectedCount, 0);
    QCOMPARE(reconnectAttemptCount, 0);
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

    static_cast<void>(socket.open());

    QVERIFY2(TestWaitUtils::waitForSpyCount(connectedSpy, 1, 15000),
             qPrintable(QStringLiteral("无法连接本地 WSS 测试服务器，caCertPath=%1，错误=%2")
                            .arg(m_caCertPath, socket.errorString())));
    QCOMPARE(connectedSpy.count(), 1);
    QCOMPARE(socket.state(), QCWebSocket::State::Connected);

    const QString handshakeError = TestWebSocketEvidenceUtils::verifyHandshakeEvidence(
        m_wssEvidenceArtifactsPath, caseId, QStringLiteral("/"), true, 1, 2000);
    QVERIFY2(handshakeError.isEmpty(), qPrintable(handshakeError));

    static_cast<void>(socket.close());

    qDebug() << "WSS connection contract verified";
}

void TestQCWebSocket::testOpenIsNonBlocking()
{
    QTcpServer stalledServer;
    QVERIFY(stalledServer.listen(QHostAddress::LocalHost, 0));

    QList<QTcpSocket *> clients;
    QObject::connect(&stalledServer, &QTcpServer::newConnection, &stalledServer, [&]() {
        while (stalledServer.hasPendingConnections()) {
            clients.append(stalledServer.nextPendingConnection());
        }
    });

    QCWebSocket socket{QUrl(QStringLiteral("ws://127.0.0.1:%1").arg(stalledServer.serverPort())),
                       QCWebSocketOptions{}};
    QCWebSocketOptions options = socket.options();
    QString optionError;
    QVERIFY(options.setConnectTimeout(std::chrono::milliseconds{500}, &optionError));
    QVERIFY(socket.setOptions(options, &optionError));

    bool timerFired = false;
    QTimer::singleShot(100, &socket, [&timerFired]() { timerFired = true; });
    static_cast<void>(socket.open());
    QTest::qWait(250);

    QVERIFY2(
        timerFired,
        "WebSocket open() blocked the owner event loop while the server withheld the handshake");
    QCOMPARE(socket.state(), QCWebSocket::State::Connecting);

    socket.abort();
    for (QTcpSocket *client : std::as_const(clients)) {
        if (client) {
            client->close();
            client->deleteLater();
        }
    }
    stalledServer.close();
}

void TestQCWebSocket::testDeleteParentFromPublicSignal_data()
{
    QTest::addColumn<int>("signalCase");
    QTest::newRow("stateChanged") << 0;
    QTest::newRow("isValidChanged") << 1;
    QTest::newRow("connected") << 2;
    QTest::newRow("textMessageReceived") << 3;
    QTest::newRow("binaryMessageReceived") << 4;
    QTest::newRow("pingReceived") << 5;
    QTest::newRow("pongReceived") << 6;
    QTest::newRow("closeReceived") << 7;
    QTest::newRow("errorOccurred") << 8;
    QTest::newRow("reconnectAttempt") << 9;
    QTest::newRow("disconnected") << 10;
    QTest::newRow("sslErrorsDetailed") << 11;
}

void TestQCWebSocket::testDeleteParentFromPublicSignal()
{
    QFETCH(int, signalCase);

    const QString caseId = QStringLiteral("delete-parent-%1").arg(signalCase);
    QUrl url(m_testEvidenceServerUrl);
    QCWebSocketOptions options;
    if (signalCase == 4) {
        QUrlQuery query;
        query.addQueryItem(QStringLiteral("type"), QStringLiteral("binary"));
        query.addQueryItem(QStringLiteral("len"), QStringLiteral("16"));
        query.addQueryItem(QStringLiteral("parts"), QStringLiteral("2"));
        query.addQueryItem(QStringLiteral("seed"), QStringLiteral("3"));
        query.addQueryItem(QStringLiteral("case"), caseId);
        url.setPath(QStringLiteral("/fragment"));
        url.setQuery(query);
    } else if (signalCase >= 5 && signalCase <= 7) {
        url = TestWebSocketEvidenceUtils::buildCaseUrl(m_testEvidenceServerUrl,
                                                       QStringLiteral("/partial-control"),
                                                       caseId);
    } else if (signalCase == 8) {
        url.setPath(QStringLiteral("/invalid-utf8"));
    } else if (signalCase == 9) {
        url = TestWebSocketEvidenceUtils::buildCaseUrl(m_testEvidenceServerUrl,
                                                       QStringLiteral("/close-once"),
                                                       caseId);
        QCWebSocketReconnectPolicy policy;
        policy.setMaxRetries(1);
        policy.setInitialDelay(std::chrono::milliseconds{10});
        policy.setMaxDelay(std::chrono::milliseconds{10});
        policy.setBackoffMultiplier(1.0);
        policy.setRetriableCloseCodes({QCWebSocket::CloseCode::GoingAway});
        options.setReconnectPolicy(policy);
    } else if (signalCase == 10) {
        QUrlQuery query;
        query.addQueryItem(QStringLiteral("code"), QStringLiteral("1000"));
        query.addQueryItem(QStringLiteral("reason"), QStringLiteral("done"));
        query.addQueryItem(QStringLiteral("case"), caseId);
        url.setPath(QStringLiteral("/close"));
        url.setQuery(query);
    } else if (signalCase == 11) {
        url = QUrl(m_testWssServerUrl);
    }

    auto *parent = new QObject;
    auto *socket = new QCWebSocket{url, options, parent};
    const QPointer<QObject> guardedParent(parent);
    const auto deleteParent = [parent]() { delete parent; };

    switch (signalCase) {
        case 0:
            QObject::connect(socket,
                             &QCWebSocket::stateChanged,
                             this,
                             [deleteParent](QCWebSocket::State state) {
                                 if (state == QCWebSocket::State::Connecting) {
                                     deleteParent();
                                 }
                             });
            break;
        case 1:
            QObject::connect(socket, &QCWebSocket::isValidChanged, this, [deleteParent](bool valid) {
                if (valid) {
                    deleteParent();
                }
            });
            break;
        case 2:
            QObject::connect(socket, &QCWebSocket::connected, this, deleteParent);
            break;
        case 3:
            QObject::connect(socket,
                             &QCWebSocket::textMessageReceived,
                             this,
                             [deleteParent](const QString &) { deleteParent(); });
            break;
        case 4:
            QObject::connect(socket,
                             &QCWebSocket::binaryMessageReceived,
                             this,
                             [deleteParent](const QByteArray &) { deleteParent(); });
            break;
        case 5:
            QObject::connect(socket,
                             &QCWebSocket::pingReceived,
                             this,
                             [deleteParent](const QByteArray &) { deleteParent(); });
            break;
        case 6:
            QObject::connect(socket,
                             &QCWebSocket::pongReceived,
                             this,
                             [deleteParent](const QByteArray &) { deleteParent(); });
            break;
        case 7:
            QObject::connect(socket,
                             &QCWebSocket::closeReceived,
                             this,
                             [deleteParent](int, const QString &) { deleteParent(); });
            break;
        case 8:
            QObject::connect(socket,
                             &QCWebSocket::errorOccurred,
                             this,
                             [deleteParent](const QString &) { deleteParent(); });
            break;
        case 9:
            QObject::connect(socket,
                             &QCWebSocket::reconnectAttempt,
                             this,
                             [deleteParent](int, QCWebSocket::CloseCode) { deleteParent(); });
            break;
        case 10:
            QObject::connect(socket, &QCWebSocket::disconnected, this, deleteParent);
            break;
        case 11:
            QObject::connect(socket,
                             &QCWebSocket::sslErrorsDetailed,
                             this,
                             [deleteParent](const QStringList &) { deleteParent(); });
            break;
        default:
            QFAIL("unknown signal deletion case");
    }

    static_cast<void>(socket->open());
    if (signalCase == 3) {
        QTRY_COMPARE_WITH_TIMEOUT(socket->state(), QCWebSocket::State::Connected, 10000);
        static_cast<void>(socket->sendTextMessage(QStringLiteral("delete-parent")));
    }

    QTRY_VERIFY_WITH_TIMEOUT(guardedParent.isNull(), 15000);
    QCoreApplication::processEvents(QEventLoop::AllEvents, 100);
}

void TestQCWebSocket::testDeleteParentFromPartialPingSuppressesAutomaticPong()
{
    const QString caseId = QString::fromLatin1(QTest::currentTestFunction());
    const QUrl url = TestWebSocketEvidenceUtils::buildCaseUrl(m_testEvidenceServerUrl,
                                                              QStringLiteral("/partial-control"),
                                                              caseId);
    auto *parent   = new QObject;
    auto *socket   = new QCWebSocket{url, QCWebSocketOptions{}, parent};
    const QPointer<QObject> guardedParent(parent);
    QObject::connect(socket, &QCWebSocket::pingReceived, this, [parent](const QByteArray &) {
        delete parent;
    });

    static_cast<void>(socket->open());
    QTRY_VERIFY_WITH_TIMEOUT(guardedParent.isNull(), 10000);

    const QList<QJsonObject> receivedFrames = TestWebSocketEvidenceUtils::waitFrameEventsByCase(
        m_evidenceArtifactsPath, caseId, 1, 500, QStringLiteral("recv"));
    const bool sentPong = std::any_of(receivedFrames.cbegin(),
                                      receivedFrames.cend(),
                                      [](const QJsonObject &frame) {
                                          return frame.value(QStringLiteral("opcode")).toInt(-1)
                                                 == 0xA;
                                      });
    QVERIFY(!sentPong);
}

void TestQCWebSocket::testReuseSocket()
{
    QCWebSocket socket{QUrl(m_testServerUrl), QCWebSocketOptions{}};

    for (int round = 0; round < 2; ++round) {
        QSignalSpy connectedSpy(&socket, &QCWebSocket::connected);
        QSignalSpy disconnectedSpy(&socket, &QCWebSocket::disconnected);

        static_cast<void>(socket.open());
        QVERIFY2(TestWaitUtils::waitForSpyCount(connectedSpy, 1, 10000),
                 qPrintable(QStringLiteral("第 %1 次连接未成功，错误=%2")
                                .arg(round + 1)
                                .arg(socket.errorString())));
        QCOMPARE(socket.state(), QCWebSocket::State::Connected);

        static_cast<void>(socket.close());
        QVERIFY2(TestWaitUtils::waitForSpyCount(disconnectedSpy, 1, 10000),
                 qPrintable(QStringLiteral("第 %1 次关闭未完成").arg(round + 1)));
        QCOMPARE(socket.state(), QCWebSocket::State::Closed);
    }

    qDebug() << "WebSocket lifecycle survives socket reuse";
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

        static_cast<void>(socket.open());
        QVERIFY2(TestWaitUtils::waitForSpyCount(connectedSpy, 1, 15000),
                 qPrintable(QStringLiteral("第 %1 次 WSS 连接未成功，ca=%2，错误=%3")
                                .arg(round + 1)
                                .arg(m_caCertPath, socket.errorString())));
        QCOMPARE(socket.state(), QCWebSocket::State::Connected);

        static_cast<void>(socket.close());
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

    static_cast<void>(socket.open());
    QCOMPARE(socket.state(), QCWebSocket::State::Connecting);
    QVERIFY(socket.options().autoPongEnabled());

    QCWebSocketOptions disabledOptions = socket.options();
    disabledOptions.setAutoPongEnabled(false);
    QString error;
    QVERIFY(!socket.setOptions(disabledOptions, &error));
    QVERIFY(!error.isEmpty());
    QVERIFY(socket.options().autoPongEnabled());
}

void TestQCWebSocket::testOptionLimitsRejectInvalidValues()
{
    QCWebSocketOptions options;
    QString error;

    const auto connectTimeout = options.connectTimeout();
    QVERIFY(!options.setConnectTimeout(std::chrono::milliseconds{0}, &error));
    QVERIFY(!error.isEmpty());
    QCOMPARE(options.connectTimeout(), connectTimeout);

    const qint64 maxFrameBytes = options.maxFrameBytes();
    error.clear();
    QVERIFY(!options.setMaxFrameBytes(0, &error));
    QVERIFY(!error.isEmpty());
    QCOMPARE(options.maxFrameBytes(), maxFrameBytes);

    const qint64 maxMessageBytes = options.maxMessageBytes();
    error.clear();
    QVERIFY(!options.setMaxMessageBytes(std::numeric_limits<qint64>::max(), &error));
    QVERIFY(!error.isEmpty());
    QCOMPARE(options.maxMessageBytes(), maxMessageBytes);

    const qint64 maxPendingSendBytes = options.maxPendingSendBytes();
    error.clear();
    QVERIFY(!options.setMaxPendingSendBytes(-1, &error));
    QVERIFY(!error.isEmpty());
    QCOMPARE(options.maxPendingSendBytes(), maxPendingSendBytes);

    const qint64 maxReceiveBufferBytes = options.maxReceiveBufferBytes();
    error.clear();
    QVERIFY(!options.setMaxReceiveBufferBytes(0, &error));
    QVERIFY(!error.isEmpty());
    QCOMPARE(options.maxReceiveBufferBytes(), maxReceiveBufferBytes);

    const auto closeTimeout = options.closeHandshakeTimeout();
    error.clear();
    QVERIFY(!options.setCloseHandshakeTimeout(std::chrono::milliseconds{0}, &error));
    QVERIFY(!error.isEmpty());
    QCOMPARE(options.closeHandshakeTimeout(), closeTimeout);
}

void TestQCWebSocket::testConnectInvalidUrl()
{
    QCWebSocket socket{QUrl("wss://invalid-host-that-does-not-exist.example.com"),
                       QCWebSocketOptions{}};
    QSignalSpy errorSpy(&socket, &QCWebSocket::errorOccurred);

    static_cast<void>(socket.open());

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
    static_cast<void>(socket.open());

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

/**
 * @brief 验证未连接状态下的数据与关闭命令会被同步拒绝，且不污染连接错误。
 */
void TestQCWebSocket::testCommandAdmissionRejectsInvalidStateWithoutMutation()
{
    QCWebSocket socket{QUrl(m_testServerUrl), QCWebSocketOptions{}};
    const auto initialState = socket.state();

    const auto textResult = socket.sendTextMessage(QStringLiteral("not-connected"));
    QCOMPARE(textResult.status(), QCWebSocketCommandResult::Status::InvalidState);
    QCOMPARE(textResult.acceptedBytes(), 0);

    const auto binaryResult = socket.sendBinaryMessage(QByteArrayLiteral("not-connected"));
    QCOMPARE(binaryResult.status(), QCWebSocketCommandResult::Status::InvalidState);
    QCOMPARE(binaryResult.acceptedBytes(), 0);

    const auto closeResult = socket.close();
    QCOMPARE(closeResult.status(), QCWebSocketCommandResult::Status::InvalidState);
    QCOMPARE(closeResult.acceptedBytes(), 0);

    const auto pingResult = socket.ping();
    QCOMPARE(pingResult.status(), QCWebSocketCommandResult::Status::InvalidState);
    QCOMPARE(pingResult.acceptedBytes(), 0);

    const auto pongResult = socket.pong();
    QCOMPARE(pongResult.status(), QCWebSocketCommandResult::Status::InvalidState);
    QCOMPARE(pongResult.acceptedBytes(), 0);

    QCOMPARE(socket.state(), initialState);
    QVERIFY(socket.errorString().isEmpty());
}

/**
 * @brief 验证重复 open 命令被同步拒绝，既有连接周期保持不变。
 */
void TestQCWebSocket::testOpenRejectsDuplicateCommandWithoutMutation()
{
    QCWebSocket socket{QUrl(m_testServerUrl), QCWebSocketOptions{}};
    QSignalSpy connectedSpy(&socket, &QCWebSocket::connected);

    const auto firstResult = socket.open();
    QCOMPARE(firstResult.status(), QCWebSocketCommandResult::Status::Accepted);
    QCOMPARE(firstResult.acceptedBytes(), 0);
    QCOMPARE(socket.state(), QCWebSocket::State::Connecting);

    const auto duplicateResult = socket.open();
    QCOMPARE(duplicateResult.status(), QCWebSocketCommandResult::Status::InvalidState);
    QCOMPARE(duplicateResult.acceptedBytes(), 0);
    QCOMPARE(socket.state(), QCWebSocket::State::Connecting);

    QVERIFY2(TestWaitUtils::waitForSpyCount(connectedSpy, 1, 10000),
             qPrintable(socket.errorString()));
    QCOMPARE(socket.state(), QCWebSocket::State::Connected);

    const auto connectedResult = socket.open();
    QCOMPARE(connectedResult.status(), QCWebSocketCommandResult::Status::InvalidState);
    QCOMPARE(socket.state(), QCWebSocket::State::Connected);
    QVERIFY(socket.errorString().isEmpty());

    QCOMPARE(socket.close().status(), QCWebSocketCommandResult::Status::Accepted);
}

/**
 * @brief 验证所有 WebSocket 命令在非 owner thread 同步拒绝且不修改 socket 状态。
 */
void TestQCWebSocket::testCommandsRejectWrongThreadWithoutMutation()
{
    QCWebSocket socket{QUrl(m_testServerUrl), QCWebSocketOptions{}};

    const auto results = std::async(std::launch::async, [&socket]() {
        return QList<QCWebSocketCommandResult>{
            socket.open(),
            socket.close(),
            socket.sendTextMessage(QStringLiteral("wrong-thread")),
            socket.sendBinaryMessage(QByteArrayLiteral("wrong-thread")),
            socket.ping(),
            socket.pong(),
        };
    }).get();

    for (const auto &result : results) {
        QCOMPARE(result.status(), QCWebSocketCommandResult::Status::WrongThread);
        QCOMPARE(result.acceptedBytes(), 0);
    }
    QCOMPARE(socket.state(), QCWebSocket::State::Unconnected);
    QVERIFY(socket.errorString().isEmpty());
}

// ============================================================================
// 消息收发测试
// ============================================================================

void TestQCWebSocket::testSendTextMessage()
{
    QCWebSocket socket{QUrl(m_testServerUrl), QCWebSocketOptions{}};
    QSignalSpy textSpy(&socket, &QCWebSocket::textMessageReceived);

    static_cast<void>(socket.open());
    QVERIFY2(waitForSignal(&socket, QMetaMethod::fromSignal(&QCWebSocket::connected), 10000),
             qPrintable(QStringLiteral("无法连接到本地 WebSocket 测试服务器：%1")
                            .arg(socket.errorString())));

    QString testMessage = QStringLiteral("Hello WebSocket!");
    const auto sendResult = socket.sendTextMessage(testMessage);
    QVERIFY(sendResult.isAccepted());
    QCOMPARE(sendResult.acceptedBytes(), testMessage.toUtf8().size());
    qDebug() << "接受字节数:" << sendResult.acceptedBytes();

    // 等待本地 echo 服务器回显
    QVERIFY(TestWaitUtils::waitForSpyCount(textSpy, 1, 10000));
    QCOMPARE(textSpy.count(), 1);

    QString received = textSpy.first().first().toString();
    qDebug() << "收到消息:" << received;

    QCOMPARE(received, testMessage);

    static_cast<void>(socket.close());

    qDebug() << "Text echo contract verified";
}

void TestQCWebSocket::testSendBinaryMessage()
{
    QCWebSocket socket{QUrl(m_testServerUrl), QCWebSocketOptions{}};
    QSignalSpy binarySpy(&socket, &QCWebSocket::binaryMessageReceived);

    static_cast<void>(socket.open());
    QVERIFY2(waitForSignal(&socket, QMetaMethod::fromSignal(&QCWebSocket::connected), 10000),
             qPrintable(QStringLiteral("无法连接到本地 WebSocket 测试服务器：%1")
                            .arg(socket.errorString())));

    QByteArray testData = "Binary Data: \x01\x02\x03\x04";
    const auto sendResult = socket.sendBinaryMessage(testData);
    QVERIFY(sendResult.isAccepted());
    QCOMPARE(sendResult.acceptedBytes(), testData.size());
    qDebug() << "接受字节数:" << sendResult.acceptedBytes();

    // 等待 Echo 服务器回显
    QVERIFY(TestWaitUtils::waitForSpyCount(binarySpy, 1, 10000));
    QCOMPARE(binarySpy.count(), 1);

    QByteArray received = binarySpy.first().first().toByteArray();
    QCOMPARE(received, testData);

    static_cast<void>(socket.close());

    qDebug() << "Binary echo contract verified";
}

void TestQCWebSocket::testReceiveTextMessage()
{
    QCWebSocket socket{QUrl(m_testServerUrl), QCWebSocketOptions{}};
    QSignalSpy textSpy(&socket, &QCWebSocket::textMessageReceived);

    static_cast<void>(socket.open());
    QVERIFY2(waitForSignal(&socket, QMetaMethod::fromSignal(&QCWebSocket::connected), 10000),
             qPrintable(QStringLiteral("无法连接到本地 WebSocket 测试服务器：%1")
                            .arg(socket.errorString())));

    const QString testMessage = QStringLiteral("Receive Test");
    static_cast<void>(socket.sendTextMessage(testMessage));

    QVERIFY(TestWaitUtils::waitForSpyCount(textSpy, 1, 10000));
    QCOMPARE(textSpy.count(), 1);
    QCOMPARE(textSpy.first().first().toString(), testMessage);

    static_cast<void>(socket.close());

    qDebug() << "Text receive path verified";
}

void TestQCWebSocket::testReceiveBinaryMessage()
{
    QCWebSocket socket{QUrl(m_testServerUrl), QCWebSocketOptions{}};
    QSignalSpy binarySpy(&socket, &QCWebSocket::binaryMessageReceived);

    static_cast<void>(socket.open());
    QVERIFY2(waitForSignal(&socket, QMetaMethod::fromSignal(&QCWebSocket::connected), 10000),
             qPrintable(QStringLiteral("无法连接到本地 WebSocket 测试服务器：%1")
                            .arg(socket.errorString())));

    // 发送多条二进制消息
    for (int i = 0; i < 3; ++i) {
        QByteArray data;
        data.append(static_cast<char>(i));
        data.append("Test Binary Data");
        static_cast<void>(socket.sendBinaryMessage(data));
        QVERIFY2(TestWaitUtils::waitForSpyCount(binarySpy, i + 1, 10000),
                 qPrintable(QStringLiteral("第 %1 条二进制回显未按时到达，当前累计=%2")
                                .arg(i + 1)
                                .arg(binarySpy.count())));
    }

    QCOMPARE(binarySpy.count(), 3);

    static_cast<void>(socket.close());

    qDebug() << "Binary receive path verified";
}

void TestQCWebSocket::testLargeMessage()
{
    QCWebSocket socket{QUrl(m_testServerUrl), QCWebSocketOptions{}};
    QSignalSpy textSpy(&socket, &QCWebSocket::textMessageReceived);

    static_cast<void>(socket.open());
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

    const auto sendResult = socket.sendTextMessage(largeMessage);
    QVERIFY(sendResult.isAccepted());
    QCOMPARE(sendResult.acceptedBytes(), largeMessage.toUtf8().size());

    // 等待服务器回显（可能需要更长时间）
    QVERIFY(TestWaitUtils::waitForSpyCount(textSpy, 1, 20000));
    QCOMPARE(textSpy.count(), 1);

    QString received = textSpy.first().first().toString();
    qDebug() << "收到消息大小:" << received.size() << "字节";

    QCOMPARE(received, largeMessage);

    static_cast<void>(socket.close());

    qDebug() << "Large message echo verified";
}

// ============================================================================
// 协议测试
// ============================================================================

void TestQCWebSocket::testPingPong()
{
    QCWebSocket socket{QUrl(m_testServerUrl), QCWebSocketOptions{}};
    QSignalSpy pongSpy(&socket, &QCWebSocket::pongReceived);

    static_cast<void>(socket.open());
    QVERIFY2(waitForSignal(&socket, QMetaMethod::fromSignal(&QCWebSocket::connected), 10000),
             qPrintable(QStringLiteral("无法连接到本地 WebSocket 测试服务器：%1")
                            .arg(socket.errorString())));

    // 发送 Ping 帧
    QByteArray pingPayload = "Ping Test";
    static_cast<void>(socket.ping(pingPayload));

    // 等待 Pong 响应
    // 注意：有些服务器可能不发送 Pong 响应，或者 libcurl 自动处理了
    if (TestWaitUtils::waitForSpyCount(pongSpy, 1, 5000)) {
        qDebug() << "收到 Pong 响应";
        QVERIFY(pongSpy.count() >= 1);
    } else {
        qDebug() << "⚠️ 未收到 Pong 响应（可能被 libcurl 自动处理）";
    }

    static_cast<void>(socket.close());

    qDebug() << "Ping/Pong path verified";
}

/**
 * @brief 验证过大的 Ping/Pong payload 被拒绝，不截断发送且连接状态保持不变。
 */
void TestQCWebSocket::testControlFrameCommandsRejectOversizedPayloadWithoutMutation()
{
    QCWebSocket socket{QUrl(m_testServerUrl), QCWebSocketOptions{}};
    QSignalSpy connectedSpy(&socket, &QCWebSocket::connected);
    QCOMPARE(socket.open().status(), QCWebSocketCommandResult::Status::Accepted);
    QVERIFY2(TestWaitUtils::waitForSpyCount(connectedSpy, 1, 10000),
             qPrintable(socket.errorString()));

    const QByteArray oversizedPayload(126, 'x');
    const auto pingResult = socket.ping(oversizedPayload);
    QCOMPARE(pingResult.status(), QCWebSocketCommandResult::Status::InvalidArgument);
    QCOMPARE(pingResult.acceptedBytes(), 0);

    const auto pongResult = socket.pong(oversizedPayload);
    QCOMPARE(pongResult.status(), QCWebSocketCommandResult::Status::InvalidArgument);
    QCOMPARE(pongResult.acceptedBytes(), 0);

    QCOMPARE(socket.state(), QCWebSocket::State::Connected);
    QVERIFY(socket.errorString().isEmpty());
    QCOMPARE(socket.close().status(), QCWebSocketCommandResult::Status::Accepted);
}

/**
 * @brief 验证发送队列拒绝通过命令结果报告，不改变连接生命周期错误。
 */
void TestQCWebSocket::testSendCommandReportsQueueLimitWithoutConnectionError()
{
    QCWebSocketOptions options;
    QString optionError;
    QVERIFY(options.setMaxPendingSendBytes(1, &optionError));

    QCWebSocket socket{QUrl(m_testServerUrl), options};
    QSignalSpy connectedSpy(&socket, &QCWebSocket::connected);
    QCOMPARE(socket.open().status(), QCWebSocketCommandResult::Status::Accepted);
    QVERIFY2(TestWaitUtils::waitForSpyCount(connectedSpy, 1, 10000),
             qPrintable(socket.errorString()));

    const auto result = socket.sendBinaryMessage(QByteArrayLiteral("too-large"));
    QCOMPARE(result.status(), QCWebSocketCommandResult::Status::QueueLimitReached);
    QCOMPARE(result.acceptedBytes(), 0);
    QVERIFY(!result.error().isEmpty());
    QCOMPARE(socket.state(), QCWebSocket::State::Connected);
    QVERIFY(socket.errorString().isEmpty());

    QCOMPARE(socket.close().status(), QCWebSocketCommandResult::Status::Accepted);
}

void TestQCWebSocket::testCloseHandshake()
{
    QCWebSocket socket{QUrl(m_testServerUrl), QCWebSocketOptions{}};
    QSignalSpy disconnectedSpy(&socket, &QCWebSocket::disconnected);

    static_cast<void>(socket.open());
    QVERIFY2(waitForSignal(&socket, QMetaMethod::fromSignal(&QCWebSocket::connected), 10000),
             qPrintable(QStringLiteral("无法连接到本地 WebSocket 测试服务器：%1")
                            .arg(socket.errorString())));

    // 优雅关闭
    static_cast<void>(socket.close(QCWebSocket::CloseCode::Normal, QStringLiteral("Test Close")));

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

    static_cast<void>(socket.open());

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
    static_cast<void>(socket.sendTextMessage(testMessage));

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
    static_cast<void>(socket.sendBinaryMessage(largeBinary));

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
    static_cast<void>(socket.sendBinaryMessage(boundaryBinary));

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
        static_cast<void>(socket.sendTextMessage(QString::fromUtf8(msg)));
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
    static_cast<void>(socket.close());

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

    static_cast<void>(socket.open());
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

    static_cast<void>(socket.close());
    qDebug() << "Frame-level reassembly verified (len/sha256 + evidence log)";
}

void TestQCWebSocket::testPartialControlFramesReassembleIndependently()
{
    const QString caseId = QString::fromLatin1(QTest::currentTestFunction());
    const QUrl url = TestWebSocketEvidenceUtils::buildCaseUrl(m_testEvidenceServerUrl,
                                                              QStringLiteral("/partial-control"),
                                                              caseId);
    QCWebSocket socket{url, QCWebSocketOptions{}};
    QSignalSpy connectedSpy(&socket, &QCWebSocket::connected);
    QSignalSpy textSpy(&socket, &QCWebSocket::textMessageReceived);
    QSignalSpy pingSpy(&socket, &QCWebSocket::pingReceived);
    QSignalSpy pongSpy(&socket, &QCWebSocket::pongReceived);
    QSignalSpy closeSpy(&socket, &QCWebSocket::closeReceived);
    QSignalSpy errorSpy(&socket, &QCWebSocket::errorOccurred);

    static_cast<void>(socket.open());
    QVERIFY2(TestWaitUtils::waitForSpyCount(connectedSpy, 1, 10000),
             qPrintable(
                 QStringLiteral("Evidence server connect failed: %1").arg(socket.errorString())));
    QVERIFY(TestWaitUtils::waitForSpyCount(closeSpy, 1, 10000));

    QCOMPARE(errorSpy.count(), 0);
    QCOMPARE(pingSpy.count(), 1);
    QCOMPARE(pingSpy.first().at(0).toByteArray(), QByteArrayLiteral("partial-ping"));
    QCOMPARE(pongSpy.count(), 1);
    QCOMPARE(pongSpy.first().at(0).toByteArray(), QByteArrayLiteral("partial-pong"));
    QCOMPARE(textSpy.count(), 1);
    QCOMPARE(textSpy.first().at(0).toString(), QStringLiteral("hello"));
    QCOMPARE(closeSpy.first().at(0).toInt(), 1000);
    QCOMPARE(closeSpy.first().at(1).toString(), QStringLiteral("partial-close"));

    const QList<QJsonObject> receivedFrames = TestWebSocketEvidenceUtils::waitFrameEventsByCase(
        m_evidenceArtifactsPath, caseId, 2, 2000, QStringLiteral("recv"));
    const auto pongIt = std::find_if(receivedFrames.cbegin(),
                                     receivedFrames.cend(),
                                     [](const QJsonObject &frame) {
                                         return frame.value(QStringLiteral("opcode")).toInt(-1)
                                                == 0xA;
                                     });
    QVERIFY(pongIt != receivedFrames.cend());
    QCOMPARE(pongIt->value(QStringLiteral("payload_len")).toInt(-1), 12);
    QCOMPARE(pongIt->value(QStringLiteral("payload_sha256")).toString(),
             sha256Hex(QByteArrayLiteral("partial-ping")));
}

void TestQCWebSocket::testPartialPingDoesNotAutoPongWhenDisabled()
{
    const QString caseId = QString::fromLatin1(QTest::currentTestFunction());
    const QUrl url = TestWebSocketEvidenceUtils::buildCaseUrl(m_testEvidenceServerUrl,
                                                              QStringLiteral("/partial-control"),
                                                              caseId);
    QCWebSocketOptions options;
    options.setAutoPongEnabled(false);
    QCWebSocket socket{url, options};
    QSignalSpy connectedSpy(&socket, &QCWebSocket::connected);
    QSignalSpy pingSpy(&socket, &QCWebSocket::pingReceived);
    QSignalSpy closeSpy(&socket, &QCWebSocket::closeReceived);
    QSignalSpy errorSpy(&socket, &QCWebSocket::errorOccurred);

    static_cast<void>(socket.open());
    QVERIFY2(TestWaitUtils::waitForSpyCount(connectedSpy, 1, 10000),
             qPrintable(
                 QStringLiteral("Evidence server connect failed: %1").arg(socket.errorString())));
    QVERIFY(TestWaitUtils::waitForSpyCount(closeSpy, 1, 10000));

    QCOMPARE(errorSpy.count(), 0);
    QCOMPARE(pingSpy.count(), 1);
    QCOMPARE(pingSpy.first().at(0).toByteArray(), QByteArrayLiteral("partial-ping"));

    const QList<QJsonObject> receivedFrames = TestWebSocketEvidenceUtils::waitFrameEventsByCase(
        m_evidenceArtifactsPath, caseId, 1, 2000, QStringLiteral("recv"));
    const bool sentPong = std::any_of(receivedFrames.cbegin(),
                                      receivedFrames.cend(),
                                      [](const QJsonObject &frame) {
                                          return frame.value(QStringLiteral("opcode")).toInt(-1)
                                                 == 0xA;
                                      });
    QVERIFY(!sentPong);
}

void TestQCWebSocket::testEmptyClosePayloadReports1005()
{
    const QString caseId = QString::fromLatin1(QTest::currentTestFunction());
    const QUrl url       = TestWebSocketEvidenceUtils::buildCaseUrl(m_testEvidenceServerUrl,
                                                                    QStringLiteral("/close-empty"),
                                                                    caseId);
    QCWebSocket socket{url, QCWebSocketOptions{}};
    QSignalSpy connectedSpy(&socket, &QCWebSocket::connected);
    QSignalSpy closeSpy(&socket, &QCWebSocket::closeReceived);
    QSignalSpy errorSpy(&socket, &QCWebSocket::errorOccurred);

    static_cast<void>(socket.open());
    QVERIFY2(TestWaitUtils::waitForSpyCount(connectedSpy, 1, 10000),
             qPrintable(
                 QStringLiteral("Evidence server connect failed: %1").arg(socket.errorString())));
    QVERIFY(TestWaitUtils::waitForSpyCount(closeSpy, 1, 10000));

    QCOMPARE(errorSpy.count(), 0);
    QCOMPARE(closeSpy.count(), 1);
    QCOMPARE(closeSpy.first().at(0).toInt(),
             static_cast<int>(QCWebSocket::CloseCode::NoStatusReceived));
    QVERIFY(closeSpy.first().at(1).toString().isEmpty());
}

void TestQCWebSocket::testCloseReasonTruncatesAtUtf8Boundary()
{
    QVERIFY2(!m_evidenceArtifactsPath.isEmpty(),
             "Evidence server artifactsPath 为空，无法复核 close reason wire 证据。");
    const QString caseId = QString::fromLatin1(QTest::currentTestFunction());
    const QUrl url       = TestWebSocketEvidenceUtils::buildCaseUrl(m_testEvidenceServerUrl,
                                                                    QStringLiteral("/echo"),
                                                                    caseId);
    QCWebSocket socket{url, QCWebSocketOptions{}};
    QSignalSpy connectedSpy(&socket, &QCWebSocket::connected);
    QSignalSpy disconnectedSpy(&socket, &QCWebSocket::disconnected);

    static_cast<void>(socket.open());
    QVERIFY2(TestWaitUtils::waitForSpyCount(connectedSpy, 1, 10000),
             qPrintable(
                 QStringLiteral("Evidence server connect failed: %1").arg(socket.errorString())));

    const QString expectedReason(121, QLatin1Char('a'));
    const QString oversizedReason = expectedReason + QChar(0x20AC) + QStringLiteral("tail");
    static_cast<void>(socket.close(QCWebSocket::CloseCode::Normal, oversizedReason));
    QVERIFY(TestWaitUtils::waitForSpyCount(disconnectedSpy, 1, 10000));

    const QList<QJsonObject> frames = TestWebSocketEvidenceUtils::waitFrameEventsByCase(
        m_evidenceArtifactsPath, caseId, 1, 2000, QStringLiteral("recv"));
    QVERIFY(!frames.isEmpty());
    const QJsonObject closeFrame = frames.constLast();
    QCOMPARE(closeFrame.value(QStringLiteral("opcode")).toInt(-1), 0x8);
    QCOMPARE(closeFrame.value(QStringLiteral("close_code")).toInt(-1), 1000);
    QCOMPARE(closeFrame.value(QStringLiteral("close_reason")).toString(), expectedReason);
    QCOMPARE(closeFrame.value(QStringLiteral("payload_len")).toInt(-1), 123);
}

void TestQCWebSocket::testInvalidUtf8ClosesWith1007()
{
    QUrl url(m_testEvidenceServerUrl + QStringLiteral("/invalid-utf8"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("case"), QString::fromLatin1(QTest::currentTestFunction()));
    url.setQuery(query);

    QCWebSocket socket{url, QCWebSocketOptions{}};
    QSignalSpy connectedSpy(&socket, &QCWebSocket::connected);
    QSignalSpy errorSpy(&socket, &QCWebSocket::errorOccurred);
    QSignalSpy closeSpy(&socket, &QCWebSocket::closeReceived);
    QSignalSpy disconnectedSpy(&socket, &QCWebSocket::disconnected);

    static_cast<void>(socket.open());
    QVERIFY2(TestWaitUtils::waitForSpyCount(connectedSpy, 1, 10000),
             qPrintable(socket.errorString()));
    QVERIFY(TestWaitUtils::waitForSpyCount(errorSpy, 1, 10000));
    QVERIFY(socket.errorString().contains(QStringLiteral("UTF-8")));
    QVERIFY(TestWaitUtils::waitForSpyCount(closeSpy, 1, 10000));
    QCOMPARE(closeSpy.first().at(0).toInt(),
             static_cast<int>(QCWebSocket::CloseCode::InvalidPayload));
    QVERIFY(TestWaitUtils::waitForSpyCount(disconnectedSpy, 1, 10000));
    QCOMPARE(socket.state(), QCWebSocket::State::Closed);
}

void TestQCWebSocket::testFrameLimitClosesWith1009()
{
    QUrl url(m_testEvidenceServerUrl + QStringLiteral("/payload"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("type"), QStringLiteral("binary"));
    query.addQueryItem(QStringLiteral("len"), QStringLiteral("8"));
    query.addQueryItem(QStringLiteral("case"), QString::fromLatin1(QTest::currentTestFunction()));
    url.setQuery(query);

    QCWebSocketOptions options;
    QVERIFY(options.setMaxFrameBytes(4));
    QCWebSocket socket{url, options};
    QSignalSpy errorSpy(&socket, &QCWebSocket::errorOccurred);
    QSignalSpy closeSpy(&socket, &QCWebSocket::closeReceived);
    QSignalSpy disconnectedSpy(&socket, &QCWebSocket::disconnected);

    static_cast<void>(socket.open());
    QVERIFY(TestWaitUtils::waitForSpyCount(errorSpy, 1, 10000));
    QVERIFY(socket.errorString().contains(QStringLiteral("frame")));
    QVERIFY(TestWaitUtils::waitForSpyCount(closeSpy, 1, 10000));
    QCOMPARE(closeSpy.first().at(0).toInt(),
             static_cast<int>(QCWebSocket::CloseCode::MessageTooBig));
    QVERIFY(TestWaitUtils::waitForSpyCount(disconnectedSpy, 1, 10000));
}

void TestQCWebSocket::testMessageLimitClosesWith1009()
{
    QUrl url(m_testEvidenceServerUrl + QStringLiteral("/fragment"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("type"), QStringLiteral("binary"));
    query.addQueryItem(QStringLiteral("len"), QStringLiteral("12"));
    query.addQueryItem(QStringLiteral("parts"), QStringLiteral("3"));
    query.addQueryItem(QStringLiteral("case"), QString::fromLatin1(QTest::currentTestFunction()));
    url.setQuery(query);

    QCWebSocketOptions options;
    QVERIFY(options.setMaxFrameBytes(4));
    QVERIFY(options.setMaxMessageBytes(8));
    QCWebSocket socket{url, options};
    QSignalSpy errorSpy(&socket, &QCWebSocket::errorOccurred);
    QSignalSpy closeSpy(&socket, &QCWebSocket::closeReceived);
    QSignalSpy disconnectedSpy(&socket, &QCWebSocket::disconnected);

    static_cast<void>(socket.open());
    QVERIFY(TestWaitUtils::waitForSpyCount(errorSpy, 1, 10000));
    QVERIFY(socket.errorString().contains(QStringLiteral("message")));
    QVERIFY(TestWaitUtils::waitForSpyCount(closeSpy, 1, 10000));
    QCOMPARE(closeSpy.first().at(0).toInt(),
             static_cast<int>(QCWebSocket::CloseCode::MessageTooBig));
    QVERIFY(TestWaitUtils::waitForSpyCount(disconnectedSpy, 1, 10000));
}

void TestQCWebSocket::testReceiveBufferLimitClosesWith1009()
{
    QUrl url(m_testEvidenceServerUrl + QStringLiteral("/fragment"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("type"), QStringLiteral("binary"));
    query.addQueryItem(QStringLiteral("len"), QStringLiteral("12"));
    query.addQueryItem(QStringLiteral("parts"), QStringLiteral("3"));
    query.addQueryItem(QStringLiteral("case"), QString::fromLatin1(QTest::currentTestFunction()));
    url.setQuery(query);

    QCWebSocketOptions options;
    QVERIFY(options.setMaxFrameBytes(4));
    QVERIFY(options.setMaxMessageBytes(16));
    QVERIFY(options.setMaxReceiveBufferBytes(8));
    QCWebSocket socket{url, options};
    QSignalSpy errorSpy(&socket, &QCWebSocket::errorOccurred);
    QSignalSpy closeSpy(&socket, &QCWebSocket::closeReceived);
    QSignalSpy disconnectedSpy(&socket, &QCWebSocket::disconnected);

    static_cast<void>(socket.open());
    QVERIFY(TestWaitUtils::waitForSpyCount(errorSpy, 1, 10000));
    QVERIFY(socket.errorString().contains(QStringLiteral("receive buffer")));
    QVERIFY(TestWaitUtils::waitForSpyCount(closeSpy, 1, 10000));
    QCOMPARE(closeSpy.first().at(0).toInt(),
             static_cast<int>(QCWebSocket::CloseCode::MessageTooBig));
    QVERIFY(TestWaitUtils::waitForSpyCount(disconnectedSpy, 1, 10000));
}

void TestQCWebSocket::testCloseHandshakeTimeout()
{
    QUrl url(m_testEvidenceServerUrl + QStringLiteral("/ignore-close"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("case"), QString::fromLatin1(QTest::currentTestFunction()));
    url.setQuery(query);

    QCWebSocketOptions options;
    QVERIFY(options.setCloseHandshakeTimeout(std::chrono::milliseconds{100}));
    QCWebSocket socket{url, options};
    QSignalSpy connectedSpy(&socket, &QCWebSocket::connected);
    QSignalSpy closeSpy(&socket, &QCWebSocket::closeReceived);
    QSignalSpy disconnectedSpy(&socket, &QCWebSocket::disconnected);

    static_cast<void>(socket.open());
    QVERIFY2(TestWaitUtils::waitForSpyCount(connectedSpy, 1, 10000),
             qPrintable(socket.errorString()));

    QElapsedTimer elapsed;
    elapsed.start();
    static_cast<void>(socket.close());
    QVERIFY(TestWaitUtils::waitForSpyCount(disconnectedSpy, 1, 3000));
    QVERIFY(elapsed.elapsed() >= 50);
    QVERIFY(elapsed.elapsed() < 3000);
    QCOMPARE(closeSpy.count(), 0);
    QCOMPARE(socket.state(), QCWebSocket::State::Closed);
}

void TestQCWebSocket::testReconnectAfterRemoteClose()
{
    const QString caseId = QString::fromLatin1(QTest::currentTestFunction());
    QUrl url(m_testEvidenceServerUrl + QStringLiteral("/close-once"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("case"), caseId);
    url.setQuery(query);

    QCWebSocketReconnectPolicy reconnectPolicy;
    reconnectPolicy.setMaxRetries(1);
    reconnectPolicy.setInitialDelay(std::chrono::milliseconds{50});
    reconnectPolicy.setMaxDelay(std::chrono::milliseconds{50});
    reconnectPolicy.setBackoffMultiplier(1.0);
    reconnectPolicy.setRetriableCloseCodes({QCWebSocket::CloseCode::GoingAway});

    QCWebSocketOptions options;
    options.setReconnectPolicy(reconnectPolicy);
    QCWebSocket socket{url, options};
    QSignalSpy connectedSpy(&socket, &QCWebSocket::connected);
    QSignalSpy reconnectSpy(&socket, &QCWebSocket::reconnectAttempt);
    QSignalSpy textSpy(&socket, &QCWebSocket::textMessageReceived);
    bool reconnectStartedFromUnconnected = false;
    QObject::connect(&socket,
                     &QCWebSocket::reconnectAttempt,
                     &socket,
                     [&socket, &reconnectStartedFromUnconnected]() {
                         reconnectStartedFromUnconnected = socket.state()
                                                           == QCWebSocket::State::Unconnected;
                     });

    static_cast<void>(socket.open());
    QVERIFY2(TestWaitUtils::waitForSpyCount(connectedSpy, 1, 10000),
             qPrintable(socket.errorString()));
    QVERIFY(TestWaitUtils::waitForSpyCount(reconnectSpy, 1, 10000));
    QVERIFY(reconnectStartedFromUnconnected);
    QVERIFY2(TestWaitUtils::waitForSpyCount(connectedSpy, 2, 10000),
             qPrintable(socket.errorString()));

    const QString message = QStringLiteral("reconnected-echo");
    const auto sendResult = socket.sendTextMessage(message);
    QVERIFY(sendResult.isAccepted());
    QCOMPARE(sendResult.acceptedBytes(), message.toUtf8().size());
    QVERIFY(TestWaitUtils::waitForSpyCount(textSpy, 1, 10000));
    QCOMPARE(textSpy.first().at(0).toString(), message);

    static_cast<void>(socket.close());
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

    static_cast<void>(socket.open());

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

    static_cast<void>(socket.open());

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

    static_cast<void>(socket2.open());

    // 等待连接（可能需要较长时间）
    QVERIFY2(TestWaitUtils::waitForSpyCount(connectedSpy, 1, 15000),
             qPrintable(QStringLiteral("预期配置 CA 后应连接成功，caCertPath=%1，错误=%2")
                            .arg(m_caCertPath, socket2.errorString())));

    qDebug() << "Connection succeeded after CA configuration";
    QCOMPARE(connectedSpy.count(), 1);
    static_cast<void>(socket2.close());

    qDebug() << "TLS validation path verified";
}

void TestQCWebSocket::testSslVerifyInfoFailurePreservesCurlError()
{
    QCWebSocket baseline{QUrl(m_testWssServerUrl), QCWebSocketOptions{}};
    QSignalSpy baselineErrorSpy(&baseline, &QCWebSocket::errorOccurred);
    static_cast<void>(baseline.open());
    QVERIFY2(TestWaitUtils::waitForSpyCount(baselineErrorSpy, 1, 15000),
             qPrintable(QStringLiteral("基线 TLS 失败未发生：%1").arg(baseline.errorString())));
    const QString expectedCurlError = baseline.errorString();
    QVERIFY(!expectedCurlError.isEmpty());

    const QByteArray previousFailure = qgetenv("QCURL_TEST_FORCE_GETINFO_ERROR");
    const auto restoreFailure        = qScopeGuard([previousFailure]() {
        if (previousFailure.isEmpty()) {
            qunsetenv("QCURL_TEST_FORCE_GETINFO_ERROR");
        } else {
            qputenv("QCURL_TEST_FORCE_GETINFO_ERROR", previousFailure);
        }
    });
    Q_UNUSED(restoreFailure);
    qputenv("QCURL_TEST_FORCE_GETINFO_ERROR", "CURLINFO_SSL_VERIFYRESULT");

    QCWebSocket socket{QUrl(m_testWssServerUrl), QCWebSocketOptions{}};
    QSignalSpy errorSpy(&socket, &QCWebSocket::errorOccurred);
    QSignalSpy sslErrorSpy(&socket, &QCWebSocket::sslErrorsDetailed);
    static_cast<void>(socket.open());

    QVERIFY2(TestWaitUtils::waitForSpyCount(errorSpy, 1, 15000),
             qPrintable(QStringLiteral("注入 getinfo 失败后未发生 TLS 错误：%1")
                            .arg(socket.errorString())));
    QCOMPARE(socket.errorString(), expectedCurlError);
    QVERIFY(!sslErrorSpy.isEmpty());

    const QStringList diagnostics = sslErrorSpy.first().at(0).toStringList();
    QVERIFY(diagnostics.contains(QStringLiteral("SSL 验证详细诊断不可用")));
    for (const QString &diagnostic : diagnostics) {
        QVERIFY(!diagnostic.contains(QStringLiteral("SSL 验证结果码:")));
    }
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

    static_cast<void>(socket.open());
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

    static_cast<void>(socket.open());
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

    static_cast<void>(socket.open());
    QVERIFY2(TestWaitUtils::waitForSpyCount(connectedSpy, 1, 10000),
             qPrintable(
                 QStringLiteral("Evidence server connect failed: %1").arg(socket.errorString())));

    QVERIFY(TestWaitUtils::waitForSpyCount(errorSpy, 1, 10000));
    QCOMPARE(closeSpy.count(), 0);
    const QString observedError = errorSpy.first().at(0).toString();
    QVERIFY2(observedError.contains(QStringLiteral("1006")),
             qPrintable(QStringLiteral("未观察到保留 close code 诊断，signal=%1 current=%2")
                            .arg(observedError, socket.errorString())));
    QVERIFY(TestWaitUtils::waitForSpyCount(disconnectedSpy, 1, 10000));
    QCOMPARE(socket.state(), QCWebSocket::State::Closed);
}

void TestQCWebSocket::testRejectReservedCloseCodeOnSend()
{
    QCWebSocket socket{QUrl(m_testEvidenceServerUrl), QCWebSocketOptions{}};
    QSignalSpy connectedSpy(&socket, &QCWebSocket::connected);
    QSignalSpy errorSpy(&socket, &QCWebSocket::errorOccurred);

    static_cast<void>(socket.open());
    QVERIFY2(TestWaitUtils::waitForSpyCount(connectedSpy, 1, 10000),
             qPrintable(
                 QStringLiteral("Evidence server connect failed: %1").arg(socket.errorString())));

    const QList<int> invalidCodes{-1, 0, 999, 1004, 1005, 1006, 1015, 1016, 2999, 5000, 65535};
    for (const int code : invalidCodes) {
        const auto result = socket.close(static_cast<QCWebSocket::CloseCode>(code),
                                         QStringLiteral("invalid"));
        QCOMPARE(result.status(), QCWebSocketCommandResult::Status::InvalidArgument);
        QCOMPARE(result.acceptedBytes(), 0);
        QVERIFY2(result.error().contains(QString::number(code)),
                 qPrintable(QStringLiteral("close code %1 缺少结果诊断").arg(code)));
        QCOMPARE(socket.state(), QCWebSocket::State::Connected);
        QVERIFY(socket.errorString().isEmpty());
    }
    QCOMPARE(errorSpy.count(), 0);

    socket.abort();
}

void TestQCWebSocket::testHeaderUnbindFailureRetainsBackingStorage()
{
    QCWebSocket socket{QUrl(QStringLiteral("ws://127.0.0.1:1")), QCWebSocketOptions{}};
    QCWebSocketPrivate privateData(&socket);

    std::unique_ptr<CURL, decltype(&curl_easy_cleanup)> curlHandle(curl_easy_init(),
                                                                   &curl_easy_cleanup);
    QVERIFY(curlHandle != nullptr);

    std::unique_ptr<curl_slist, decltype(&curl_slist_free_all)>
        headers(curl_slist_append(nullptr, "X-QCurl-Test: retained"), &curl_slist_free_all);
    QVERIFY(headers != nullptr);
    QCOMPARE(curl_easy_setopt(curlHandle.get(), CURLOPT_HTTPHEADER, headers.get()), CURLE_OK);

    privateData.managedCurlHandle = curlHandle.get();
    privateData.requestHeaders.reset(headers.release());

    const QByteArray previousFailure = qgetenv("QCURL_TEST_FORCE_SETOPT_ERROR");
    const auto restoreFailure        = qScopeGuard([previousFailure]() {
        if (previousFailure.isEmpty()) {
            qunsetenv("QCURL_TEST_FORCE_SETOPT_ERROR");
        } else {
            qputenv("QCURL_TEST_FORCE_SETOPT_ERROR", previousFailure);
        }
    });
    Q_UNUSED(restoreFailure);
    qputenv("QCURL_TEST_FORCE_SETOPT_ERROR", "CURLOPT_HTTPHEADER");

    QVERIFY(!privateData.clearRequestHeaders());
    QVERIFY(privateData.requestHeaders.get() != nullptr);

    qunsetenv("QCURL_TEST_FORCE_SETOPT_ERROR");
    QVERIFY(privateData.clearRequestHeaders());
    QVERIFY(privateData.requestHeaders.get() == nullptr);
}

void TestQCWebSocket::testHeaderUnbindFailureDuringTeardownQuarantinesBackingStorage()
{
    QCWebSocket socket{QUrl(QStringLiteral("ws://127.0.0.1:1")), QCWebSocketOptions{}};
    QCWebSocketPrivate privateData(&socket);

    std::unique_ptr<CURL, decltype(&curl_easy_cleanup)> curlHandle(curl_easy_init(),
                                                                   &curl_easy_cleanup);
    QVERIFY(curlHandle != nullptr);

    std::unique_ptr<curl_slist, decltype(&curl_slist_free_all)>
        headers(curl_slist_append(nullptr, "X-QCurl-Test: quarantine"), &curl_slist_free_all);
    QVERIFY(headers != nullptr);
    QCOMPARE(curl_easy_setopt(curlHandle.get(), CURLOPT_HTTPHEADER, headers.get()), CURLE_OK);

    privateData.managedCurlHandle = curlHandle.get();
    privateData.requestHeaders.reset(headers.release());
    const int initialCount = Internal::quarantinedWebSocketHeaderBackingCountForTest();

    const QByteArray previousFailure = qgetenv("QCURL_TEST_FORCE_SETOPT_ERROR");
    const auto restoreFailure        = qScopeGuard([previousFailure]() {
        if (previousFailure.isEmpty()) {
            qunsetenv("QCURL_TEST_FORCE_SETOPT_ERROR");
        } else {
            qputenv("QCURL_TEST_FORCE_SETOPT_ERROR", previousFailure);
        }
    });
    Q_UNUSED(restoreFailure);
    qputenv("QCURL_TEST_FORCE_SETOPT_ERROR", "CURLOPT_HTTPHEADER");

    privateData.teardownForDestruction();

    QVERIFY(privateData.requestHeaders.get() == nullptr);
    QCOMPARE(Internal::quarantinedWebSocketHeaderBackingCountForTest(), initialCount + 1);
}

void TestQCWebSocket::testPersistentTransferHeaderCleanupExactlyOnce()
{
    QCWebSocket socket{QUrl(QStringLiteral("ws://127.0.0.1:1")), QCWebSocketOptions{}};
    QCWebSocketPrivate privateData(&socket);

    std::unique_ptr<CURL, decltype(&curl_easy_cleanup)> curlHandle(curl_easy_init(),
                                                                   &curl_easy_cleanup);
    QVERIFY(curlHandle != nullptr);

    std::unique_ptr<curl_slist, decltype(&curl_slist_free_all)>
        headers(curl_slist_append(nullptr, "X-QCurl-Test: once"), &curl_slist_free_all);
    QVERIFY(headers != nullptr);
    QCOMPARE(curl_easy_setopt(curlHandle.get(), CURLOPT_HTTPHEADER, headers.get()), CURLE_OK);

    privateData.managedCurlHandle = curlHandle.get();
    privateData.requestHeaders.reset(headers.release());

    QVERIFY(privateData.clearRequestHeaders());
    QVERIFY(privateData.requestHeaders.get() == nullptr);

    QVERIFY(privateData.clearRequestHeaders());
    QVERIFY(privateData.requestHeaders.get() == nullptr);
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
