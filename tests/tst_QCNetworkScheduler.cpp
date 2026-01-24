// SPDX-License-Identifier: MIT
// Copyright (c) 2025 QCurl Project

#include "../src/QCNetworkAccessManager.h"
#include "../src/QCNetworkMockHandler.h"
#include "../src/QCNetworkReply.h"
#include "../src/QCNetworkRequest.h"
#include "../src/QCNetworkRequestPriority.h"
#include "../src/QCNetworkRequestScheduler.h"

#include <QCoreApplication>
#include <QSignalSpy>
#include <QTimer>
#include <QtTest>

using namespace QCurl;

/**
 * @brief 测试 QCNetworkRequestScheduler（请求优先级调度器）
 * 
 * 测试覆盖：
 * 1. 优先级排序
 * 2. 并发限制（全局 + 每主机）
 * 3. 请求管理（延后/恢复调度/取消）
 * 4. 动态优先级调整
 * 5. 带宽限制
 * 6. 统计信息
 * 7. 信号通知
 */
class tst_QCNetworkScheduler : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();

    // 测试用例
    void testSchedulerEnabled();
    void testPriorityQueueOrdering();
    void testConcurrentRequestLimit();
    void testPerHostLimit();
    void testDeferUndefer();
    void testCancelRequest();
    void testCancelAllRequests();
    void testChangePriority();
    void testStatistics();
    void testCriticalPriority();
    void testQueueEmptySignal();
    void testSchedulerIntegration();

private:
    QCNetworkAccessManager *m_manager      = nullptr;
    QCNetworkRequestScheduler *m_scheduler = nullptr;
    QCNetworkMockHandler m_mock;
};

void tst_QCNetworkScheduler::initTestCase()
{
    m_manager   = new QCNetworkAccessManager(this);
    m_scheduler = QCNetworkRequestScheduler::instance();

    m_mock.clear();
    m_mock.setCaptureEnabled(false);
    m_mock.setGlobalDelay(0);
    m_manager->setMockHandler(&m_mock);

    qRegisterMetaType<QCNetworkReply *>("QCNetworkReply*");

    // 重置配置为测试默认值
    QCNetworkRequestScheduler::Config config;
    config.maxConcurrentRequests   = 2; // 限制为 2 以便测试
    config.maxRequestsPerHost      = 1; // 限制为 1 以便测试
    config.maxBandwidthBytesPerSec = 0; // 默认无限制
    config.enableThrottling        = true;
    m_scheduler->setConfig(config);

    qDebug() << "Test initialized with config:"
             << "maxConcurrent=" << config.maxConcurrentRequests
             << "maxPerHost=" << config.maxRequestsPerHost;
}

void tst_QCNetworkScheduler::cleanupTestCase()
{
    // 清理所有请求
    m_scheduler->cancelAllRequests();
    m_manager = nullptr;
}

/**
 * @brief 测试 1：验证启用/禁用调度器
 */
void tst_QCNetworkScheduler::testSchedulerEnabled()
{
    // 默认应该是禁用的
    QVERIFY(!m_manager->isSchedulerEnabled());

    // 启用调度器
    m_manager->enableRequestScheduler(true);
    QVERIFY(m_manager->isSchedulerEnabled());

    // 禁用调度器
    m_manager->enableRequestScheduler(false);
    QVERIFY(!m_manager->isSchedulerEnabled());

    qDebug() << "✅ Test 1 passed: Scheduler enable/disable";
}

/**
 * @brief 测试 2：验证优先级排序
 * 
 * 创建不同优先级的请求，验证高优先级先执行。
 */
