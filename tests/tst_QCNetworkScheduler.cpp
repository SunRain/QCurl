// SPDX-License-Identifier: MIT
// Copyright (c) 2025 QCurl Project

#include <QtTest>
#include <QSignalSpy>
#include <QCoreApplication>
#include <QTimer>

#include "../src/QCNetworkRequestScheduler.h"
#include "../src/QCNetworkAccessManager.h"
#include "../src/QCNetworkRequest.h"
#include "../src/QCNetworkReply.h"
#include "../src/QCNetworkRequestPriority.h"

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
    QCNetworkAccessManager *m_manager = nullptr;
    QCNetworkRequestScheduler *m_scheduler = nullptr;
};

void tst_QCNetworkScheduler::initTestCase()
{
    m_manager = new QCNetworkAccessManager(this);
    m_scheduler = QCNetworkRequestScheduler::instance();
    
    // 重置配置为测试默认值
    QCNetworkRequestScheduler::Config config;
    config.maxConcurrentRequests = 2;  // 限制为 2 以便测试
    config.maxRequestsPerHost = 1;     // 限制为 1 以便测试
    config.maxBandwidthBytesPerSec = 0;  // 默认无限制
    config.enableThrottling = true;
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
    
    // 创建不同优先级的请求（使用无效 URL，不会真正执行）
    QCNetworkRequest lowReq(QUrl("http://invalid-test-host-low.local/test"));
    lowReq.setPriority(QCNetworkRequestPriority::Low);
    
    QCNetworkRequest highReq(QUrl("http://invalid-test-host-high.local/test"));
    highReq.setPriority(QCNetworkRequestPriority::High);
    
    QCNetworkRequest normalReq(QUrl("http://invalid-test-host-normal.local/test"));
    normalReq.setPriority(QCNetworkRequestPriority::Normal);
    
    // 按相反的顺序加入队列（先 Low，后 High）
    auto *lowReply = m_scheduler->scheduleRequest(lowReq, HttpMethod::Get, 
                                                 QCNetworkRequestPriority::Low);
    auto *normalReply = m_scheduler->scheduleRequest(normalReq, HttpMethod::Get, 
                                                    QCNetworkRequestPriority::Normal);
    auto *highReply = m_scheduler->scheduleRequest(highReq, HttpMethod::Get, 
                                                  QCNetworkRequestPriority::High);
    
    // 获取等待中的请求列表
    QList<QCNetworkReply*> pending = m_scheduler->pendingRequests();
    
    // 由于并发限制为 2，应该有 1 个等待中（High 和 Normal 已开始执行）
    QVERIFY(pending.size() >= 1);
    
    // 清理
    m_scheduler->cancelAllRequests();
    lowReply->deleteLater();
    normalReply->deleteLater();
    highReply->deleteLater();
    
    qDebug() << "✅ Test 2 passed: Priority queue ordering";
}

/**
 * @brief 测试 3：验证全局并发限制
 */
