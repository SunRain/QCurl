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
#include "test_wait_utils.h"
#include "test_websocket_evidence_utils.h"

#include <QCoreApplication>
#include <QEvent>
#include <QFuture>
#include <QHash>
#include <QJsonObject>
#include <QObjectCleanupHandler>
#include <QPointer>
#include <QSignalSpy>
#include <QTimer>
#include <QUrlQuery>
#include <QtTest>

#include <limits>
#include <thread>

using namespace QCurl;

class TestQCWebSocketPool : public QObject
{
    Q_OBJECT

private Q_SLOTS:
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
    void testAcquireFromEmptyPool(); // 空池获取
    void testReleaseInvalidLease();  // 拒绝无效 lease
    void testReleaseForeignLease();  // 拒绝其他连接池的 lease
    void testMaxTotalConnections();  // 全局连接数限制
    void testAcquireResultCarriesPureValueLease();
    void testLeaseResolveReleaseLifecycle();
    void testWrongThreadAcquireFailsWithoutSideEffects();
    void testWrongThreadMutatorsFailWithoutSideEffects();
    void testPublicSignalsAllowSynchronousReentry_data();
    void testPublicSignalsAllowSynchronousReentry();
    void testKeepAlivePongMatrix_data();
    void testKeepAlivePongMatrix();
    void testPendingAcquireCompletesOnClearAndDestroy();
    void testPreWarmReleaseFailureDoesNotWarm();
    void testBorrowedHandleInvalidatesOnExternalDelete_data();
    void testBorrowedHandleInvalidatesOnExternalDelete();
    void testDestroyedSignalCleansIndexesAndPending();
    void testDestroyingRejectsReentry_data();
    void testDestroyingRejectsReentry();
    void testDestroyingMutatorsAndQueries();
    void testPreWarmCountValidation_data();
    void testPreWarmCountValidation();
    void testInvalidConfigLeavesStateUnchanged();
    void testKeepAliveIntervalOverflowRejected();

private:
    QCWebSocketPool *pool = nullptr;
    QHash<QCWebSocket *, QCWebSocketPool::LeaseId> m_socketLeases;

    void applyLocalWssConfig(QCWebSocketPool *target);
    QCWebSocket *resolveSocket(const QCWebSocketAcquireResult &result);
    void releaseSocket(QCWebSocket *socket);

    // 辅助方法
    bool waitForConnection(QCWebSocket *socket, int timeout = 5000);
    QCWebSocketAcquireResult awaitAcquire(const QUrl &url, int timeout = 10000);
    QCWebSocketPreWarmResult awaitPreWarm(const QUrl &url, int count, int timeout = 15000);
    QCWebSocket *acquireIdleSocket(const QUrl &url);

    QString m_testServerUrl;
    QString m_testServerUrl2;
    QString m_artifactsPath;
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
    auto config    = target->config();
    auto sslConfig = config.sslConfig();
    sslConfig.setCaCertPath(m_caCertPath);
    config.setSslConfig(sslConfig);
    QString error;
    QVERIFY2(target->setConfig(config, &error), qPrintable(error));
}

void TestQCWebSocketPool::releaseSocket(QCWebSocket *socket)
{
    QVERIFY(socket != nullptr);
    const auto leaseIt = m_socketLeases.constFind(socket);
    QVERIFY2(leaseIt != m_socketLeases.cend(), "测试未记录 socket 对应的活动 lease");
    const auto leaseId = *leaseIt;
    m_socketLeases.erase(leaseIt);
    QCOMPARE(pool->release(leaseId), QCWebSocketPool::LeaseResult::Success);
}

QCWebSocket *TestQCWebSocketPool::resolveSocket(const QCWebSocketAcquireResult &result)
{
    if (!result.isSuccess()) {
        return nullptr;
    }
    QCWebSocket *socket      = nullptr;
    const auto resolveResult = pool->resolveLease(result.leaseId(), &socket);
    if (resolveResult != QCWebSocketPool::LeaseResult::Success || !socket) {
        return nullptr;
    }
    m_socketLeases.insert(socket, result.leaseId());
    return socket;
}

bool TestQCWebSocketPool::waitForConnection(QCWebSocket *socket, int timeout)
{
    if (!socket) {
        return false;
    }

    if (socket->state() == QCWebSocket::State::Connected) {
        return true;
    }

    QSignalSpy spy(socket, &QCWebSocket::connected);
    return TestWaitUtils::waitUntil(
        [&]() { return socket->state() == QCWebSocket::State::Connected || spy.count() > 0; },
        timeout);
}

QCWebSocketAcquireResult TestQCWebSocketPool::awaitAcquire(const QUrl &url, int timeout)
{
    auto future = pool->acquire(url);
    if (!TestWaitUtils::waitUntil([&future]() { return future.isFinished(); }, timeout)) {
        return QCWebSocketAcquireResult::failure(QCWebSocketAcquireResult::Status::ConnectionFailed,
                                                 QStringLiteral("acquire watchdog timeout"));
    }
    return future.result();
}

QCWebSocketPreWarmResult TestQCWebSocketPool::awaitPreWarm(const QUrl &url, int count, int timeout)
{
    auto future = pool->preWarm(url, count);
    if (!TestWaitUtils::waitUntil([&future]() { return future.isFinished(); }, timeout)) {
        return QCWebSocketPreWarmResult::failure(QCWebSocketPreWarmResult::Status::ConnectionFailed,
                                                 count,
                                                 0,
                                                 QStringLiteral("preWarm watchdog timeout"));
    }
    return future.result();
}

QCWebSocket *TestQCWebSocketPool::acquireIdleSocket(const QUrl &url)
{
    const auto result = awaitAcquire(url);
    if (result.status() != QCWebSocketAcquireResult::Status::Success) {
        return nullptr;
    }
    auto *socket = resolveSocket(result);
    releaseSocket(socket);
    return socket;
}

// ============================================================================
// 测试生命周期
// ============================================================================

void TestQCWebSocketPool::initTestCase()
{
    m_testServerUrl.clear();
    m_testServerUrl2.clear();
    m_artifactsPath.clear();
    m_caCertPath.clear();

    QVERIFY2(m_testServer.start(QCWebSocketTestServer::Mode::Wss,
                                QCWebSocketTestServer::ServerKind::Evidence),
             qPrintable(m_testServer.skipReason()));
    m_testServerUrl  = m_testServer.baseUrl();
    m_testServerUrl2 = m_testServer.urlWithPath(QStringLiteral("/echo"));
    m_artifactsPath  = m_testServer.artifactsPath();
    m_caCertPath     = m_testServer.caCertPath();
    qDebug() << "测试服务器:" << m_testServerUrl;
    qDebug() << "WSS evidence artifacts:" << m_artifactsPath;
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
        QString error;
        QVERIFY2(pool->clearPool({}, &error), qPrintable(error));
        pool->deleteLater();
        pool = nullptr;
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    }
    m_socketLeases.clear();
}

// ============================================================================
// 核心功能测试
// ============================================================================

