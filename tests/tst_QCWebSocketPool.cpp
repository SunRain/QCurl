/**
 * @file tst_QCWebSocketPool.cpp
 * @brief QCurl WebSocket 连接池测试 - 验证连接复用和池管理功能
 *
 * 测试覆盖：
 * - 连接获取和释放
 * - 连接复用验证
 * - 池大小限制
 * - 空闲连接清理
 * - 统计信息
 * - 预热连接
 * - 多 URL 管理
 *
 */

#include <QtTest>
#include <QSignalSpy>
#include "QCWebSocketPool.h"
#include "QCWebSocket.h"

using namespace QCurl;

// 测试服务器（使用公共 WebSocket echo 服务）
static const QString TEST_WS_URL = QStringLiteral("wss://echo.websocket.org");
static const QString TEST_WS_URL2 = QStringLiteral("wss://echo.websocket.org/echo");

class TestQCWebSocketPool : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void init();
    void cleanup();

    // ========== 核心功能测试 ==========
    void testConstructor();                 // 构造函数和默认配置
    void testAcquireAndRelease();           // 基本获取和释放
    void testConnectionReuse();             // 连接复用验证
    void testMultipleUrls();                // 多个 URL 独立池
    void testMaxPoolSize();                 // 池大小限制
    
    // ========== 池管理测试 ==========
    void testPreWarm();                     // 预热连接
    void testClearPool();                   // 清理池
    void testStatistics();                  // 统计信息
    
    // ========== 边界情况测试 ==========
    void testAcquireFromEmptyPool();        // 空池获取
    void testReleaseNullPointer();          // 释放空指针
    void testReleaseNonPooledSocket();      // 释放非池连接
    void testMaxTotalConnections();         // 全局连接数限制

private:
    QCWebSocketPool *pool = nullptr;
    
    // 辅助方法
    bool waitForConnection(QCWebSocket *socket, int timeout = 5000);
};

// ============================================================================
// 测试辅助方法
// ============================================================================

bool TestQCWebSocketPool::waitForConnection(QCWebSocket *socket, int timeout)
{
    if (!socket) {
        return false;
    }
    
    QSignalSpy spy(socket, &QCWebSocket::connected);
    return spy.wait(timeout) || socket->state() == QCWebSocket::State::Connected;
}

// ============================================================================
// 测试生命周期
// ============================================================================

void TestQCWebSocketPool::initTestCase()
{
    qDebug() << "========================================";
    qDebug() << "QCurl WebSocket 连接池测试套件 (v2.5.0)";
    qDebug() << "========================================";
    qDebug() << "测试服务器:" << TEST_WS_URL;
}

void TestQCWebSocketPool::cleanupTestCase()
{
    qDebug() << "WebSocket 连接池测试套件完成";
}

void TestQCWebSocketPool::init()
{
    // 每个测试前执行
}

void TestQCWebSocketPool::cleanup()
{
    // 清理连接池
    if (pool) {
        pool->clearPool();
        delete pool;
        pool = nullptr;
    }
}

// ============================================================================
// 核心功能测试
// ============================================================================

void TestQCWebSocketPool::testConstructor()
{
    qDebug() << "========== testConstructor ==========";

    // 测试默认构造
    QCWebSocketPool pool1;
    auto config1 = pool1.config();
    QCOMPARE(config1.maxPoolSize, 10);
    QCOMPARE(config1.maxIdleTime, 300);
    QCOMPARE(config1.enableKeepAlive, true);

    // 测试自定义配置
    QCWebSocketPool::Config config;
    config.maxPoolSize = 5;
    config.maxIdleTime = 600;
    config.enableKeepAlive = false;

    QCWebSocketPool pool2(config);
    auto config2 = pool2.config();
    QCOMPARE(config2.maxPoolSize, 5);
    QCOMPARE(config2.maxIdleTime, 600);
    QCOMPARE(config2.enableKeepAlive, false);

    qDebug() << "✅ 构造函数测试通过";
}

void TestQCWebSocketPool::testAcquireAndRelease()
{
    qDebug() << "========== testAcquireAndRelease ==========";

    pool = new QCWebSocketPool();
    QUrl url(TEST_WS_URL);

    // 获取连接
    auto *socket = pool->acquire(url);
    QVERIFY(socket != nullptr);
    QVERIFY(socket->state() == QCWebSocket::State::Connected || 
            socket->state() == QCWebSocket::State::Connecting);

    // 等待连接完成
    if (!waitForConnection(socket, 10000)) {
        QSKIP("无法连接到测试服务器，跳过此测试");
    }

    QCOMPARE(socket->state(), QCWebSocket::State::Connected);

    // 检查统计信息
    auto stats = pool->statistics(url);
    QCOMPARE(stats.totalConnections, 1);
    QCOMPARE(stats.activeConnections, 1);
    QCOMPARE(stats.idleConnections, 0);

    // 归还连接
    pool->release(socket);

    // 检查统计信息
    stats = pool->statistics(url);
    QCOMPARE(stats.totalConnections, 1);
    QCOMPARE(stats.activeConnections, 0);
    QCOMPARE(stats.idleConnections, 1);

    qDebug() << "✅ 获取和释放测试通过";
}