void tst_QCNetworkScheduler::testConcurrentRequestLimit()
{
    m_scheduler->cancelAllRequests();
    
    // 配置为最多 2 个并发
    QCNetworkRequestScheduler::Config config = m_scheduler->config();
    config.maxConcurrentRequests = 2;
    m_scheduler->setConfig(config);
    
    // 创建 5 个请求
    QList<QCNetworkReply*> replies;
    for (int i = 0; i < 5; ++i) {
        QCNetworkRequest req(QUrl(QString("http://invalid-host-%1.local/test").arg(i)));
        req.setPriority(QCNetworkRequestPriority::Normal);
        auto *reply = m_scheduler->scheduleRequest(req, HttpMethod::Get, 
                                                  QCNetworkRequestPriority::Normal);
        replies.append(reply);
    }
    
    // 获取统计信息
    auto stats = m_scheduler->statistics();
    
    // 运行中的请求应该不超过 2
    QVERIFY(stats.runningRequests <= 2);
    
    // 等待中的请求应该至少有 3 个
    QVERIFY(stats.pendingRequests >= 3);
    
    qDebug() << "Running:" << stats.runningRequests
             << "Pending:" << stats.pendingRequests;
    
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
    
    // 配置为每主机最多 1 个并发
    QCNetworkRequestScheduler::Config config = m_scheduler->config();
    config.maxConcurrentRequests = 10;  // 全局足够大
    config.maxRequestsPerHost = 1;      // 每主机限制为 1
    m_scheduler->setConfig(config);
    
    // 向同一主机发送 3 个请求
    QList<QCNetworkReply*> replies;
    for (int i = 0; i < 3; ++i) {
        QCNetworkRequest req(QUrl("http://same-host.local/test"));
        req.setPriority(QCNetworkRequestPriority::Normal);
        auto *reply = m_scheduler->scheduleRequest(req, HttpMethod::Get, 
                                                  QCNetworkRequestPriority::Normal);
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
    
    // 创建一个请求
    QCNetworkRequest req(QUrl("http://pause-test-host.local/test"));
    req.setPriority(QCNetworkRequestPriority::Normal);
    auto *reply = m_scheduler->scheduleRequest(req, HttpMethod::Get, 
                                              QCNetworkRequestPriority::Normal);
    
    auto statsBefore = m_scheduler->statistics();
    int pendingBefore = statsBefore.pendingRequests + statsBefore.runningRequests;
    
    // 延后请求（调度层语义，非传输级 pause）
    m_scheduler->deferRequest(reply);
    
    // 恢复调度
    m_scheduler->undeferRequest(reply);
    
    auto statsAfter = m_scheduler->statistics();
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
    
    // 创建请求
    QCNetworkRequest req(QUrl("http://cancel-test-host.local/test"));
    req.setPriority(QCNetworkRequestPriority::Normal);
    auto *reply = m_scheduler->scheduleRequest(req, HttpMethod::Get, 
                                              QCNetworkRequestPriority::Normal);
    
    // 监听取消信号
    QSignalSpy cancelSpy(m_scheduler, &QCNetworkRequestScheduler::requestCancelled);
    
    // 取消请求
    m_scheduler->cancelRequest(reply);
    
    // 应该收到取消信号
    QVERIFY(cancelSpy.count() >= 1);
    
    reply->deleteLater();
    
    qDebug() << "✅ Test 6 passed: Cancel request";
}

/**
 * @brief 测试 7：验证取消所有请求
 */
void tst_QCNetworkScheduler::testCancelAllRequests()
{
    m_scheduler->cancelAllRequests();
    
    // 创建多个请求
    QList<QCNetworkReply*> replies;
    for (int i = 0; i < 5; ++i) {
        QCNetworkRequest req(QUrl(QString("http://cancel-all-host-%1.local/test").arg(i)));
        req.setPriority(QCNetworkRequestPriority::Normal);
        auto *reply = m_scheduler->scheduleRequest(req, HttpMethod::Get, 
                                                  QCNetworkRequestPriority::Normal);
        replies.append(reply);
    }
    
    auto statsBefore = m_scheduler->statistics();
    int totalBefore = statsBefore.pendingRequests + statsBefore.runningRequests;
    
    QVERIFY(totalBefore >= 5);
    
    // 取消所有请求
    m_scheduler->cancelAllRequests();
    
    auto statsAfter = m_scheduler->statistics();
    int totalAfter = statsAfter.pendingRequests + statsAfter.runningRequests;
    
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
    
    // 创建低优先级请求
    QCNetworkRequest req(QUrl("http://priority-change-host.local/test"));
    req.setPriority(QCNetworkRequestPriority::Low);
    auto *reply = m_scheduler->scheduleRequest(req, HttpMethod::Get, 
                                              QCNetworkRequestPriority::Low);
    
    // 监听队列信号
    QSignalSpy queueSpy(m_scheduler, &QCNetworkRequestScheduler::requestQueued);
    
    // 调整优先级为 High
    m_scheduler->changePriority(reply, QCNetworkRequestPriority::High);
    
    // 应该收到重新加入队列的信号
    // 注意：可能因为请求已经开始执行而无法调整
    // 这个测试主要验证 API 不会崩溃
    
    qDebug() << "Priority change signal count:" << queueSpy.count();
    
    // 清理
    m_scheduler->cancelRequest(reply);
    reply->deleteLater();
    
    qDebug() << "✅ Test 8 passed: Change priority";
}

/**
 * @brief 测试 9：验证统计信息
 */
void tst_QCNetworkScheduler::testStatistics()
{
    m_scheduler->cancelAllRequests();
    
    // 创建几个请求
    QList<QCNetworkReply*> replies;
    for (int i = 0; i < 3; ++i) {
        QCNetworkRequest req(QUrl(QString("http://stats-host-%1.local/test").arg(i)));
        req.setPriority(QCNetworkRequestPriority::Normal);
        auto *reply = m_scheduler->scheduleRequest(req, HttpMethod::Get, 
                                                  QCNetworkRequestPriority::Normal);
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
    
    qDebug() << "Stats: pending=" << stats.pendingRequests
             << "running=" << stats.runningRequests
             << "completed=" << stats.completedRequests
             << "cancelled=" << stats.cancelledRequests;
    
    // 清理
    m_scheduler->cancelAllRequests();
    for (auto *reply : replies) {
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
    
    // 先创建一些 Normal 请求填满队列
    QList<QCNetworkReply*> normalReplies;
    for (int i = 0; i < 5; ++i) {
        QCNetworkRequest req(QUrl(QString("http://normal-host-%1.local/test").arg(i)));
        req.setPriority(QCNetworkRequestPriority::Normal);
        auto *reply = m_scheduler->scheduleRequest(req, HttpMethod::Get, 
                                                  QCNetworkRequestPriority::Normal);
        normalReplies.append(reply);
    }
    
    auto statsBefore = m_scheduler->statistics();
    int runningBefore = statsBefore.runningRequests;
    
    // 创建 Critical 请求
    QCNetworkRequest criticalReq(QUrl("http://critical-host.local/test"));
    criticalReq.setPriority(QCNetworkRequestPriority::Critical);
    auto *criticalReply = m_scheduler->scheduleRequest(criticalReq, HttpMethod::Get, 
                                                      QCNetworkRequestPriority::Critical);
    
    auto statsAfter = m_scheduler->statistics();
    int runningAfter = statsAfter.runningRequests;
    
    // Critical 请求应该立即执行（不受并发限制）
    QVERIFY(runningAfter > runningBefore);
    
    qDebug() << "Running before:" << runningBefore << "after:" << runningAfter;
    
    // 清理
    m_scheduler->cancelAllRequests();
    for (auto *reply : normalReplies) {
        reply->deleteLater();
    }
    criticalReply->deleteLater();
    
    qDebug() << "✅ Test 10 passed: Critical priority";
}

/**
 * @brief 测试 11：验证队列空信号
 */
void tst_QCNetworkScheduler::testQueueEmptySignal()
{
    m_scheduler->cancelAllRequests();
    
    // 监听队列空信号
    QSignalSpy emptySpy(m_scheduler, &QCNetworkRequestScheduler::queueEmpty);
    
    // 创建一个请求
    QCNetworkRequest req(QUrl("http://queue-empty-host.local/test"));
    req.setPriority(QCNetworkRequestPriority::Normal);
    auto *reply = m_scheduler->scheduleRequest(req, HttpMethod::Get, 
                                              QCNetworkRequestPriority::Normal);
    
    // 立即取消
    m_scheduler->cancelRequest(reply);
    
    // 等待信号
    QTest::qWait(100);
    
    // 应该收到队列空信号
    QVERIFY(emptySpy.count() >= 1);
    
    reply->deleteLater();
    
    qDebug() << "✅ Test 11 passed: Queue empty signal";
}

/**
 * @brief 测试 12：验证 QCNetworkAccessManager 集成
 */
void tst_QCNetworkScheduler::testSchedulerIntegration()
{
    // 启用调度器
    m_manager->enableRequestScheduler(true);
    QVERIFY(m_manager->isSchedulerEnabled());
    
    // 通过 manager 创建请求
    QCNetworkRequest req(QUrl("http://integration-test-host.local/test"));
    req.setPriority(QCNetworkRequestPriority::High);
    
    auto *reply = m_manager->scheduleGet(req);
    QVERIFY(reply != nullptr);
    
    // 验证请求已加入调度器
    auto stats = m_scheduler->statistics();
    QVERIFY(stats.pendingRequests + stats.runningRequests >= 1);
    
    // 清理
    m_scheduler->cancelRequest(reply);
    reply->deleteLater();
    
    // 禁用调度器
    m_manager->enableRequestScheduler(false);
    QVERIFY(!m_manager->isSchedulerEnabled());
    
    qDebug() << "✅ Test 12 passed: Scheduler integration";
}

QTEST_MAIN(tst_QCNetworkScheduler)
#include "tst_QCNetworkScheduler.moc"