void TestQCWebSocketPool::testConstructor()
{
    // 测试默认构造
    QCWebSocketPool pool1;
    auto config1 = pool1.config();
    QCOMPARE(config1.maxPoolSize(), 10);
    QCOMPARE(config1.maxIdleTime(), 300);
    QCOMPARE(config1.enableKeepAlive(), true);

    // 测试自定义配置
    QCWebSocketPoolConfig config;
    config.setMaxPoolSize(5);
    config.setMaxIdleTime(600);
    config.setEnableKeepAlive(false);

    QCWebSocketPool pool2(config);
    auto config2 = pool2.config();
    QCOMPARE(config2.maxPoolSize(), 5);
    QCOMPARE(config2.maxIdleTime(), 600);
    QCOMPARE(config2.enableKeepAlive(), false);
}

void TestQCWebSocketPool::testAcquireAndRelease()
{
    QVERIFY2(!m_artifactsPath.isEmpty(),
             "WSS evidence artifactsPath 为空，无法复核 acquire 握手证据。");

    pool = new QCWebSocketPool();
    applyLocalWssConfig(pool);
    const QString caseId = QString::fromLatin1(QTest::currentTestFunction());
    const QUrl url       = TestWebSocketEvidenceUtils::buildCaseUrl(m_testServerUrl,
                                                                    QStringLiteral("/"),
                                                                    caseId);

    // 获取连接
    const auto acquireResult = awaitAcquire(url);
    QCOMPARE(acquireResult.status(), QCWebSocketAcquireResult::Status::Success);
    auto *socket = resolveSocket(acquireResult);
    QVERIFY2(socket != nullptr, "无法连接到测试服务器");
    QVERIFY(socket->state() == QCWebSocket::State::Connected
            || socket->state() == QCWebSocket::State::Connecting);

    // 等待连接完成
    QVERIFY2(waitForConnection(socket, 10000), "无法连接到测试服务器");

    QCOMPARE(socket->state(), QCWebSocket::State::Connected);

    // 检查统计信息
    auto stats = pool->statistics(url);
    QCOMPARE(stats.totalConnections(), 1);
    QCOMPARE(stats.activeConnections(), 1);
    QCOMPARE(stats.idleConnections(), 0);

    // 归还连接
    QCOMPARE(pool->release(acquireResult.leaseId()), QCWebSocketPool::LeaseResult::Success);
    m_socketLeases.remove(socket);

    // 检查统计信息
    stats = pool->statistics(url);
    QCOMPARE(stats.totalConnections(), 1);
    QCOMPARE(stats.activeConnections(), 0);
    QCOMPARE(stats.idleConnections(), 1);

    const QString handshakeError = TestWebSocketEvidenceUtils::verifyHandshakeEvidence(
        m_artifactsPath, caseId, QStringLiteral("/"), true, 1, 2000);
    QVERIFY2(handshakeError.isEmpty(), qPrintable(handshakeError));
}

void TestQCWebSocketPool::testConnectionReuse()
{
    QVERIFY2(!m_artifactsPath.isEmpty(), "WSS evidence artifactsPath 为空，无法复核复用握手证据。");

    pool = new QCWebSocketPool();
    applyLocalWssConfig(pool);
    const QString caseId = QString::fromLatin1(QTest::currentTestFunction());
    const QUrl url       = TestWebSocketEvidenceUtils::buildCaseUrl(m_testServerUrl,
                                                                    QStringLiteral("/"),
                                                                    caseId);

    // 第 1 次获取（创建新连接）
    auto *socket1 = resolveSocket(awaitAcquire(url));
    QVERIFY2(socket1 != nullptr, "无法连接到测试服务器");

    QVERIFY2(waitForConnection(socket1, 10000), "无法连接到测试服务器");

    auto stats1 = pool->statistics(url);
    QCOMPARE(stats1.missCount(), 1); // 未命中（创建新连接）
    QCOMPARE(stats1.hitCount(), 0);

    // 归还
    releaseSocket(socket1);

    // 第 2 次获取（应复用同一连接）
    auto *socket2 = resolveSocket(awaitAcquire(url));
    QVERIFY(socket2 == socket1); // 应该是同一个对象
    QCOMPARE(socket2->state(), QCWebSocket::State::Connected);

    auto stats2 = pool->statistics(url);
    QCOMPARE(stats2.hitCount(), 1);  // 命中（复用连接）
    QCOMPARE(stats2.missCount(), 1); // 仍是 1（没有创建新连接）
    QVERIFY(stats2.hitRate() > 0.0); // 命中率应该 > 0

    releaseSocket(socket2);

    const QString handshakeError = TestWebSocketEvidenceUtils::verifyHandshakeEvidence(
        m_artifactsPath, caseId, QStringLiteral("/"), true, 1, 2000);
    QVERIFY2(handshakeError.isEmpty(), qPrintable(handshakeError));

    qDebug() << "连接复用命中率:" << stats2.hitRate() << "%";
}

void TestQCWebSocketPool::testMultipleUrls()
{
    pool = new QCWebSocketPool();
    applyLocalWssConfig(pool);
    QUrl url1(m_testServerUrl);
    QUrl url2(m_testServerUrl2);

    // 获取第一个 URL 的连接
    auto *socket1 = resolveSocket(awaitAcquire(url1));
    QVERIFY2(socket1 != nullptr, "无法连接到测试服务器");

    // 获取第二个 URL 的连接
    auto *socket2 = resolveSocket(awaitAcquire(url2));
    QVERIFY2(socket2 != nullptr, "无法连接到测试服务器");
    QVERIFY(socket2 != socket1); // 应该是不同的连接

    // 检查池中是否包含两个 URL
    QVERIFY(pool->contains(url1));
    QVERIFY(pool->contains(url2));

    // 检查全局统计
    auto globalStats = pool->statistics();
    QVERIFY(globalStats.totalConnections() >= 2);

    releaseSocket(socket1);
    releaseSocket(socket2);
}