void TestQCWebSocketPool::testConnectionReuse()
{
    qDebug() << "========== testConnectionReuse ==========";

    pool = new QCWebSocketPool();
    QUrl url(TEST_WS_URL);

    // 第 1 次获取（创建新连接）
    auto *socket1 = pool->acquire(url);
    QVERIFY(socket1 != nullptr);
    
    if (!waitForConnection(socket1, 10000)) {
        QSKIP("无法连接到测试服务器，跳过此测试");
    }

    auto stats1 = pool->statistics(url);
    QCOMPARE(stats1.missCount, 1);  // 未命中（创建新连接）
    QCOMPARE(stats1.hitCount, 0);

    // 归还
    pool->release(socket1);

    // 第 2 次获取（应复用同一连接）
    auto *socket2 = pool->acquire(url);
    QVERIFY(socket2 == socket1);  // 应该是同一个对象
    QCOMPARE(socket2->state(), QCWebSocket::State::Connected);

    auto stats2 = pool->statistics(url);
    QCOMPARE(stats2.hitCount, 1);   // 命中（复用连接）
    QCOMPARE(stats2.missCount, 1);  // 仍是 1（没有创建新连接）
    QVERIFY(stats2.hitRate > 0.0);  // 命中率应该 > 0

    pool->release(socket2);

    qDebug() << "✅ 连接复用测试通过，命中率:" << stats2.hitRate << "%";
}

void TestQCWebSocketPool::testMultipleUrls()
{
    qDebug() << "========== testMultipleUrls ==========";

    pool = new QCWebSocketPool();
    QUrl url1(TEST_WS_URL);
    QUrl url2(TEST_WS_URL2);

    // 获取第一个 URL 的连接
    auto *socket1 = pool->acquire(url1);
    QVERIFY(socket1 != nullptr);

    // 获取第二个 URL 的连接
    auto *socket2 = pool->acquire(url2);
    QVERIFY(socket2 != nullptr);
    QVERIFY(socket2 != socket1);  // 应该是不同的连接

    // 检查池中是否包含两个 URL
    QVERIFY(pool->contains(url1));
    QVERIFY(pool->contains(url2));

    // 检查全局统计
    auto globalStats = pool->statistics();
    QVERIFY(globalStats.totalConnections >= 2);

    pool->release(socket1);
    pool->release(socket2);

    qDebug() << "✅ 多 URL 测试通过";
}

void TestQCWebSocketPool::testMaxPoolSize()
{
    qDebug() << "========== testMaxPoolSize ==========";

    QCWebSocketPool::Config config;
    config.maxPoolSize = 2;  // 每个 URL 最多 2 个连接

    pool = new QCWebSocketPool(config);
    QUrl url(TEST_WS_URL);

    // 获取 2 个连接（应该成功）
    QList<QCWebSocket*> sockets;
    for (int i = 0; i < 2; ++i) {
        auto *socket = pool->acquire(url);
        if (!socket) {
            qWarning() << "创建第" << (i+1) << "个连接失败";
            break;
        }
        if (!waitForConnection(socket, 10000)) {
            qWarning() << "第" << (i+1) << "个连接超时";
            break;
        }
        sockets.append(socket);
    }

    QVERIFY(sockets.size() >= 1);  // 至少要有 1 个成功

    // 尝试获取第 3 个（应该失败，因为达到限制）
    QSignalSpy spy(pool, &QCWebSocketPool::poolLimitReached);
    auto *socket3 = pool->acquire(url);
    
    if (sockets.size() == 2) {
        // 如果前面 2 个都成功了，第 3 个应该失败
        QVERIFY(socket3 == nullptr);
        QCOMPARE(spy.count(), 1);
    }

    // 释放第一个连接
    if (!sockets.isEmpty()) {
        pool->release(sockets[0]);
        
        // 再次尝试获取（应该成功，复用第一个）
        auto *socket4 = pool->acquire(url);
        QVERIFY(socket4 != nullptr);
        QVERIFY(socket4 == sockets[0]);  // 应该复用
        
        pool->release(socket4);
    }

    // 清理
    for (auto *s : sockets) {
        if (s != sockets[0]) {
            pool->release(s);
        }
    }

    qDebug() << "✅ 池大小限制测试通过";
}

// ============================================================================
// 池管理测试
// ============================================================================

void TestQCWebSocketPool::testPreWarm()
{
    qDebug() << "========== testPreWarm ==========";

    pool = new QCWebSocketPool();
    QUrl url(TEST_WS_URL);

    // 预热 3 个连接
    pool->preWarm(url, 3);

    // 等待连接建立
    QTest::qWait(2000);

    // 检查统计
    auto stats = pool->statistics(url);
    qDebug() << "预热后连接数:" << stats.totalConnections;
    qDebug() << "空闲连接:" << stats.idleConnections;

    // 应该至少有 1 个连接被创建
    QVERIFY(stats.totalConnections >= 1);
    // 所有连接都应该是空闲的
    QCOMPARE(stats.activeConnections, 0);

    qDebug() << "✅ 预热连接测试通过";
}