void tst_QCNetworkScheduler::testPriorityQueueOrdering()
{
    // 清理之前的请求
    m_scheduler->cancelAllRequests();

    // 固化测试契约：优先级为“非抢占”
    // - 已 Running 的请求不会被更高优先级请求中断
    // - 优先级只影响 pending 出队顺序
    QCNetworkRequestScheduler::Config config = m_scheduler->config();
    config.maxConcurrentRequests             = 1;
    config.maxRequestsPerHost                = 10;
    m_scheduler->setConfig(config);

    m_mock.clear();

    QCNetworkRequest lowReq(QUrl("http://prio-low.test/test"));
    QCNetworkRequest normalReq(QUrl("http://prio-normal.test/test"));
    QCNetworkRequest highReq(QUrl("http://prio-high.test/test"));

    lowReq.setPriority(QCNetworkRequestPriority::Low);
    normalReq.setPriority(QCNetworkRequestPriority::Normal);
    highReq.setPriority(QCNetworkRequestPriority::High);

    m_mock.mockResponse(HttpMethod::Get, lowReq.url(), QByteArray("OK"));
    m_mock.mockResponse(HttpMethod::Get, normalReq.url(), QByteArray("OK"));
    m_mock.mockResponse(HttpMethod::Get, highReq.url(), QByteArray("OK"));

    QSignalSpy startedSpy(m_scheduler, &QCNetworkRequestScheduler::requestStarted);

    // 按相反顺序入队：先 Low，再 Normal，再 High
    auto *lowReply    = m_scheduler->scheduleRequest(lowReq,
                                                  HttpMethod::Get,
                                                  QCNetworkRequestPriority::Low,
                                                  QByteArray(),
                                                  m_manager);
    auto *normalReply = m_scheduler->scheduleRequest(normalReq,
                                                     HttpMethod::Get,
                                                     QCNetworkRequestPriority::Normal,
                                                     QByteArray(),
                                                     m_manager);
    auto *highReply   = m_scheduler->scheduleRequest(highReq,
                                                   HttpMethod::Get,
                                                   QCNetworkRequestPriority::High,
                                                   QByteArray(),
                                                   m_manager);

    // 由于并发限制为 1，只有第一个请求会进入 Running；后续不会“抢占”。
    QCOMPARE(startedSpy.count(), 1);

    // 推进事件循环直至三个请求都依次开始（Low -> High -> Normal）
    QTRY_COMPARE_WITH_TIMEOUT(startedSpy.count(), 3, 2000);

    const auto started0 = qvariant_cast<QCNetworkReply *>(startedSpy.at(0).at(0));
    const auto started1 = qvariant_cast<QCNetworkReply *>(startedSpy.at(1).at(0));
    const auto started2 = qvariant_cast<QCNetworkReply *>(startedSpy.at(2).at(0));

    QCOMPARE(started0, lowReply);
    QCOMPARE(started1, highReply);
    QCOMPARE(started2, normalReply);

    QTRY_VERIFY_WITH_TIMEOUT(lowReply->isFinished(), 2000);
    QTRY_VERIFY_WITH_TIMEOUT(highReply->isFinished(), 2000);
    QTRY_VERIFY_WITH_TIMEOUT(normalReply->isFinished(), 2000);

    // 清理
    m_scheduler->cancelAllRequests();
    lowReply->deleteLater();
    normalReply->deleteLater();
    highReply->deleteLater();

    qDebug() << "✅ Test 2 passed: Priority queue ordering (non-preemptive contract)";
}

/**
 * @brief 测试 3：验证全局并发限制
 */
