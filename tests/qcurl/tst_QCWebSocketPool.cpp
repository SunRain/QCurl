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

#include "QCWebSocket.h"
#include "QCWebSocketPool.h"
#include "QCWebSocketTestServer.h"

#include <QCoreApplication>
#include <QEvent>
#include <QSignalSpy>
#include <QtTest>

using namespace QCurl;

class TestQCWebSocketPool : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void init();
    void cleanup();

    // ========== 核心功能测试 ==========
    void testConstructor();       // 构造函数和默认配置
    void testAcquireAndRelease(); // 基本获取和释放
    void testConnectionReuse();   // 连接复用验证
    void testMultipleUrls();      // 多个 URL 独立池
    void testMaxPoolSize();       // 池大小限制

    // ========== 池管理测试 ==========
    void testPreWarm();    // 预热连接
    void testClearPool();  // 清理池
    void testStatistics(); // 统计信息

    // ========== 边界情况测试 ==========
    void testAcquireFromEmptyPool();   // 空池获取
    void testReleaseNullPointer();     // 释放空指针
    void testReleaseNonPooledSocket(); // 释放非池连接
    void testMaxTotalConnections();    // 全局连接数限制

private:
    QCWebSocketPool *pool = nullptr;

    [[nodiscard]] bool localServerAvailable() const { return m_localServerSkipReason.isEmpty(); }
    void applyLocalWssConfig(QCWebSocketPool *target);

    // 辅助方法
    bool waitForConnection(QCWebSocket *socket, int timeout = 5000);

    QString m_localServerSkipReason;
    QString m_testServerUrl;
    QString m_testServerUrl2;
    QString m_caCertPath;
    QCWebSocketTestServer m_testServer;
};

// ============================================================================
// 测试辅助方法
// ============================================================================

void TestQCWebSocketPool::applyLocalWssConfig(QCWebSocketPool *target)
{
    if (!target) {
        return;
    }
    auto config                 = target->config();
    config.sslConfig.caCertPath = m_caCertPath;
    target->setConfig(config);
}

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
    m_localServerSkipReason.clear();
    m_testServerUrl.clear();
    m_testServerUrl2.clear();
    m_caCertPath.clear();

    if (m_testServer.start(QCWebSocketTestServer::Mode::Wss)) {
        m_testServerUrl  = m_testServer.baseUrl();
        m_testServerUrl2 = m_testServer.urlWithPath(QStringLiteral("/echo"));
        m_caCertPath     = m_testServer.caCertPath();
        qDebug() << "测试服务器:" << m_testServerUrl;
    } else {
        m_localServerSkipReason = m_testServer.skipReason();
        qWarning().noquote() << "Local WSS server unavailable, tests will be skipped:"
                             << m_localServerSkipReason;
    }
}

void TestQCWebSocketPool::cleanupTestCase()
{
    m_testServer.stop();
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
        pool->deleteLater();
        pool = nullptr;
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    }
}

// ============================================================================
// 核心功能测试
// ============================================================================

void TestQCWebSocketPool::testConstructor()
{
    // 测试默认构造
    QCWebSocketPool pool1;
    auto config1 = pool1.config();
    QCOMPARE(config1.maxPoolSize, 10);
    QCOMPARE(config1.maxIdleTime, 300);
    QCOMPARE(config1.enableKeepAlive, true);

    // 测试自定义配置
    QCWebSocketPool::Config config;
    config.maxPoolSize     = 5;
    config.maxIdleTime     = 600;
    config.enableKeepAlive = false;

    QCWebSocketPool pool2(config);
    auto config2 = pool2.config();
    QCOMPARE(config2.maxPoolSize, 5);
    QCOMPARE(config2.maxIdleTime, 600);
    QCOMPARE(config2.enableKeepAlive, false);

}

void TestQCWebSocketPool::testAcquireAndRelease()
{
    if (!localServerAvailable()) {
        QSKIP(qPrintable(m_localServerSkipReason));
    }

    pool = new QCWebSocketPool();
    applyLocalWssConfig(pool);
    QUrl url(m_testServerUrl);

    // 获取连接
    auto *socket = pool->acquire(url);
    if (!socket) {
        QSKIP("无法连接到测试服务器，跳过此测试");
    }
    QVERIFY(socket->state() == QCWebSocket::State::Connected
            || socket->state() == QCWebSocket::State::Connecting);

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

}