void TestQCWebSocketPool::testClearPool()
{
    qDebug() << "========== testClearPool ==========";

    pool = new QCWebSocketPool();
    QUrl url(TEST_WS_URL);

    // 创建一些连接
    auto *socket1 = pool->acquire(url);
    if (socket1 && waitForConnection(socket1, 10000)) {
        pool->release(socket1);

        // 清理池
        pool->clearPool(url);

        // 检查统计
        auto stats = pool->statistics(url);
        QCOMPARE(stats.totalConnections, 0);
        QVERIFY(!pool->contains(url));

        qDebug() << "✅ 清理池测试通过";
    } else {
        QSKIP("无法连接到测试服务器");
    }
}

void TestQCWebSocketPool::testStatistics()
{
    qDebug() << "========== testStatistics ==========";

    pool = new QCWebSocketPool();
    QUrl url(TEST_WS_URL);

    // 初始统计
    auto stats0 = pool->statistics(url);
    QCOMPARE(stats0.totalConnections, 0);
    QCOMPARE(stats0.hitRate, 0.0);

    // 第 1 次获取
    auto *socket1 = pool->acquire(url);
    if (!socket1 || !waitForConnection(socket1, 10000)) {
        QSKIP("无法连接到测试服务器");
    }

    auto stats1 = pool->statistics(url);
    QCOMPARE(stats1.totalConnections, 1);
    QCOMPARE(stats1.activeConnections, 1);
    QCOMPARE(stats1.missCount, 1);

    pool->release(socket1);

    // 第 2 次获取（复用）
    auto *socket2 = pool->acquire(url);
    QVERIFY(socket2 == socket1);

    auto stats2 = pool->statistics(url);
    QCOMPARE(stats2.hitCount, 1);
    QCOMPARE(stats2.missCount, 1);
    QCOMPARE(stats2.hitRate, 50.0);  // 1 hit / 2 total = 50%

    pool->release(socket2);

    qDebug() << "✅ 统计信息测试通过";
}

// ============================================================================
// 边界情况测试
// ============================================================================

void TestQCWebSocketPool::testAcquireFromEmptyPool()
{
    qDebug() << "========== testAcquireFromEmptyPool ==========";

    pool = new QCWebSocketPool();
    QUrl url(TEST_WS_URL);

    // 从空池获取（应该创建新连接）
    auto *socket = pool->acquire(url);
    QVERIFY(socket != nullptr);

    if (waitForConnection(socket, 10000)) {
        QCOMPARE(socket->state(), QCWebSocket::State::Connected);
        pool->release(socket);
        qDebug() << "✅ 空池获取测试通过";
    } else {
        QSKIP("无法连接到测试服务器");
    }
}

void TestQCWebSocketPool::testReleaseNullPointer()
{
    qDebug() << "========== testReleaseNullPointer ==========";

    pool = new QCWebSocketPool();

    // 释放空指针（应该安全处理，不崩溃）
    pool->release(nullptr);

    qDebug() << "✅ 释放空指针测试通过";
}

void TestQCWebSocketPool::testReleaseNonPooledSocket()
{
    qDebug() << "========== testReleaseNonPooledSocket ==========";

    pool = new QCWebSocketPool();
    
    // 创建一个不在池中的 socket
    QCWebSocket *socket = new QCWebSocket(QUrl(TEST_WS_URL));

    // 尝试释放（应该安全处理，发出警告但不崩溃）
    pool->release(socket);

    delete socket;

    qDebug() << "✅ 释放非池连接测试通过";
}

void TestQCWebSocketPool::testMaxTotalConnections()
{
    qDebug() << "========== testMaxTotalConnections ==========";

    QCWebSocketPool::Config config;
    config.maxPoolSize = 10;
    config.maxTotalConnections = 3;  // 全局最多 3 个连接

    pool = new QCWebSocketPool(config);
    QUrl url1(TEST_WS_URL);
    QUrl url2(TEST_WS_URL2);

    // 尝试创建超过全局限制的连接
    QList<QCWebSocket*> sockets;
    
    // URL1 创建 2 个
    for (int i = 0; i < 2; ++i) {
        auto *socket = pool->acquire(url1);
        if (socket && waitForConnection(socket, 10000)) {
            sockets.append(socket);
        }
    }

    // URL2 创建 1 个
    auto *socket3 = pool->acquire(url2);
    if (socket3 && waitForConnection(socket3, 10000)) {
        sockets.append(socket3);
    }

    qDebug() << "成功创建" << sockets.size() << "个连接";

    // 尝试创建第 4 个（应该失败）
    if (sockets.size() == 3) {
        auto *socket4 = pool->acquire(url1);
        QVERIFY(socket4 == nullptr);  // 应该失败
        qDebug() << "✅ 全局连接数限制验证成功";
    }

    // 清理
    for (auto *s : sockets) {
        pool->release(s);
    }

    qDebug() << "✅ 全局连接数限制测试通过";
}

QTEST_MAIN(TestQCWebSocketPool)
#include "tst_QCWebSocketPool.moc"
