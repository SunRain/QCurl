/**
 * @file benchmark_websocket_pool.cpp
 * @brief WebSocket 连接池性能基准测试
 *
 * 使用 Qt Test 框架的 QBENCHMARK 宏测量性能。
 */

#include <QtTest>
#include <QSignalSpy>
#include "QCWebSocketPool.h"
#include "QCWebSocket.h"

using namespace QCurl;

static const QString TEST_URL = QStringLiteral("wss://echo.websocket.org");

class BenchmarkWebSocketPool : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void init();
    void cleanup();

    // 基准测试
    void benchmarkAcquireWithoutPool();
    void benchmarkAcquireWithPool();
    void benchmarkAcquireWithPool_data();
    void benchmarkConnectionReuse();
    void benchmarkPreWarm();

private:
    QCWebSocketPool *pool = nullptr;
    bool waitForConnection(QCWebSocket *socket, int timeout = 5000);
};

void BenchmarkWebSocketPool::initTestCase()
{
    qDebug() << "测试 URL:" << TEST_URL;
}

void BenchmarkWebSocketPool::cleanupTestCase()
{
}

void BenchmarkWebSocketPool::init()
{
    // 每个测试前创建连接池
    pool = new QCWebSocketPool();
}

void BenchmarkWebSocketPool::cleanup()
{
    // 清理连接池
    if (pool) {
        pool->clearPool();
        delete pool;
        pool = nullptr;
    }
}

bool BenchmarkWebSocketPool::waitForConnection(QCWebSocket *socket, int timeout)
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

void BenchmarkWebSocketPool::benchmarkAcquireWithoutPool()
{
    QUrl url(TEST_URL);

    QBENCHMARK {
        QCWebSocket socket(url);
        socket.open();
        
        if (waitForConnection(&socket, 10000)) {
            socket.close();
            QTest::qWait(100);
        } else {
            QSKIP("无法连接到测试服务器");
        }
    }
}

void BenchmarkWebSocketPool::benchmarkAcquireWithPool_data()
{
    QTest::addColumn<int>("warmupCount");

    QTest::newRow("no warmup") << 0;
    QTest::newRow("warmup 1") << 1;
    QTest::newRow("warmup 5") << 5;
}

void BenchmarkWebSocketPool::benchmarkAcquireWithPool()
{
    QFETCH(int, warmupCount);

    QUrl url(TEST_URL);

    // 预热连接
    if (warmupCount > 0) {
        pool->preWarm(url, warmupCount);
        QTest::qWait(3000);  // 等待连接建立
    }

    QBENCHMARK {
        auto *socket = pool->acquire(url);
        
        if (!socket) {
            QSKIP("无法获取连接");
        }

        if (socket->state() != QCWebSocket::State::Connected) {
            if (!waitForConnection(socket, 10000)) {
                pool->release(socket);
                QSKIP("连接超时");
            }
        }

        pool->release(socket);
    }
}

void BenchmarkWebSocketPool::benchmarkConnectionReuse()
{
    QUrl url(TEST_URL);

    // 预热 1 个连接
    pool->preWarm(url, 1);
    QTest::qWait(3000);

    QBENCHMARK {
        // 获取 → 释放 → 再次获取（应复用）
        auto *s1 = pool->acquire(url);
        QVERIFY(s1 != nullptr);
        pool->release(s1);

        auto *s2 = pool->acquire(url);
        QVERIFY(s2 == s1);  // 验证复用
        pool->release(s2);
    }
}

void BenchmarkWebSocketPool::benchmarkPreWarm()
{
    QUrl url(TEST_URL);

    QBENCHMARK {
        QCWebSocketPool tempPool;
        tempPool.preWarm(url, 5);
        QTest::qWait(3000);
    }
}

QTEST_MAIN(BenchmarkWebSocketPool)
#include "benchmark_websocket_pool.moc"