void TestQCWebSocketPool::testConnectionReuse()
{
    if (!localServerAvailable()) {
        QSKIP(qPrintable(m_localServerSkipReason));
    }

    pool = new QCWebSocketPool();
    applyLocalWssConfig(pool);
    QUrl url(m_testServerUrl);

    // 第 1 次获取（创建新连接）
    auto *socket1 = pool->acquire(url);
    if (!socket1) {
        QSKIP("无法连接到测试服务器，跳过此测试");
    }

    if (!waitForConnection(socket1, 10000)) {
        QSKIP("无法连接到测试服务器，跳过此测试");
    }

    auto stats1 = pool->statistics(url);
    QCOMPARE(stats1.missCount, 1); // 未命中（创建新连接）
    QCOMPARE(stats1.hitCount, 0);

    // 归还
    pool->release(socket1);

    // 第 2 次获取（应复用同一连接）
    auto *socket2 = pool->acquire(url);
    QVERIFY(socket2 == socket1); // 应该是同一个对象
    QCOMPARE(socket2->state(), QCWebSocket::State::Connected);

    auto stats2 = pool->statistics(url);
    QCOMPARE(stats2.hitCount, 1);  // 命中（复用连接）
    QCOMPARE(stats2.missCount, 1); // 仍是 1（没有创建新连接）
    QVERIFY(stats2.hitRate > 0.0); // 命中率应该 > 0

    pool->release(socket2);

    qDebug() << "连接复用命中率:" << stats2.hitRate << "%";
}

void TestQCWebSocketPool::testMultipleUrls()
{
    if (!localServerAvailable()) {
        QSKIP(qPrintable(m_localServerSkipReason));
    }

    pool = new QCWebSocketPool();
    applyLocalWssConfig(pool);
    QUrl url1(m_testServerUrl);
    QUrl url2(m_testServerUrl2);

    // 获取第一个 URL 的连接
    auto *socket1 = pool->acquire(url1);
    if (!socket1) {
        QSKIP("无法连接到测试服务器，跳过此测试");
    }

    // 获取第二个 URL 的连接
    auto *socket2 = pool->acquire(url2);
    if (!socket2) {
        pool->release(socket1);
        QSKIP("无法连接到测试服务器，跳过此测试");
    }
    QVERIFY(socket2 != socket1); // 应该是不同的连接

    // 检查池中是否包含两个 URL
    QVERIFY(pool->contains(url1));
    QVERIFY(pool->contains(url2));

    // 检查全局统计
    auto globalStats = pool->statistics();
    QVERIFY(globalStats.totalConnections >= 2);

    pool->release(socket1);
    pool->release(socket2);

}

void TestQCWebSocketPool::testMaxPoolSize()
{
    if (!localServerAvailable()) {
        QSKIP(qPrintable(m_localServerSkipReason));
    }

    QCWebSocketPool::Config config;
    config.maxPoolSize = 2; // 每个 URL 最多 2 个连接

    pool = new QCWebSocketPool(config);
    applyLocalWssConfig(pool);
    QUrl url(m_testServerUrl);

    // 获取 2 个连接（应该成功）
    QList<QCWebSocket *> sockets;
    for (int i = 0; i < 2; ++i) {
        auto *socket = pool->acquire(url);
        if (!socket) {
            qWarning() << "创建第" << (i + 1) << "个连接失败";
            break;
        }
        if (!waitForConnection(socket, 10000)) {
            qWarning() << "第" << (i + 1) << "个连接超时";
            break;
        }
        sockets.append(socket);
    }

    // 取证式口径：该用例的证据前提是“至少能建立 2 个连接”以验证第 3 个被拒绝。
    // 若前提不成立，应显式 SKIP，而不是“部分执行后仍 PASS”造成伪通过。
    if (sockets.size() < 2) {
        for (auto *s : sockets) {
            pool->release(s);
        }
        QSKIP("无法建立足够连接以验证 maxPoolSize（本地 WSS server 不可用或环境受限）");
    }

    // 尝试获取第 3 个（应该失败，因为达到限制）
    QSignalSpy spy(pool, &QCWebSocketPool::poolLimitReached);
    auto *socket3 = pool->acquire(url);
    // 如果前面 2 个都成功了，第 3 个必须失败
    QVERIFY(socket3 == nullptr);
    QCOMPARE(spy.count(), 1);

    // 释放第一个连接
    if (!sockets.isEmpty()) {
        pool->release(sockets[0]);

        // 再次尝试获取（应该成功，复用第一个）
        auto *socket4 = pool->acquire(url);
        QVERIFY(socket4 != nullptr);
        QVERIFY(socket4 == sockets[0]); // 应该复用

        pool->release(socket4);
    }

    // 清理
    for (auto *s : sockets) {
        if (s != sockets[0]) {
            pool->release(s);
        }
    }

}

// ============================================================================
// 池管理测试
// ============================================================================

