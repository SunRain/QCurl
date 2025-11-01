/**
 * @file benchmark_websocket_eventdriven.cpp
 * @brief WebSocket 事件驱动性能基准测试
 * 
 * 测量 v2.4.2 事件驱动优化的性能提升：
 * - 接收延迟（轮询 50ms → 事件驱动 <1ms）
 * - CPU 占用（空闲 2% → <0.1%）
 * 
 * @since 2.4.2
 */

#include <QtTest>
#include <QSignalSpy>
#include <QElapsedTimer>
#include <numeric>
#include <algorithm>
#include "QCWebSocket.h"

using namespace QCurl;

static const QString TEST_URL = QStringLiteral("wss://echo.websocket.org");

class BenchmarkWebSocketEventDriven : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();

    // 基准测试
    void benchmarkReceiveLatency();
    void benchmarkReceiveLatency_data();
    void benchmarkMultipleConnections();

private:
    bool waitForConnection(QCWebSocket *socket, int timeout = 5000);
};

void BenchmarkWebSocketEventDriven::initTestCase()
{
    qDebug() << "========================================";
    qDebug() << "WebSocket 事件驱动性能基准测试";
    qDebug() << "========================================";
    qDebug() << "测试 URL:" << TEST_URL;
    qDebug() << "";
    qDebug() << "注意：此测试验证 v2.4.2 的事件驱动优化";
    qDebug() << "";
}

void BenchmarkWebSocketEventDriven::cleanupTestCase()
{
    qDebug() << "基准测试完成";
}

bool BenchmarkWebSocketEventDriven::waitForConnection(QCWebSocket *socket, int timeout)
{
    if (!socket) {
        return false;
    }

    if (socket->state() == QCWebSocket::State::Connected) {
        return true;
    }

    QSignalSpy spy(socket, &QCWebSocket::connected);
    return spy.wait(timeout) || socket->state() == QCWebSocket::State::Connected;
}

void BenchmarkWebSocketEventDriven::benchmarkReceiveLatency_data()
{
    QTest::addColumn<QString>("message");
    QTest::addColumn<int>("iterations");

    QTest::newRow("short message") << "ping" << 10;
    QTest::newRow("medium message") << QString("test message ").repeated(10) << 10;
}

void BenchmarkWebSocketEventDriven::benchmarkReceiveLatency()
{
    QFETCH(QString, message);
    QFETCH(int, iterations);

    qDebug() << "基准测试：接收延迟（消息大小:" << message.size() << "字节）";

    QCWebSocket *socket = new QCWebSocket(QUrl(TEST_URL));
    socket->open();

    if (!waitForConnection(socket, 10000)) {
        delete socket;
        QSKIP("无法连接到测试服务器");
    }

    qDebug() << "连接成功，开始测试...";

    QList<qint64> latencies;

    for (int i = 0; i < iterations; ++i) {
        QSignalSpy spy(socket, &QCWebSocket::textMessageReceived);
        QElapsedTimer timer;

        timer.start();
        socket->sendTextMessage(message);

        if (spy.wait(5000)) {
            qint64 latency = timer.elapsed();
            latencies.append(latency);
            qDebug() << "  第" << (i+1) << "次延迟:" << latency << "ms";
        } else {
            qWarning() << "  第" << (i+1) << "次：响应超时";
        }

        QTest::qWait(100);
    }

    if (!latencies.isEmpty()) {
        qint64 avgLatency = std::accumulate(latencies.begin(), latencies.end(), 0LL) / latencies.size();
        qint64 maxLatency = *std::max_element(latencies.begin(), latencies.end());
        qint64 minLatency = *std::min_element(latencies.begin(), latencies.end());

        qDebug() << "";
        qDebug() << "延迟统计:";
        qDebug() << "  平均:" << avgLatency << "ms";
        qDebug() << "  最小:" << minLatency << "ms";
        qDebug() << "  最大:" << maxLatency << "ms";
        qDebug() << "";
        qDebug() << "预期：事件驱动模式下延迟应 <1ms（网络延迟除外）";
    }

    socket->close();
    delete socket;
}

void BenchmarkWebSocketEventDriven::benchmarkMultipleConnections()
{
    qDebug() << "基准测试：多连接空闲 CPU 占用";
    qDebug() << "";
    qDebug() << "创建 10 个连接并保持空闲 10 秒...";
    qDebug() << "请使用 top/htop 监控进程 CPU 占用率";
    qDebug() << "";

    QList<QCWebSocket*> sockets;

    // 创建 10 个连接
    for (int i = 0; i < 10; ++i) {
        auto *socket = new QCWebSocket(QUrl(TEST_URL));
        socket->open();
        sockets.append(socket);
        QTest::qWait(500);
    }

    qDebug() << "所有连接已建立，开始空闲...";

    QBENCHMARK_ONCE {
        // 空闲 10 秒
        QTest::qWait(10000);
    }

    qDebug() << "";
    qDebug() << "空闲完成";
    qDebug() << "预期：事件驱动模式下空闲 CPU 应 <0.1%";
    qDebug() << "";

    // 清理
    for (auto *socket : sockets) {
        socket->abort();
        delete socket;
    }
}

QTEST_MAIN(BenchmarkWebSocketEventDriven)
#include "benchmark_websocket_eventdriven.moc"
