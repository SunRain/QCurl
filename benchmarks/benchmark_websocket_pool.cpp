/**
 * @file benchmark_websocket_pool.cpp
 * @brief WebSocket 连接池性能基准测试
 * 
 * 使用 Qt Test 框架的 QBENCHMARK 宏测量性能
 * 
 * @since 2.5.0
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
    qDebug() << "========================================";
    qDebug() << "WebSocket 连接池性能基准测试";
    qDebug() << "========================================";
    qDebug() << "测试 URL:" << TEST_URL;
    qDebug() << "";
}

void BenchmarkWebSocketPool::cleanupTestCase()
{
    qDebug() << "基准测试完成";
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

// ============================================================================
// 基准测试
// ============================================================================

void BenchmarkWebSocketPool::benchmarkAcquireWithoutPool()
{
    qDebug() << "基准测试：无连接池（每次新建连接）";

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

    qDebug() << "基准测试：有连接池（预热" << warmupCount << "个连接）";

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

    // 显示统计信息
    auto stats = pool->statistics(url);
    qDebug() << "统计信息 - 命中率:" << stats.hitRate << "% | 总连接:" << stats.totalConnections;
}

void BenchmarkWebSocketPool::benchmarkConnectionReuse()
{
    qDebug() << "基准测试：连接复用性能";

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

    auto stats = pool->statistics(url);
    qDebug() << "统计信息 - 命中率:" << stats.hitRate << "%";
}

void BenchmarkWebSocketPool::benchmarkPreWarm()
{
    qDebug() << "基准测试：预热连接性能";

    QUrl url(TEST_URL);

    QBENCHMARK {
        QCWebSocketPool tempPool;
        tempPool.preWarm(url, 5);
        QTest::qWait(3000);
        
        auto stats = tempPool.statistics(url);
        qDebug() << "预热结果 - 总连接:" << stats.totalConnections;
    }
}

QTEST_MAIN(BenchmarkWebSocketPool)
#include "benchmark_websocket_pool.moc"