void tst_QCNetworkScheduler::testConcurrentRequestLimit()
{
    m_scheduler->cancelAllRequests();
    m_mock.clear();

    // 配置为最多 2 个并发
    QCNetworkRequestScheduler::Config config = m_scheduler->config();
    config.maxConcurrentRequests             = 2;
    config.maxRequestsPerHost                = 10;
    m_scheduler->setConfig(config);

    // 创建 5 个请求
    QList<QCNetworkReply *> replies;
    for (int i = 0; i < 5; ++i) {
        QCNetworkRequest req(QUrl(QString("http://concurrent-%1.test/test").arg(i)));
        req.setPriority(QCNetworkRequestPriority::Normal);
        m_mock.mockResponse(HttpMethod::Get, req.url(), QByteArray("OK"));
        auto *reply = m_scheduler->scheduleRequest(req,
                                                   HttpMethod::Get,
                                                   QCNetworkRequestPriority::Normal,
                                                   QByteArray(),
                                                   m_manager);
        replies.append(reply);
    }

    // 获取统计信息
    auto stats = m_scheduler->statistics();

    // 离线门禁：调度层统计应稳定可重复（不依赖真实网络时序）
    QCOMPARE(stats.runningRequests, 2);
    QCOMPARE(stats.pendingRequests, 3);

    qDebug() << "Running:" << stats.runningRequests << "Pending:" << stats.pendingRequests;

    // 清理
    m_scheduler->cancelAllRequests();
    for (auto *reply : replies) {
        reply->deleteLater();
    }

    qDebug() << "✅ Test 3 passed: Concurrent request limit";
}

/**
 * @brief 测试 4：验证每主机并发限制
 */
void tst_QCNetworkScheduler::testPerHostLimit()
{
    m_scheduler->cancelAllRequests();
    m_mock.clear();

    // 配置为每主机最多 1 个并发
    QCNetworkRequestScheduler::Config config = m_scheduler->config();
    config.maxConcurrentRequests             = 10; // 全局足够大
    config.maxRequestsPerHost                = 1;  // 每主机限制为 1
    m_scheduler->setConfig(config);

    // 向同一主机发送 3 个请求
    QList<QCNetworkReply *> replies;
    for (int i = 0; i < 3; ++i) {
        QCNetworkRequest req(QUrl(QString("http://same-host.test/test?i=%1").arg(i)));
        req.setPriority(QCNetworkRequestPriority::Normal);
        m_mock.mockResponse(HttpMethod::Get, req.url(), QByteArray("OK"));
        auto *reply = m_scheduler->scheduleRequest(req,
                                                   HttpMethod::Get,
                                                   QCNetworkRequestPriority::Normal,
                                                   QByteArray(),
                                                   m_manager);
        replies.append(reply);
    }

    // 获取运行中的请求
    auto running = m_scheduler->runningRequests();

    // 运行中的请求应该只有 1 个（因为是同一主机）
    QVERIFY(running.size() <= 1);

    qDebug() << "Running requests to same host:" << running.size();

    // 清理
    m_scheduler->cancelAllRequests();
    for (auto *reply : replies) {
        reply->deleteLater();
    }

    qDebug() << "✅ Test 4 passed: Per-host limit";
}

/**
 * @brief 测试 5：验证延后/恢复调度功能
 */
void tst_QCNetworkScheduler::testDeferUndefer()
{
    m_scheduler->cancelAllRequests();
    m_mock.clear();

    // 创建一个请求
    QCNetworkRequest req(QUrl("http://defer.test/test"));
    req.setPriority(QCNetworkRequestPriority::Normal);
    m_mock.mockResponse(HttpMethod::Get, req.url(), QByteArray("OK"));
    auto *reply = m_scheduler->scheduleRequest(req,
                                               HttpMethod::Get,
                                               QCNetworkRequestPriority::Normal,
                                               QByteArray(),
                                               m_manager);

    auto statsBefore  = m_scheduler->statistics();
    int pendingBefore = statsBefore.pendingRequests + statsBefore.runningRequests;

    // 延后请求（调度层语义，非传输级 pause）
    m_scheduler->deferRequest(reply);

    // 恢复调度
    m_scheduler->undeferRequest(reply);

    auto statsAfter  = m_scheduler->statistics();
    int pendingAfter = statsAfter.pendingRequests + statsAfter.runningRequests;

    // 恢复后请求应该重新加入队列
    QVERIFY(pendingAfter >= 1);

    qDebug() << "Pending before:" << pendingBefore << "after:" << pendingAfter;

    // 清理
    m_scheduler->cancelRequest(reply);
    reply->deleteLater();

    qDebug() << "✅ Test 5 passed: Defer/Undefer";
}