void TestQCWebSocketPool::testMaxPoolSize()
{
    QCWebSocketPoolConfig config;
    config.setMaxPoolSize(2); // 每个 URL 最多 2 个连接

    pool = new QCWebSocketPool(config);
    applyLocalWssConfig(pool);
    QUrl url(m_testServerUrl);

    // 获取 2 个连接（应该成功）
    QList<QCWebSocket *> sockets;
    for (int i = 0; i < 2; ++i) {
        auto *socket = resolveSocket(awaitAcquire(url));
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
    QVERIFY2(sockets.size() >= 2,
             "无法建立足够连接以验证 maxPoolSize（本地 WSS server 不可用或环境受限）");

    // 尝试获取第 3 个（应该失败，因为达到限制）
    QSignalSpy spy(pool, &QCWebSocketPool::poolLimitReached);
    const auto limitResult = awaitAcquire(url);
    auto *socket3          = resolveSocket(limitResult);
    // 如果前面 2 个都成功了，第 3 个必须失败
    QVERIFY(socket3 == nullptr);
    QCOMPARE(limitResult.status(), QCWebSocketAcquireResult::Status::PoolLimitReached);
    QCOMPARE(spy.count(), 1);

    // 释放第一个连接
    if (!sockets.isEmpty()) {
        releaseSocket(sockets[0]);

        // 再次尝试获取（应该成功，复用第一个）
        auto *socket4 = resolveSocket(awaitAcquire(url));
        QVERIFY(socket4 != nullptr);
        QVERIFY(socket4 == sockets[0]); // 应该复用
        releaseSocket(socket4);
    }

    // 清理
    for (auto *s : sockets) {
        if (s != sockets[0]) {
            releaseSocket(s);
        }
    }
}

// ============================================================================
// 池管理测试
// ============================================================================

void TestQCWebSocketPool::testPreWarm()
{
    pool = new QCWebSocketPool();
    applyLocalWssConfig(pool);
    QUrl url(m_testServerUrl);
    QSignalSpy createdSpy(pool, &QCWebSocketPool::connectionCreated);
    QSignalSpy reusedSpy(pool, &QCWebSocketPool::connectionReused);

    // 预热 3 个连接
    const auto preWarmResult = awaitPreWarm(url, 3);
    QCOMPARE(preWarmResult.status(), QCWebSocketPreWarmResult::Status::Success);
    QCOMPARE(preWarmResult.warmedCount(), 3);

    QTRY_COMPARE_WITH_TIMEOUT(createdSpy.count(), 3, 1000);
    QTRY_VERIFY_WITH_TIMEOUT(pool->statistics(url).totalConnections() == 3, 2000);
    QTRY_VERIFY_WITH_TIMEOUT(pool->statistics(url).idleConnections() == 3, 2000);

    auto *socket = resolveSocket(awaitAcquire(url));
    QVERIFY2(socket != nullptr, "preWarm 后应能直接从池中拿到可复用连接");
    QVERIFY(waitForConnection(socket, 10000));
    QCOMPARE(socket->state(), QCWebSocket::State::Connected);
    QCOMPARE(reusedSpy.count(), 1);
    releaseSocket(socket);

    // 检查统计
    auto stats = pool->statistics(url);
    qDebug() << "预热后连接数:" << stats.totalConnections();
    qDebug() << "空闲连接:" << stats.idleConnections();

    QCOMPARE(stats.totalConnections(), 3);
    QCOMPARE(stats.activeConnections(), 0);
    QCOMPARE(stats.idleConnections(), 3);
}

void TestQCWebSocketPool::testClearPool()
{
    pool = new QCWebSocketPool();
    applyLocalWssConfig(pool);
    QUrl url(m_testServerUrl);
    QString clearError = QStringLiteral("stale error");

    QVERIFY2(pool->clearPool(url, &clearError), qPrintable(clearError));
    QVERIFY(clearError.isEmpty());

    // 创建一些连接
    auto *socket1 = resolveSocket(awaitAcquire(url));
    QVERIFY2(socket1 != nullptr, "无法连接到本地 WSS 测试服务器");
    QVERIFY2(waitForConnection(socket1, 10000), "无法连接到本地 WSS 测试服务器");
    releaseSocket(socket1);

    // 清理池
    clearError = QStringLiteral("stale error");
    QVERIFY2(pool->clearPool(url, &clearError), qPrintable(clearError));
    QVERIFY(clearError.isEmpty());

    // 检查统计
    auto stats = pool->statistics(url);
    QCOMPARE(stats.totalConnections(), 0);
    QVERIFY(!pool->contains(url));
    QVERIFY(pool->clearPool());
}

void TestQCWebSocketPool::testStatistics()
{
    pool = new QCWebSocketPool();
    applyLocalWssConfig(pool);
    QUrl url(m_testServerUrl);

    // 初始统计
    auto stats0 = pool->statistics(url);
    QCOMPARE(stats0.totalConnections(), 0);
    QCOMPARE(stats0.hitRate(), 0.0);

    // 第 1 次获取
    auto *socket1 = resolveSocket(awaitAcquire(url));
    QVERIFY2(socket1 != nullptr, "无法连接到本地 WSS 测试服务器");
    QVERIFY2(waitForConnection(socket1, 10000), "无法连接到本地 WSS 测试服务器");

    auto stats1 = pool->statistics(url);
    QCOMPARE(stats1.totalConnections(), 1);
    QCOMPARE(stats1.activeConnections(), 1);
    QCOMPARE(stats1.missCount(), 1);

    releaseSocket(socket1);

    // 第 2 次获取（复用）
    auto *socket2 = resolveSocket(awaitAcquire(url));
    QVERIFY(socket2 == socket1);

    auto stats2 = pool->statistics(url);
    QCOMPARE(stats2.hitCount(), 1);
    QCOMPARE(stats2.missCount(), 1);
    QCOMPARE(stats2.hitRate(), 50.0); // 1 hit / 2 total = 50%

    releaseSocket(socket2);
}

// ============================================================================
// 边界情况测试
// ============================================================================

void TestQCWebSocketPool::testAcquireFromEmptyPool()
{
    pool = new QCWebSocketPool();
    applyLocalWssConfig(pool);
    QUrl url(m_testServerUrl);

    // 从空池获取（应该创建新连接）
    auto *socket = resolveSocket(awaitAcquire(url));
    QVERIFY2(socket != nullptr, "无法连接到本地 WSS 测试服务器");
    QVERIFY2(waitForConnection(socket, 10000), "无法连接到本地 WSS 测试服务器");
    QCOMPARE(socket->state(), QCWebSocket::State::Connected);
    releaseSocket(socket);
}

void TestQCWebSocketPool::testReleaseInvalidLease()
{
    pool = new QCWebSocketPool();

    QCOMPARE(pool->release(0), QCWebSocketPool::LeaseResult::UnknownLease);
    QCOMPARE(pool->resolveLease(0, nullptr), QCWebSocketPool::LeaseResult::InvalidArgument);
    QCOMPARE(pool->statistics().totalConnections(), 0);
}

void TestQCWebSocketPool::testReleaseForeignLease()
{
    pool = new QCWebSocketPool();
    applyLocalWssConfig(pool);
    const auto result = awaitAcquire(QUrl(m_testServerUrl));
    QCOMPARE(result.status(), QCWebSocketAcquireResult::Status::Success);

    QCWebSocketPool otherPool;
    QCOMPARE(otherPool.release(result.leaseId()), QCWebSocketPool::LeaseResult::UnknownLease);
    QCOMPARE(pool->release(result.leaseId()), QCWebSocketPool::LeaseResult::Success);
}

void TestQCWebSocketPool::testMaxTotalConnections()
{
    QCWebSocketPoolConfig config;
    config.setMaxPoolSize(10);
    config.setMaxTotalConnections(3); // 全局最多 3 个连接

    pool = new QCWebSocketPool(config);
    applyLocalWssConfig(pool);
    QUrl url1(m_testServerUrl);
    QUrl url2(m_testServerUrl2);

    // 尝试创建超过全局限制的连接
    QList<QCWebSocket *> sockets;

    // URL1 创建 2 个
    for (int i = 0; i < 2; ++i) {
        auto *socket = resolveSocket(awaitAcquire(url1));
        if (socket && waitForConnection(socket, 10000)) {
            sockets.append(socket);
        }
    }

    // URL2 创建 1 个
    auto *socket3 = resolveSocket(awaitAcquire(url2));
    if (socket3 && waitForConnection(socket3, 10000)) {
        sockets.append(socket3);
    }

    qDebug() << "成功创建" << sockets.size() << "个连接";

    // 取证式口径：该用例的证据前提是“成功建立 3 个连接”，否则无法验证第 4 个被拒绝。
    QVERIFY2(sockets.size() == 3,
             "无法建立足够连接以验证 maxTotalConnections（本地 WSS server 不可用或环境受限）");

    // 尝试创建第 4 个（应该失败）
    QSignalSpy spy(pool, &QCWebSocketPool::poolLimitReached);
    const auto limitResult = awaitAcquire(url1);
    auto *socket4          = resolveSocket(limitResult);
    QVERIFY(socket4 == nullptr); // 应该失败
    QCOMPARE(limitResult.status(), QCWebSocketAcquireResult::Status::PoolLimitReached);
    QVERIFY2(spy.count() >= 1, "expected poolLimitReached emitted when maxTotalConnections reached");
    // 清理
    for (auto *s : sockets) {
        releaseSocket(s);
    }
}

void TestQCWebSocketPool::testAcquireResultCarriesPureValueLease()
{
    constexpr QCWebSocketAcquireResult::LeaseId leaseId = 42;
    const auto result = QCWebSocketAcquireResult::success(leaseId);

    QCWebSocketAcquireResult copiedResult;
    std::thread worker([&copiedResult, result]() { copiedResult = result; });
    worker.join();

    QCOMPARE(copiedResult.status(), QCWebSocketAcquireResult::Status::Success);
    QCOMPARE(copiedResult.leaseId(), leaseId);
    QVERIFY(copiedResult.error().isEmpty());

    const auto failure
        = QCWebSocketAcquireResult::failure(QCWebSocketAcquireResult::Status::ConnectionFailed,
                                            QStringLiteral("connection failed"));
    QCOMPARE(failure.leaseId(), QCWebSocketAcquireResult::LeaseId{0});
}

void TestQCWebSocketPool::testLeaseResolveReleaseLifecycle()
{
    pool = new QCWebSocketPool();
    applyLocalWssConfig(pool);
    const QUrl url(m_testServerUrl);

    const auto acquireResult = awaitAcquire(url);
    QCOMPARE(acquireResult.status(), QCWebSocketAcquireResult::Status::Success);
    QVERIFY(acquireResult.leaseId() != 0);

    QCWebSocket *socket = nullptr;
    QCOMPARE(pool->resolveLease(acquireResult.leaseId(), &socket),
             QCWebSocketPool::LeaseResult::Success);
    QVERIFY(socket != nullptr);
    QVERIFY2(waitForConnection(socket, 10000), "无法连接到本地 WSS 测试服务器");

    QCOMPARE(pool->release(acquireResult.leaseId()), QCWebSocketPool::LeaseResult::Success);
    socket = reinterpret_cast<QCWebSocket *>(quintptr{1});
    QCOMPARE(pool->resolveLease(acquireResult.leaseId(), &socket),
             QCWebSocketPool::LeaseResult::InactiveLease);
    QCOMPARE(socket, nullptr);
    QCOMPARE(pool->release(acquireResult.leaseId()), QCWebSocketPool::LeaseResult::InactiveLease);

    const auto secondResult = awaitAcquire(url);
    QCOMPARE(secondResult.status(), QCWebSocketAcquireResult::Status::Success);
    QVERIFY(secondResult.leaseId() != acquireResult.leaseId());
    QString clearError;
    QVERIFY2(pool->clearPool(url, &clearError), qPrintable(clearError));
    QCOMPARE(pool->release(secondResult.leaseId()), QCWebSocketPool::LeaseResult::InactiveLease);
    QCOMPARE(pool->release(secondResult.leaseId() + 100),
             QCWebSocketPool::LeaseResult::UnknownLease);
}

void TestQCWebSocketPool::testWrongThreadAcquireFailsWithoutSideEffects()
{
    pool = new QCWebSocketPool();
    applyLocalWssConfig(pool);
    const QUrl url(m_testServerUrl);
    QFuture<QCWebSocketAcquireResult> future;
    QFuture<QCWebSocketPreWarmResult> preWarmFuture;

    std::thread worker([this, &future, &preWarmFuture, url]() {
        future        = pool->acquire(url);
        preWarmFuture = pool->preWarm(url, 2);
    });
    worker.join();

    QVERIFY(future.isFinished());
    QCOMPARE(future.result().status(), QCWebSocketAcquireResult::Status::WrongThread);
    QCOMPARE(future.result().leaseId(), QCWebSocketPool::LeaseId{0});
    QVERIFY(preWarmFuture.isFinished());
    QCOMPARE(preWarmFuture.result().status(), QCWebSocketPreWarmResult::Status::WrongThread);
    QCOMPARE(preWarmFuture.result().warmedCount(), 0);
    QCOMPARE(pool->statistics().totalConnections(), 0);
}

void TestQCWebSocketPool::testWrongThreadMutatorsFailWithoutSideEffects()
{
    pool = new QCWebSocketPool();
    applyLocalWssConfig(pool);
    const QUrl url(m_testServerUrl);

    const auto acquireResult = awaitAcquire(url);
    QCOMPARE(acquireResult.status(), QCWebSocketAcquireResult::Status::Success);
    auto *socket = resolveSocket(acquireResult);
    QVERIFY(socket != nullptr);
    QVERIFY2(waitForConnection(socket, 10000), "无法连接到本地 WSS 测试服务器");

    const auto beforeConfig = pool->config();
    const auto beforeStats  = pool->statistics(url);
    auto changedConfig      = beforeConfig;
    changedConfig.setMaxPoolSize(beforeConfig.maxPoolSize() + 1);

    auto releaseResult             = QCWebSocketPool::LeaseResult::Success;
    auto resolveResult             = QCWebSocketPool::LeaseResult::Success;
    QCWebSocket *wrongThreadSocket = socket;
    bool clearResult               = true;
    bool configResult              = true;
    QString clearError;
    QString configError;
    std::thread worker([&]() {
        resolveResult = pool->resolveLease(acquireResult.leaseId(), &wrongThreadSocket);
        releaseResult = pool->release(acquireResult.leaseId());
        clearResult   = pool->clearPool(url, &clearError);
        configResult  = pool->setConfig(changedConfig, &configError);
    });
    worker.join();

    QCOMPARE(resolveResult, QCWebSocketPool::LeaseResult::WrongThread);
    QCOMPARE(wrongThreadSocket, nullptr);
    QCOMPARE(releaseResult, QCWebSocketPool::LeaseResult::WrongThread);
    QVERIFY(!clearResult);
    QVERIFY(!configResult);
    QVERIFY(!clearError.isEmpty());
    QVERIFY(!configError.isEmpty());

    const auto afterStats  = pool->statistics(url);
    const auto afterConfig = pool->config();
    QCOMPARE(afterStats.totalConnections(), beforeStats.totalConnections());
    QCOMPARE(afterStats.activeConnections(), beforeStats.activeConnections());
    QCOMPARE(afterStats.idleConnections(), beforeStats.idleConnections());
    QCOMPARE(afterConfig.maxPoolSize(), beforeConfig.maxPoolSize());
    QCOMPARE(afterConfig.maxTotalConnections(), beforeConfig.maxTotalConnections());
    QVERIFY(pool->contains(url));

    releaseSocket(socket);
}

void TestQCWebSocketPool::testPublicSignalsAllowSynchronousReentry_data()
{
    QTest::addColumn<QString>("signalName");
    QTest::addColumn<QString>("actionName");
    QTest::addColumn<int>("expectedTotal");
    QTest::addColumn<int>("expectedActive");
    QTest::addColumn<int>("expectedIdle");
    QTest::addColumn<int>("expectedHits");
    QTest::addColumn<int>("expectedMisses");
    QTest::addColumn<int>("expectedOtherIdle");

    QTest::newRow("connectionCreated_acquire")
        << QStringLiteral("connectionCreated") << QStringLiteral("acquire") << 2 << 2 << 0 << 0 << 2
        << 0;
    QTest::newRow("connectionCreated_release")
        << QStringLiteral("connectionCreated") << QStringLiteral("release") << 1 << 1 << 0 << 0 << 1
        << 1;
    QTest::newRow("connectionCreated_clearPool")
        << QStringLiteral("connectionCreated") << QStringLiteral("clearPool") << 0 << 0 << 0 << 0
        << 0 << 0;
    QTest::newRow("connectionReused_acquire")
        << QStringLiteral("connectionReused") << QStringLiteral("acquire") << 2 << 2 << 0 << 1 << 2
        << 0;
    QTest::newRow("connectionReused_release")
        << QStringLiteral("connectionReused") << QStringLiteral("release") << 1 << 1 << 0 << 1 << 1
        << 1;
    QTest::newRow("connectionReused_clearPool")
        << QStringLiteral("connectionReused") << QStringLiteral("clearPool") << 0 << 0 << 0 << 0
        << 0 << 0;
    QTest::newRow("connectionClosed_acquire")
        << QStringLiteral("connectionClosed") << QStringLiteral("acquire") << 1 << 1 << 0 << 0 << 1
        << 0;
    QTest::newRow("connectionClosed_release")
        << QStringLiteral("connectionClosed") << QStringLiteral("release") << 0 << 0 << 0 << 0 << 0
        << 1;
    QTest::newRow("connectionClosed_clearPool")
        << QStringLiteral("connectionClosed") << QStringLiteral("clearPool") << 0 << 0 << 0 << 0
        << 0 << 0;
    QTest::newRow("poolLimitReached_acquire")
        << QStringLiteral("poolLimitReached") << QStringLiteral("acquire") << 1 << 1 << 0 << 0 << 1
        << 0;
    QTest::newRow("poolLimitReached_release")
        << QStringLiteral("poolLimitReached") << QStringLiteral("release") << 1 << 0 << 1 << 0 << 1
        << 0;
    QTest::newRow("poolLimitReached_clearPool")
        << QStringLiteral("poolLimitReached") << QStringLiteral("clearPool") << 0 << 0 << 0 << 0
        << 0 << 0;
}

void TestQCWebSocketPool::testPublicSignalsAllowSynchronousReentry()
{
    QFETCH(QString, signalName);
    QFETCH(QString, actionName);
    QFETCH(int, expectedTotal);
    QFETCH(int, expectedActive);
    QFETCH(int, expectedIdle);
    QFETCH(int, expectedHits);
    QFETCH(int, expectedMisses);
    QFETCH(int, expectedOtherIdle);

    QCWebSocketPoolConfig config;
    if (signalName == QStringLiteral("poolLimitReached")) {
        config.setMaxTotalConnections(1);
    }
    pool = new QCWebSocketPool(config);
    applyLocalWssConfig(pool);
    const QUrl url(m_testServerUrl);

    QFuture<QCWebSocketAcquireResult> triggerFuture;
    QFuture<QCWebSocketAcquireResult> nestedFuture;
    QCWebSocket *knownSocket              = nullptr;
    QCWebSocketPool::LeaseId knownLeaseId = 0;
    if (signalName == QStringLiteral("connectionReused")
        || signalName == QStringLiteral("connectionClosed")) {
        QVERIFY(acquireIdleSocket(url) != nullptr);
    } else if (signalName == QStringLiteral("poolLimitReached")) {
        const auto initialResult = awaitAcquire(url);
        QCOMPARE(initialResult.status(), QCWebSocketAcquireResult::Status::Success);
        knownSocket  = resolveSocket(initialResult);
        knownLeaseId = initialResult.leaseId();
    } else {
        QCOMPARE(signalName, QStringLiteral("connectionCreated"));
    }
    if (actionName == QStringLiteral("release") && knownLeaseId == 0) {
        const auto releaseTarget = awaitAcquire(QUrl(m_testServerUrl2));
        QCOMPARE(releaseTarget.status(), QCWebSocketAcquireResult::Status::Success);
        knownSocket  = resolveSocket(releaseTarget);
        knownLeaseId = releaseTarget.leaseId();
    }
    if (actionName == QStringLiteral("release")) {
        QVERIFY2(knownSocket != nullptr, "release row requires a known pooled socket");
        QVERIFY2(knownLeaseId != 0, "release row requires an active lease");
    }

    int signalInvocationCount = 0;
    int actionInvocationCount = 0;
    bool slotReturned         = false;
    QObject reentryContext;
    const auto onSignal = [&, actionName](const QUrl &) {
        ++signalInvocationCount;
        if (actionInvocationCount != 0) {
            return;
        }
        ++actionInvocationCount;
        if (actionName == QStringLiteral("acquire")) {
            nestedFuture = pool->acquire(url);
        } else if (actionName == QStringLiteral("release")) {
            QCOMPARE(pool->release(knownLeaseId), QCWebSocketPool::LeaseResult::Success);
            m_socketLeases.remove(knownSocket);
        } else {
            QString error;
            QVERIFY2(pool->clearPool(url, &error), qPrintable(error));
        }
        slotReturned = true;
    };

    if (signalName == QStringLiteral("connectionCreated")) {
        connect(pool,
                &QCWebSocketPool::connectionCreated,
                &reentryContext,
                onSignal,
                Qt::DirectConnection);
        triggerFuture = pool->acquire(url);
    } else if (signalName == QStringLiteral("connectionReused")) {
        connect(pool,
                &QCWebSocketPool::connectionReused,
                &reentryContext,
                onSignal,
                Qt::DirectConnection);
        triggerFuture = pool->acquire(url);
    } else if (signalName == QStringLiteral("connectionClosed")) {
        connect(pool,
                &QCWebSocketPool::connectionClosed,
                &reentryContext,
                onSignal,
                Qt::DirectConnection);
        QString error;
        QVERIFY2(pool->clearPool(url, &error), qPrintable(error));
    } else {
        connect(pool,
                &QCWebSocketPool::poolLimitReached,
                &reentryContext,
                onSignal,
                Qt::DirectConnection);
        triggerFuture = pool->acquire(url);
    }

    if (signalName != QStringLiteral("connectionClosed")) {
        QVERIFY2(TestWaitUtils::waitUntil([&triggerFuture]() { return triggerFuture.isFinished(); },
                                          10000),
                 "trigger future watchdog timeout");
        const auto triggerResult  = triggerFuture.result();
        const auto expectedStatus = signalName == QStringLiteral("poolLimitReached")
                                        ? QCWebSocketAcquireResult::Status::PoolLimitReached
                                        : QCWebSocketAcquireResult::Status::Success;
        QCOMPARE(triggerResult.status(), expectedStatus);
    }

    QVERIFY2(signalInvocationCount > 0, "signal did not synchronously invoke its direct slot");
    QCOMPARE(actionInvocationCount, 1);
    QVERIFY(slotReturned);

    if (actionName == QStringLiteral("acquire")) {
        QVERIFY2(TestWaitUtils::waitUntil([&nestedFuture]() { return nestedFuture.isFinished(); },
                                          10000),
                 "nested acquire watchdog timeout");
        const auto nestedResult   = nestedFuture.result();
        const auto expectedStatus = signalName == QStringLiteral("poolLimitReached")
                                        ? QCWebSocketAcquireResult::Status::PoolLimitReached
                                        : QCWebSocketAcquireResult::Status::Success;
        QCOMPARE(nestedResult.status(), expectedStatus);
    }

    const auto stats = pool->statistics(url);
    QCOMPARE(stats.totalConnections(), expectedTotal);
    QCOMPARE(stats.activeConnections(), expectedActive);
    QCOMPARE(stats.idleConnections(), expectedIdle);
    QCOMPARE(stats.hitCount(), expectedHits);
    QCOMPARE(stats.missCount(), expectedMisses);
    if (expectedOtherIdle > 0) {
        QCOMPARE(pool->statistics(QUrl(m_testServerUrl2)).idleConnections(), expectedOtherIdle);
    }
}

void TestQCWebSocketPool::testKeepAlivePongMatrix_data()
{
    QTest::addColumn<QString>("pongMode");
    QTest::addColumn<int>("delayMs");
    QTest::addColumn<bool>("expectPoolRetention");

    QTest::newRow("match") << QStringLiteral("match") << 0 << true;
    QTest::newRow("delay") << QStringLiteral("delay") << 2500 << false;
    QTest::newRow("wrong") << QStringLiteral("wrong") << 0 << false;
    QTest::newRow("none") << QStringLiteral("none") << 0 << false;
    QTest::newRow("disconnect") << QStringLiteral("disconnect") << 0 << false;
}

void TestQCWebSocketPool::testKeepAlivePongMatrix()
{
    QFETCH(QString, pongMode);
    QFETCH(int, delayMs);
    QFETCH(bool, expectPoolRetention);

    QCWebSocketPoolConfig config;
    config.setKeepAliveInterval(1);
    config.setMinIdleConnections(1);
    pool = new QCWebSocketPool(config);
    applyLocalWssConfig(pool);

    QVERIFY2(!m_artifactsPath.isEmpty(),
             "WSS evidence artifactsPath 为空，无法复核 keepalive Ping 证据。");

    const QString caseId = QStringLiteral("testKeepAlivePongMatrix_%1")
                               .arg(QString::fromLatin1(QTest::currentDataTag()));
    QUrl url             = TestWebSocketEvidenceUtils::buildCaseUrl(m_testServerUrl,
                                                                    QStringLiteral("/"),
                                                                    caseId);
    QUrlQuery query(url);
    query.addQueryItem(QStringLiteral("pong"), pongMode);
    if (delayMs > 0) {
        query.addQueryItem(QStringLiteral("delay_ms"), QString::number(delayMs));
    }
    url.setQuery(query);

    QCOMPARE(awaitPreWarm(url, 1).status(), QCWebSocketPreWarmResult::Status::Success);
    const auto acquireResult = awaitAcquire(url);
    QCOMPARE(acquireResult.status(), QCWebSocketAcquireResult::Status::Success);
    auto *socket = resolveSocket(acquireResult);
    QVERIFY2(socket != nullptr, "keepalive 场景无法获取已预热连接");
    releaseSocket(socket);

    QPointer<QCWebSocket> originalSocket(socket);
    QSignalSpy textSpy(socket, &QCWebSocket::textMessageReceived);
    QSignalSpy pongSpy(socket, &QCWebSocket::pongReceived);
    QSignalSpy closedSpy(pool, &QCWebSocketPool::connectionClosed);

    QList<QJsonObject> pingFrames;
    const bool pingObserved = TestWaitUtils::waitUntil(
        [&]() {
            pingFrames.clear();
            const auto frames
                = TestWebSocketEvidenceUtils::readFrameEventsByCaseOnce(m_artifactsPath,
                                                                        caseId,
                                                                        QStringLiteral("recv"));
            for (const auto &frame : frames) {
                if (frame.value(QStringLiteral("opcode")).toInt() == 0x9) {
                    pingFrames.append(frame);
                }
            }
            return !pingFrames.isEmpty();
        },
        5000);
    QVERIFY2(pingObserved, "keepalive fixture 未收到 client Ping");

    if (expectPoolRetention) {
        QVERIFY2(TestWaitUtils::waitForSpyCount(pongSpy, 1, 2000), "match 场景未收到匹配 Pong");
        const QByteArray nonce = pongSpy.first().first().toByteArray();
        QVERIFY(!nonce.isEmpty());
        QVERIFY(nonce.size() <= 16);
        QVERIFY(pool->contains(url));

        const auto reusedResult = awaitAcquire(url);
        QCOMPARE(reusedResult.status(), QCWebSocketAcquireResult::Status::Success);
        auto *reusedSocket = resolveSocket(reusedResult);
        QCOMPARE(reusedSocket, socket);
        releaseSocket(reusedSocket);
        QVERIFY2(TestWaitUtils::waitForNoAdditionalSignal(closedSpy, 1500),
                 "匹配 Pong 后连接不应被 keepalive deadline 摘除");
    } else {
        if (pongMode == QStringLiteral("wrong")) {
            QVERIFY2(TestWaitUtils::waitForSpyCount(pongSpy, 1, 2000), "wrong 场景未收到错误 Pong");
        }

        QVERIFY2(TestWaitUtils::waitForSpyCount(closedSpy, 1, 5000),
                 "非匹配 keepalive 场景未在 deadline 后摘除连接");
        QVERIFY(!pool->contains(url));
        QCOMPARE(pool->statistics(url).totalConnections(), 0);
        QCOMPARE(textSpy.count(), 0);
        QCOMPARE(pongSpy.count(), pongMode == QStringLiteral("wrong") ? 1 : 0);
        QVERIFY2(TestWaitUtils::waitUntil([&originalSocket]() { return originalSocket.isNull(); },
                                          2000),
                 "被摘除的 keepalive 连接未销毁");

        const auto replacementResult = awaitAcquire(url);
        QCOMPARE(replacementResult.status(), QCWebSocketAcquireResult::Status::Success);
        auto *replacementSocket = resolveSocket(replacementResult);
        QVERIFY(replacementSocket != nullptr);
        QVERIFY(replacementSocket != originalSocket.data());
        releaseSocket(replacementSocket);
    }

    QCOMPARE(textSpy.count(), 0);
}

void TestQCWebSocketPool::testPendingAcquireCompletesOnClearAndDestroy()
{
    pool = new QCWebSocketPool();
    applyLocalWssConfig(pool);
    const QUrl url(m_testServerUrl);

    auto cancelledFuture = pool->acquire(url);
    QString clearError;
    QVERIFY2(pool->clearPool(url, &clearError), qPrintable(clearError));
    QVERIFY(cancelledFuture.isFinished());
    QCOMPARE(cancelledFuture.result().status(), QCWebSocketAcquireResult::Status::Cancelled);

    auto destroyedFuture        = pool->acquire(url);
    auto preWarmDestroyedFuture = pool->preWarm(url, 2);
    delete pool;
    pool = nullptr;
    QVERIFY(destroyedFuture.isFinished());
    QCOMPARE(destroyedFuture.result().status(), QCWebSocketAcquireResult::Status::PoolDestroyed);
    QVERIFY(preWarmDestroyedFuture.isFinished());
    QCOMPARE(preWarmDestroyedFuture.result().status(),
             QCWebSocketPreWarmResult::Status::PoolDestroyed);
}

void TestQCWebSocketPool::testPreWarmReleaseFailureDoesNotWarm()
{
    pool = new QCWebSocketPool();
    applyLocalWssConfig(pool);
    const QUrl url(m_testServerUrl);

    QFuture<QCWebSocketPreWarmResult> future;
    QObject signalContext;
    bool clearResult = false;
    QString clearError;
    int createdCount = 0;
    connect(
        pool,
        &QCWebSocketPool::connectionCreated,
        &signalContext,
        [&](const QUrl &createdUrl) {
            if (createdUrl != url || createdCount != 0) {
                return;
            }
            ++createdCount;
            clearResult = pool->clearPool(createdUrl, &clearError);
        },
        Qt::DirectConnection);

    future = pool->preWarm(url, 1);
    QVERIFY2(TestWaitUtils::waitUntil([&future]() { return future.isFinished(); }, 10000),
             "preWarm release failure watchdog timeout");

    QVERIFY(clearResult);
    QVERIFY(clearError.isEmpty());
    QCOMPARE(createdCount, 1);
    QCOMPARE(future.resultCount(), 1);
    const auto result = future.result();
    QCOMPARE(result.status(), QCWebSocketPreWarmResult::Status::ConnectionFailed);
    QCOMPARE(result.requestedCount(), 1);
    QCOMPARE(result.warmedCount(), 0);
    QVERIFY(!result.error().isEmpty());
    QCOMPARE(pool->statistics(url).totalConnections(), 0);
    QVERIFY(!pool->contains(url));
}

void TestQCWebSocketPool::testBorrowedHandleInvalidatesOnExternalDelete_data()
{
    QTest::addColumn<QString>("deletionMode");
    QTest::newRow("direct-delete") << QStringLiteral("direct-delete");
    QTest::newRow("parent-delete") << QStringLiteral("parent-delete");
    QTest::newRow("delete-later") << QStringLiteral("delete-later");
}

void TestQCWebSocketPool::testBorrowedHandleInvalidatesOnExternalDelete()
{
    QFETCH(QString, deletionMode);

    pool = new QCWebSocketPool();
    applyLocalWssConfig(pool);
    const QUrl url(m_testServerUrl);
    const auto acquireResult = awaitAcquire(url);
    QCOMPARE(acquireResult.status(), QCWebSocketAcquireResult::Status::Success);
    auto *socket = resolveSocket(acquireResult);
    QVERIFY(socket != nullptr);
    releaseSocket(socket);

    QPointer<QCWebSocket> borrowedSocket(socket);
    QSignalSpy closedSpy(pool, &QCWebSocketPool::connectionClosed);
    if (deletionMode == QStringLiteral("parent-delete")) {
        QObject externalParent;
        socket->setParent(&externalParent);
    } else if (deletionMode == QStringLiteral("delete-later")) {
        socket->deleteLater();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    } else {
        QObjectCleanupHandler cleanupHandler;
        cleanupHandler.add(socket);
    }

    QVERIFY(borrowedSocket.isNull());
    QVERIFY2(TestWaitUtils::waitUntil(
                 [&]() {
                     return !pool->contains(url) && pool->statistics(url).totalConnections() == 0;
                 },
                 2000),
             "外部销毁后连接池索引未清理");
    QVERIFY(closedSpy.count() <= 1);
    QString clearError;
    QCOMPARE(pool->release(acquireResult.leaseId()), QCWebSocketPool::LeaseResult::InactiveLease);
    clearError = QStringLiteral("stale error");
    QVERIFY2(pool->clearPool(url, &clearError), qPrintable(clearError));
}

void TestQCWebSocketPool::testDestroyedSignalCleansIndexesAndPending()
{
    pool = new QCWebSocketPool();
    applyLocalWssConfig(pool);
    const QUrl url(m_testServerUrl);

    const auto future  = pool->acquire(url);
    const auto sockets = pool->findChildren<QCWebSocket *>();
    QCOMPARE(sockets.size(), 1);
    QPointer<QCWebSocket> pendingSocket(sockets.constFirst());
    {
        QObjectCleanupHandler cleanupHandler;
        cleanupHandler.add(sockets.constFirst());
    }

    QVERIFY(pendingSocket.isNull());
    QVERIFY2(TestWaitUtils::waitUntil([&future]() { return future.isFinished(); }, 2000),
             "socket 销毁后 pending acquire 未完成");
    QCOMPARE(future.result().status(), QCWebSocketAcquireResult::Status::ConnectionFailed);
    QCOMPARE(future.result().leaseId(), QCWebSocketPool::LeaseId{0});
    QCOMPARE(pool->statistics(url).totalConnections(), 0);
    QVERIFY(!pool->contains(url));
    QString clearError;
    QVERIFY2(pool->clearPool(url, &clearError), qPrintable(clearError));
    QVERIFY(future.isFinished());
}

void TestQCWebSocketPool::testDestroyingRejectsReentry_data()
{
    QTest::addColumn<int>("preWarmCount");
    QTest::newRow("all-apis") << 1;
}

void TestQCWebSocketPool::testDestroyingRejectsReentry()
{
    QFETCH(int, preWarmCount);

    pool = new QCWebSocketPool();
    applyLocalWssConfig(pool);
    const QUrl url(m_testServerUrl);
    QVERIFY(acquireIdleSocket(url) != nullptr);

    QFuture<QCWebSocketAcquireResult> acquireFuture;
    QFuture<QCWebSocketPreWarmResult> preWarmFuture;
    int socketCountBefore = 0;
    int socketCountAfter  = 0;
    bool reentered        = false;
    bool setConfigResult  = true;
    QString setConfigError;
    const QCWebSocketPoolConfig defaultConfig;
    QCWebSocketPoolConfig observedConfig;
    QObject reentryContext;
    connect(
        pool,
        &QCWebSocketPool::connectionClosed,
        &reentryContext,
        [&](const QUrl &) {
            if (reentered) {
                return;
            }
            reentered         = true;
            socketCountBefore = pool->findChildren<QCWebSocket *>().size();
            acquireFuture     = pool->acquire(url);
            preWarmFuture     = pool->preWarm(url, preWarmCount);
            auto replacement  = defaultConfig;
            replacement.setMaxPoolSize(defaultConfig.maxPoolSize() + 1);
            setConfigResult  = pool->setConfig(replacement, &setConfigError);
            observedConfig   = pool->config();
            socketCountAfter = pool->findChildren<QCWebSocket *>().size();
        },
        Qt::DirectConnection);

    {
        QObjectCleanupHandler cleanupHandler;
        cleanupHandler.add(pool);
    }
    pool = nullptr;

    QVERIFY(reentered);
    QVERIFY(!setConfigResult);
    QVERIFY(!setConfigError.isEmpty());
    QCOMPARE(socketCountAfter, socketCountBefore);
    QVERIFY(acquireFuture.isFinished());
    QCOMPARE(acquireFuture.resultCount(), 1);
    QCOMPARE(acquireFuture.result().status(), QCWebSocketAcquireResult::Status::PoolDestroyed);
    QCOMPARE(acquireFuture.result().leaseId(), QCWebSocketPool::LeaseId{0});
    QVERIFY(preWarmFuture.isFinished());
    QCOMPARE(preWarmFuture.resultCount(), 1);
    QCOMPARE(preWarmFuture.result().status(), QCWebSocketPreWarmResult::Status::PoolDestroyed);
    QCOMPARE(preWarmFuture.result().warmedCount(), 0);
    QCOMPARE(observedConfig.maxPoolSize(), defaultConfig.maxPoolSize());
}

void TestQCWebSocketPool::testDestroyingMutatorsAndQueries()
{
    pool = new QCWebSocketPool();
    applyLocalWssConfig(pool);
    const QUrl url(m_testServerUrl);
    const auto leaseResult = awaitAcquire(url);
    QCOMPARE(leaseResult.status(), QCWebSocketAcquireResult::Status::Success);
    auto *socket = resolveSocket(leaseResult);
    QVERIFY(socket != nullptr);

    const QCWebSocketPoolConfig defaultConfig;
    QCWebSocketPoolStats observedStats;
    bool observedContains = true;
    int childCountBefore  = 0;
    int childCountAfter   = 0;
    int signalCount       = 0;
    bool setConfigResult  = true;
    auto releaseResult    = QCWebSocketPool::LeaseResult::Success;
    bool clearPoolResult  = true;
    QString setConfigError;
    QString clearPoolError;
    QObject reentryContext;
    connect(
        pool,
        &QCWebSocketPool::connectionClosed,
        &reentryContext,
        [&](const QUrl &) {
            ++signalCount;
            childCountBefore = pool->children().size();
            auto replacement = defaultConfig;
            replacement.setMaxPoolSize(defaultConfig.maxPoolSize() + 1);
            setConfigResult  = pool->setConfig(replacement, &setConfigError);
            releaseResult    = pool->release(leaseResult.leaseId());
            clearPoolResult  = pool->clearPool(url, &clearPoolError);
            observedContains = pool->contains(url);
            observedStats    = pool->statistics(url);
            childCountAfter  = pool->children().size();
        },
        Qt::DirectConnection);

    {
        QObjectCleanupHandler cleanupHandler;
        cleanupHandler.add(pool);
    }
    pool = nullptr;

    QCOMPARE(signalCount, 1);
    QVERIFY(!setConfigResult);
    QCOMPARE(releaseResult, QCWebSocketPool::LeaseResult::PoolDestroyed);
    QVERIFY(!clearPoolResult);
    QVERIFY(!setConfigError.isEmpty());
    QVERIFY(!clearPoolError.isEmpty());
    QCOMPARE(childCountAfter, childCountBefore);
    QVERIFY(!observedContains);
    QCOMPARE(observedStats.totalConnections(), 0);
    QCOMPARE(observedStats.hitCount(), 0);
    QCOMPARE(observedStats.missCount(), 0);
}

void TestQCWebSocketPool::testPreWarmCountValidation_data()
{
    QTest::addColumn<int>("invalidCount");
    QTest::newRow("negative") << -1;
}

void TestQCWebSocketPool::testPreWarmCountValidation()
{
    QFETCH(int, invalidCount);

    pool = new QCWebSocketPool();
    applyLocalWssConfig(pool);
    const QUrl url(m_testServerUrl);

    auto config = pool->config();
    config.setEnableKeepAlive(false);
    config.setMaxPoolSize(2);
    config.setMaxTotalConnections(2);
    QString configError = QStringLiteral("stale error");
    QVERIFY2(pool->setConfig(config, &configError), qPrintable(configError));
    QVERIFY(configError.isEmpty());

    const auto invalidResult  = awaitPreWarm(url, invalidCount);
    const auto zeroResult     = awaitPreWarm(url, 0);
    const auto tooLargeResult = awaitPreWarm(url, 1025);

    auto independentConfig = config;
    independentConfig.setMaxPoolSize(4);
    independentConfig.setMaxTotalConnections(1);
    QVERIFY(pool->setConfig(independentConfig));

    auto invalidIdleConfig = independentConfig;
    invalidIdleConfig.setMaxPoolSize(1);
    QVERIFY(!pool->setConfig(invalidIdleConfig, &configError));
    QCOMPARE(configError, QStringLiteral("minIdleConnections 超过每个 URL 的连接上限"));

    const auto observedConfig = pool->config();
    const auto observedStats  = pool->statistics(url);
    const bool valid = invalidResult.status() == QCWebSocketPreWarmResult::Status::InvalidArgument
                       && invalidResult.requestedCount() == invalidCount
                       && invalidResult.warmedCount() == 0
                       && zeroResult.status() == QCWebSocketPreWarmResult::Status::Success
                       && zeroResult.requestedCount() == 0 && zeroResult.warmedCount() == 0
                       && tooLargeResult.status()
                              == QCWebSocketPreWarmResult::Status::InvalidArgument
                       && tooLargeResult.requestedCount() == 1025
                       && tooLargeResult.warmedCount() == 0 && observedConfig.maxPoolSize() == 4
                       && observedConfig.maxTotalConnections() == 1
                       && observedStats.totalConnections() == 0;
    QVERIFY2(valid, "Pool 配置或 preWarm 参数边界未按合同处理");
}

void TestQCWebSocketPool::testInvalidConfigLeavesStateUnchanged()
{
    pool                    = new QCWebSocketPool();
    const auto beforeConfig = pool->config();
    QTimer *keepAliveTimer  = nullptr;
    const auto timersBefore = pool->findChildren<QTimer *>();
    for (auto *timer : timersBefore) {
        if (timer->interval() == beforeConfig.keepAliveInterval() * 1000) {
            keepAliveTimer = timer;
            break;
        }
    }
    QVERIFY(keepAliveTimer != nullptr);
    QVERIFY(keepAliveTimer->isActive());

    auto invalidConfig = beforeConfig;
    invalidConfig.setMaxPoolSize(0);
    invalidConfig.setEnableKeepAlive(false);
    QString error = QStringLiteral("stale error");
    QVERIFY(!pool->setConfig(invalidConfig, &error));
    QCOMPARE(error, QStringLiteral("maxPoolSize 超出允许范围"));

    const auto afterConfig = pool->config();
    QCOMPARE(afterConfig.maxPoolSize(), beforeConfig.maxPoolSize());
    QCOMPARE(afterConfig.maxTotalConnections(), beforeConfig.maxTotalConnections());
    QCOMPARE(afterConfig.keepAliveInterval(), beforeConfig.keepAliveInterval());
    QCOMPARE(afterConfig.enableKeepAlive(), beforeConfig.enableKeepAlive());
    const auto timersAfter = pool->findChildren<QTimer *>();
    QCOMPARE(timersAfter.size(), timersBefore.size());
    QVERIFY(timersAfter.contains(keepAliveTimer));
    QVERIFY(keepAliveTimer->isActive());
    QCOMPARE(keepAliveTimer->interval(), beforeConfig.keepAliveInterval() * 1000);
}

void TestQCWebSocketPool::testKeepAliveIntervalOverflowRejected()
{
    QCWebSocketPoolConfig config;
    config.setKeepAliveInterval(std::numeric_limits<int>::max());
    config.setEnableKeepAlive(true);

    QCWebSocketPool observedPool(config);
    QVERIFY(observedPool.config().keepAliveInterval() > 0);
}

QTEST_MAIN(TestQCWebSocketPool)
#include "tst_QCWebSocketPool.moc"