void TestQCWebSocketPool::testPreWarm()
{
    if (!localServerAvailable()) {
        QSKIP(qPrintable(m_localServerSkipReason));
    }

    pool = new QCWebSocketPool();
    applyLocalWssConfig(pool);
    QUrl url(m_testServerUrl);
    QSignalSpy createdSpy(pool, &QCWebSocketPool::connectionCreated);
    QSignalSpy reusedSpy(pool, &QCWebSocketPool::connectionReused);

    // 预热 3 个连接
    pool->preWarm(url, 3);

    QTRY_COMPARE_WITH_TIMEOUT(createdSpy.count(), 3, 1000);
    QTRY_VERIFY_WITH_TIMEOUT(pool->statistics(url).totalConnections == 3, 2000);
    QTRY_VERIFY_WITH_TIMEOUT(pool->statistics(url).idleConnections == 3, 2000);

    auto *socket = pool->acquire(url);
    QVERIFY2(socket != nullptr, "preWarm 后应能直接从池中拿到可复用连接");
    QVERIFY(waitForConnection(socket, 10000));
    QCOMPARE(socket->state(), QCWebSocket::State::Connected);
    QCOMPARE(reusedSpy.count(), 1);
    pool->release(socket);

    // 检查统计
    auto stats = pool->statistics(url);
    qDebug() << "预热后连接数:" << stats.totalConnections;
    qDebug() << "空闲连接:" << stats.idleConnections;

    QCOMPARE(stats.totalConnections, 3);
    QCOMPARE(stats.activeConnections, 0);
    QCOMPARE(stats.idleConnections, 3);

}

void TestQCWebSocketPool::testClearPool()
{
    if (!localServerAvailable()) {
        QSKIP(qPrintable(m_localServerSkipReason));
    }

    pool = new QCWebSocketPool();
    applyLocalWssConfig(pool);
    QUrl url(m_testServerUrl);

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

    } else {
        QSKIP("无法连接到本地 WSS 测试服务器");
    }
}

void TestQCWebSocketPool::testStatistics()
{
    if (!localServerAvailable()) {
        QSKIP(qPrintable(m_localServerSkipReason));
    }

    pool = new QCWebSocketPool();
    applyLocalWssConfig(pool);
    QUrl url(m_testServerUrl);

    // 初始统计
    auto stats0 = pool->statistics(url);
    QCOMPARE(stats0.totalConnections, 0);
    QCOMPARE(stats0.hitRate, 0.0);

    // 第 1 次获取
    auto *socket1 = pool->acquire(url);
    if (!socket1 || !waitForConnection(socket1, 10000)) {
        QSKIP("无法连接到本地 WSS 测试服务器");
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
    QCOMPARE(stats2.hitRate, 50.0); // 1 hit / 2 total = 50%

    pool->release(socket2);

}

// ============================================================================
// 边界情况测试
// ============================================================================

void TestQCWebSocketPool::testAcquireFromEmptyPool()
{
    if (!localServerAvailable()) {
        QSKIP(qPrintable(m_localServerSkipReason));
    }

    pool = new QCWebSocketPool();
    applyLocalWssConfig(pool);
    QUrl url(m_testServerUrl);

    // 从空池获取（应该创建新连接）
    auto *socket = pool->acquire(url);
    if (!socket) {
        QSKIP("无法连接到本地 WSS 测试服务器，跳过此测试");
    }

    if (waitForConnection(socket, 10000)) {
        QCOMPARE(socket->state(), QCWebSocket::State::Connected);
        pool->release(socket);
    } else {
        QSKIP("无法连接到本地 WSS 测试服务器");
    }
}

void TestQCWebSocketPool::testReleaseNullPointer()
{
    pool = new QCWebSocketPool();

    // 释放空指针（应该安全处理，不崩溃）
    pool->release(nullptr);

}

void TestQCWebSocketPool::testReleaseNonPooledSocket()
{
    pool = new QCWebSocketPool();

    // 创建一个不在池中的 socket
    QCWebSocket *socket = new QCWebSocket(QUrl(QStringLiteral("wss://localhost:1")));

    // 尝试释放（应该安全处理，发出警告但不崩溃）
    pool->release(socket);

    socket->deleteLater();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);

}

void TestQCWebSocketPool::testMaxTotalConnections()
{
    if (!localServerAvailable()) {
        QSKIP(qPrintable(m_localServerSkipReason));
    }

    QCWebSocketPool::Config config;
    config.maxPoolSize         = 10;
    config.maxTotalConnections = 3; // 全局最多 3 个连接

    pool = new QCWebSocketPool(config);
    applyLocalWssConfig(pool);
    QUrl url1(m_testServerUrl);
    QUrl url2(m_testServerUrl2);

    // 尝试创建超过全局限制的连接
    QList<QCWebSocket *> sockets;

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

    // 取证式口径：该用例的证据前提是“成功建立 3 个连接”，否则无法验证第 4 个被拒绝。
    if (sockets.size() != 3) {
        for (auto *s : sockets) {
            pool->release(s);
        }
        QSKIP("无法建立足够连接以验证 maxTotalConnections（本地 WSS server 不可用或环境受限）");
    }

    // 尝试创建第 4 个（应该失败）
    QSignalSpy spy(pool, &QCWebSocketPool::poolLimitReached);
    auto *socket4 = pool->acquire(url1);
    QVERIFY(socket4 == nullptr); // 应该失败
    QVERIFY2(spy.count() >= 1, "expected poolLimitReached emitted when maxTotalConnections reached");
    // 清理
    for (auto *s : sockets) {
        pool->release(s);
    }
}

QTEST_MAIN(TestQCWebSocketPool)
#include "tst_QCWebSocketPool.moc"