/**
 * @brief 测试 6：验证取消单个请求
 */
void tst_QCNetworkScheduler::testCancelRequest()
{
    m_scheduler->cancelAllRequests();
    m_mock.clear();

    // 创建请求
    QCNetworkRequest req(QUrl("http://cancel-one.test/test"));
    req.setPriority(QCNetworkRequestPriority::Normal);
    m_mock.mockResponse(HttpMethod::Get, req.url(), QByteArray("OK"));
    auto *reply = m_scheduler->scheduleRequest(req,
                                               HttpMethod::Get,
                                               QCNetworkRequestPriority::Normal,
                                               QByteArray(),
                                               m_manager);

    // 监听取消信号
    QSignalSpy cancelSpy(m_scheduler, &QCNetworkRequestScheduler::requestCancelled);

    // 取消请求
    m_scheduler->cancelRequest(reply);

    // 应该收到取消信号
    QVERIFY(cancelSpy.count() >= 1);
    QTRY_VERIFY_WITH_TIMEOUT(reply->isFinished(), 2000);

    reply->deleteLater();

    qDebug() << "✅ Test 6 passed: Cancel request";
}

/**
 * @brief 测试 7：验证取消所有请求
 */
void tst_QCNetworkScheduler::testCancelAllRequests()
{
    m_scheduler->cancelAllRequests();
    m_mock.clear();

    QCNetworkRequestScheduler::Config config = m_scheduler->config();
    config.maxConcurrentRequests             = 2;
    config.maxRequestsPerHost                = 10;
    m_scheduler->setConfig(config);

    // 创建多个请求
    QList<QCNetworkReply *> replies;
    for (int i = 0; i < 5; ++i) {
        QCNetworkRequest req(QUrl(QString("http://cancel-all-%1.test/test").arg(i)));
        req.setPriority(QCNetworkRequestPriority::Normal);
        m_mock.mockResponse(HttpMethod::Get, req.url(), QByteArray("OK"));
        auto *reply = m_scheduler->scheduleRequest(req,
                                                   HttpMethod::Get,
                                                   QCNetworkRequestPriority::Normal,
                                                   QByteArray(),
                                                   m_manager);
        replies.append(reply);
    }

    auto statsBefore = m_scheduler->statistics();
    int totalBefore  = statsBefore.pendingRequests + statsBefore.runningRequests;

    QVERIFY(totalBefore >= 5);

    // 取消所有请求
    m_scheduler->cancelAllRequests();

    auto statsAfter = m_scheduler->statistics();
    int totalAfter  = statsAfter.pendingRequests + statsAfter.runningRequests;

    // 所有请求应该被取消
    QCOMPARE(totalAfter, 0);

    qDebug() << "Total before:" << totalBefore << "after:" << totalAfter;

    for (auto *reply : replies) {
        reply->deleteLater();
    }

    qDebug() << "✅ Test 7 passed: Cancel all requests";
}

/**
 * @brief 测试 8：验证动态优先级调整
 */
void tst_QCNetworkScheduler::testChangePriority()
{
    m_scheduler->cancelAllRequests();
    m_mock.clear();

    // 使请求保持在 pending，避免“已 Running 无法调整优先级”的不确定性
    QCNetworkRequestScheduler::Config config = m_scheduler->config();
    config.maxConcurrentRequests             = 0;
    config.maxRequestsPerHost                = 10;
    m_scheduler->setConfig(config);

    // 创建低优先级请求
    QCNetworkRequest req(QUrl("http://priority-change.test/test"));
    req.setPriority(QCNetworkRequestPriority::Low);
    m_mock.mockResponse(HttpMethod::Get, req.url(), QByteArray("OK"));
    auto *reply = m_scheduler->scheduleRequest(req,
                                               HttpMethod::Get,
                                               QCNetworkRequestPriority::Low,
                                               QByteArray(),
                                               m_manager);

    // 监听队列信号
    QSignalSpy queueSpy(m_scheduler, &QCNetworkRequestScheduler::requestQueued);

    // 调整优先级为 High
    m_scheduler->changePriority(reply, QCNetworkRequestPriority::High);

    // 应该收到重新加入队列的信号
    // 注意：可能因为请求已经开始执行而无法调整
    // 这个测试主要验证 API 不会崩溃

    qDebug() << "Priority change signal count:" << queueSpy.count();
    QVERIFY(queueSpy.count() >= 1);

    // 清理
    m_scheduler->cancelRequest(reply);
    QTRY_VERIFY_WITH_TIMEOUT(reply->isFinished(), 2000);
    reply->deleteLater();

    qDebug() << "✅ Test 8 passed: Change priority";
}

