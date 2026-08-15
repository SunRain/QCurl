/**
 * @file benchmark_websocket_pool.cpp
 * @brief WebSocket 连接池性能基准测试
 *
 * 使用 Qt Test 框架的 QBENCHMARK 宏测量性能。
 */

#include "QCWebSocket.h"
#include "QCWebSocketPool.h"

#include <QElapsedTimer>
#include <QSignalSpy>
#include <QtTest>

using namespace QCurl;

static const QString TEST_URL = QStringLiteral("wss://echo.websocket.org");

class BenchmarkWebSocketPool : public QObject
{
    Q_OBJECT

private Q_SLOTS:
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
    /**
     * @brief 连接池 owner thread 中一次已解析的活动借用。
     */
    struct AcquiredSocket
    {
        QCWebSocket *socket              = nullptr;
        QCWebSocketPool::LeaseId leaseId = 0;
    };

    QCWebSocketPool *pool = nullptr;
    bool waitForConnection(QCWebSocket *socket, int timeout = 5000);
    QCWebSocketAcquireResult awaitAcquire(const QUrl &url, int timeout = 10000);
    AcquiredSocket acquireSocket(const QUrl &url, int timeout = 10000);
    QCWebSocketPreWarmResult awaitPreWarm(const QUrl &url, int count, int timeout = 15000);
};

void BenchmarkWebSocketPool::initTestCase()
{
    qDebug() << "测试 URL:" << TEST_URL;
}

void BenchmarkWebSocketPool::cleanupTestCase() {}

void BenchmarkWebSocketPool::init()
{
    // 每个测试前创建连接池
    pool = new QCWebSocketPool();
}

void BenchmarkWebSocketPool::cleanup()
{
    // 清理连接池
    if (pool) {
        QString error;
        QVERIFY2(pool->clearPool({}, &error), qPrintable(error));
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

QCWebSocketAcquireResult BenchmarkWebSocketPool::awaitAcquire(const QUrl &url, int timeout)
{
    auto future = pool->acquire(url);
    QElapsedTimer timer;
    timer.start();
    while (!future.isFinished() && timer.elapsed() < timeout) {
        QTest::qWait(10);
    }
    if (!future.isFinished()) {
        return QCWebSocketAcquireResult::failure(QCWebSocketAcquireResult::Status::ConnectionFailed,
                                                 QStringLiteral("acquire timeout"));
    }
    return future.result();
}

BenchmarkWebSocketPool::AcquiredSocket BenchmarkWebSocketPool::acquireSocket(const QUrl &url,
                                                                             const int timeout)
{
    const auto result = awaitAcquire(url, timeout);
    if (!result.isSuccess()) {
        return {};
    }

    QCWebSocket *socket = nullptr;
    if (pool->resolveLease(result.leaseId(), &socket) != QCWebSocketPool::LeaseResult::Success) {
        static_cast<void>(pool->release(result.leaseId()));
        return {};
    }
    return {socket, result.leaseId()};
}

QCWebSocketPreWarmResult BenchmarkWebSocketPool::awaitPreWarm(const QUrl &url,
                                                              int count,
                                                              int timeout)
{
    auto future = pool->preWarm(url, count);
    QElapsedTimer timer;
    timer.start();
    while (!future.isFinished() && timer.elapsed() < timeout) {
        QTest::qWait(10);
    }
    if (!future.isFinished()) {
        return QCWebSocketPreWarmResult::failure(QCWebSocketPreWarmResult::Status::ConnectionFailed,
                                                 count,
                                                 0,
                                                 QStringLiteral("preWarm timeout"));
    }
    return future.result();
}

void BenchmarkWebSocketPool::benchmarkAcquireWithoutPool()
{
    QUrl url(TEST_URL);

    QBENCHMARK
    {
        QCWebSocket socket(url, QCWebSocketOptions{});
        static_cast<void>(socket.open());

        if (waitForConnection(&socket, 10000)) {
            static_cast<void>(socket.close());
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
        const auto result = awaitPreWarm(url, warmupCount);
        QCOMPARE(result.status(), QCWebSocketPreWarmResult::Status::Success);
    }

    QBENCHMARK
    {
        const auto acquired = acquireSocket(url);
        auto *socket        = acquired.socket;

        if (!socket) {
            QSKIP("无法获取连接");
        }

        if (socket->state() != QCWebSocket::State::Connected) {
            if (!waitForConnection(socket, 10000)) {
                QCOMPARE(pool->release(acquired.leaseId), QCWebSocketPool::LeaseResult::Success);
                QSKIP("连接超时");
            }
        }

        QCOMPARE(pool->release(acquired.leaseId), QCWebSocketPool::LeaseResult::Success);
    }
}

void BenchmarkWebSocketPool::benchmarkConnectionReuse()
{
    QUrl url(TEST_URL);

    // 预热 1 个连接
    QCOMPARE(awaitPreWarm(url, 1).status(), QCWebSocketPreWarmResult::Status::Success);

    QBENCHMARK
    {
        // 获取 → 释放 → 再次获取（应复用）
        const auto acquired1 = acquireSocket(url);
        auto *s1             = acquired1.socket;
        QVERIFY(s1 != nullptr);
        QCOMPARE(pool->release(acquired1.leaseId), QCWebSocketPool::LeaseResult::Success);

        const auto acquired2 = acquireSocket(url);
        auto *s2             = acquired2.socket;
        QVERIFY(s2 == s1); // 验证复用
        QCOMPARE(pool->release(acquired2.leaseId), QCWebSocketPool::LeaseResult::Success);
    }
}

void BenchmarkWebSocketPool::benchmarkPreWarm()
{
    QUrl url(TEST_URL);

    QBENCHMARK
    {
        QCWebSocketPool tempPool;
        auto future = tempPool.preWarm(url, 5);
        QTRY_VERIFY_WITH_TIMEOUT(future.isFinished(), 15000);
        QCOMPARE(future.result().status(), QCWebSocketPreWarmResult::Status::Success);
    }
}

QTEST_MAIN(BenchmarkWebSocketPool)
#include "benchmark_websocket_pool.moc"