/**
 * @brief 测试 9：验证统计信息
 */
void tst_QCNetworkScheduler::testStatistics()
{
    m_scheduler->cancelAllRequests();
    m_mock.clear();

    QCNetworkRequestScheduler::Config config = m_scheduler->config();
    config.maxConcurrentRequests             = 1;
    config.maxRequestsPerHost                = 10;
    m_scheduler->setConfig(config);

    // 创建几个请求
    QList<QCNetworkReply *> replies;
    for (int i = 0; i < 3; ++i) {
        QCNetworkRequest req(QUrl(QString("http://stats-%1.test/test").arg(i)));
        req.setPriority(QCNetworkRequestPriority::Normal);
        m_mock.mockResponse(HttpMethod::Get, req.url(), QByteArray("OK"));
        auto *reply = m_scheduler->scheduleRequest(req,
                                                   HttpMethod::Get,
                                                   QCNetworkRequestPriority::Normal,
                                                   QByteArray(),
                                                   m_manager);
        replies.append(reply);
    }

    // 获取统计信息
    auto stats = m_scheduler->statistics();

    // 验证统计信息
    QVERIFY(stats.pendingRequests + stats.runningRequests >= 3);
    QVERIFY(stats.completedRequests >= 0);
    QVERIFY(stats.cancelledRequests >= 0);
    QVERIFY(stats.totalBytesReceived >= 0);
    QVERIFY(stats.totalBytesSent >= 0);
    QVERIFY(stats.avgResponseTime >= 0);

    qDebug() << "Stats: pending=" << stats.pendingRequests << "running=" << stats.runningRequests
             << "completed=" << stats.completedRequests << "cancelled=" << stats.cancelledRequests;

    // 清理
    m_scheduler->cancelAllRequests();
    for (auto *reply : replies) {
        QTRY_VERIFY_WITH_TIMEOUT(reply->isFinished(), 2000);
        reply->deleteLater();
    }

    qDebug() << "✅ Test 9 passed: Statistics";
}

/**
 * @brief 测试 10：验证 Critical 优先级立即执行
 */
void tst_QCNetworkScheduler::testCriticalPriority()
{
    m_scheduler->cancelAllRequests();
    m_mock.clear();

    QCNetworkRequestScheduler::Config config = m_scheduler->config();
    config.maxConcurrentRequests             = 1;
    config.maxRequestsPerHost                = 10;
    m_scheduler->setConfig(config);

    // 先创建一些 Normal 请求填满队列
    QList<QCNetworkReply *> normalReplies;
    for (int i = 0; i < 5; ++i) {
        QCNetworkRequest req(QUrl(QString("http://normal-%1.test/test").arg(i)));
        req.setPriority(QCNetworkRequestPriority::Normal);
        m_mock.mockResponse(HttpMethod::Get, req.url(), QByteArray("OK"));
        auto *reply = m_scheduler->scheduleRequest(req,
                                                   HttpMethod::Get,
                                                   QCNetworkRequestPriority::Normal,
                                                   QByteArray(),
                                                   m_manager);
        normalReplies.append(reply);
    }

    auto statsBefore  = m_scheduler->statistics();
    int runningBefore = statsBefore.runningRequests;

    // 创建 Critical 请求
    QCNetworkRequest criticalReq(QUrl("http://critical.test/test"));
    criticalReq.setPriority(QCNetworkRequestPriority::Critical);
    m_mock.mockResponse(HttpMethod::Get, criticalReq.url(), QByteArray("OK"));
    auto *criticalReply = m_scheduler->scheduleRequest(criticalReq,
                                                       HttpMethod::Get,
                                                       QCNetworkRequestPriority::Critical,
                                                       QByteArray(),
                                                       m_manager);

    auto statsAfter  = m_scheduler->statistics();
    int runningAfter = statsAfter.runningRequests;

    // Critical 请求应该立即执行（不受并发限制）
    QCOMPARE(runningAfter, runningBefore + 1);

    qDebug() << "Running before:" << runningBefore << "after:" << runningAfter;

    // 清理
    m_scheduler->cancelAllRequests();
    for (auto *reply : normalReplies) {
        QTRY_VERIFY_WITH_TIMEOUT(reply->isFinished(), 2000);
        reply->deleteLater();
    }
    QTRY_VERIFY_WITH_TIMEOUT(criticalReply->isFinished(), 2000);
    criticalReply->deleteLater();

    qDebug() << "✅ Test 10 passed: Critical priority";
}

/**
 * @brief 测试 11：验证队列空信号
 */
void tst_QCNetworkScheduler::testQueueEmptySignal()
{
    m_scheduler->cancelAllRequests();
    m_mock.clear();

    // 监听队列空信号
    QSignalSpy emptySpy(m_scheduler, &QCNetworkRequestScheduler::queueEmpty);

    // 创建一个请求
    QCNetworkRequest req(QUrl("http://queue-empty.test/test"));
    req.setPriority(QCNetworkRequestPriority::Normal);
    m_mock.mockResponse(HttpMethod::Get, req.url(), QByteArray("OK"));
    auto *reply = m_scheduler->scheduleRequest(req,
                                               HttpMethod::Get,
                                               QCNetworkRequestPriority::Normal,
                                               QByteArray(),
                                               m_manager);

    // 立即取消
    m_scheduler->cancelRequest(reply);

    // 等待信号（避免固定 sleep 引入 flaky）
    if (emptySpy.count() == 0) {
        QVERIFY(emptySpy.wait(2000));
    }

    // 应该收到队列空信号
    QVERIFY(emptySpy.count() >= 1);

    QTRY_VERIFY_WITH_TIMEOUT(reply->isFinished(), 2000);
    reply->deleteLater();

    qDebug() << "✅ Test 11 passed: Queue empty signal";
}

/**
 * @brief 测试 12：验证 QCNetworkAccessManager 集成
 */
void tst_QCNetworkScheduler::testSchedulerIntegration()
{
    m_mock.clear();

    // 启用调度器
    m_manager->enableRequestScheduler(true);
    QVERIFY(m_manager->isSchedulerEnabled());

    // 通过 manager 创建请求
    QCNetworkRequest req(QUrl("http://integration.test/test"));
    req.setPriority(QCNetworkRequestPriority::High);
    m_mock.mockResponse(HttpMethod::Get, req.url(), QByteArray("OK"));

    auto *reply = m_manager->scheduleGet(req);
    QVERIFY(reply != nullptr);

    // 验证请求已加入调度器
    auto stats = m_scheduler->statistics();
    QVERIFY(stats.pendingRequests + stats.runningRequests >= 1);

    // 清理
    m_scheduler->cancelRequest(reply);
    QTRY_VERIFY_WITH_TIMEOUT(reply->isFinished(), 2000);
    reply->deleteLater();

    // 禁用调度器
    m_manager->enableRequestScheduler(false);
    QVERIFY(!m_manager->isSchedulerEnabled());

    qDebug() << "✅ Test 12 passed: Scheduler integration";
}

QTEST_MAIN(tst_QCNetworkScheduler)
#include "tst_QCNetworkScheduler.moc"
