// SPDX-License-Identifier: MIT
// Copyright (c) 2025 QCurl Project

#include "../src/QCNetworkAccessManager.h"
#include "../src/QCNetworkMockHandler.h"
#include "../src/QCNetworkReply.h"
#include "../src/QCNetworkRequest.h"
#include "../src/QCNetworkRequestPriority.h"
#include "../src/QCNetworkRequestScheduler.h"

#include <QAbstractEventDispatcher>
#include <QCoreApplication>
#include <QMetaType>
#include <QPointer>
#include <QScopeGuard>
#include <QSet>
#include <QSignalSpy>
#include <QThread>
#include <QTimer>
#include <QtTest>

#include <thread>

using namespace QCurl;

namespace {

void verifyPriorityMetaTypeContract()
{
    const QMetaType byName = QMetaType::fromName("QCurl::QCNetworkRequestPriority");
    QVERIFY(byName.isValid());

    const QMetaType byType = QMetaType::fromType<QCNetworkRequestPriority>();
    QVERIFY(byType.isValid());
    QCOMPARE(byName.id(), byType.id());
    QCOMPARE(QString::fromLatin1(byType.name()), QStringLiteral("QCurl::QCNetworkRequestPriority"));
}

} // namespace

class PriorityEmitter : public QObject
{
    Q_OBJECT

signals:
    void priorityReady(QCNetworkRequestPriority priority);
};

class PriorityReceiver : public QObject
{
    Q_OBJECT

public slots:
    void accept(QCNetworkRequestPriority priority)
    {
        emit received(priority);
    }

signals:
    void received(QCNetworkRequestPriority priority);
};

class SchedulerAccessorWorker : public QObject
{
    Q_OBJECT

public slots:
    void lookupWorkerScheduler()
    {
        const quintptr workerThreadScheduler
            = reinterpret_cast<quintptr>(QCNetworkRequestScheduler::instance());
        emit lookupFinished(workerThreadScheduler);
    }

signals:
    void lookupFinished(quintptr workerThreadScheduler);
};

/**
 * @brief 验证 QCNetworkRequestScheduler 的稳定调度合同。
 *
 * 重点覆盖：
 * - 非抢占式优先级排序
 * - 全局/每主机并发限制
 * - defer/undefer、cancel 的语义边界
 * - 动态优先级调整、统计信息和信号通知
 * - 与 QCNetworkAccessManager 的集成路径
 */
class tst_QCNetworkScheduler : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();

    // 测试用例
    void testSchedulerEnabled();
    void testPriorityMetatypeContract();
    void testRequestStartedRequiresExecuteDispatch();
    void testCancelAfterStartQueuedDoesNotStart();
    void testCancelFromAboutToStartSlotPreventsExecute();
    void testSchedulerAccessorThreadContract();
    void testSchedulerAccessorRejectsOwnerThreadWithoutDispatcher();
    void testPriorityQueueOrdering();
    void testConcurrentRequestLimit();
    void testPerHostLimit();
    void testDeferUndefer();
    void testDeferPendingRequestRejectsRunning();
    void testDirectCancelContract();
    void testCancelRequest();
    void testCancelAllRequests();
    void testBandwidthWindowUsesProgressDeltas();
    void testChangePriority();
    void testStatistics();
    void testCriticalPriority();
    void testLaneReservationGating();
    void testDrrFairnessByLane();
    void testPerHostHeadOfLineAvoidance();
    void testHostKeyUsesOrigin();
    void testReservationFallbackProgress();
    void testCancelLaneRequests();
    void testQueueEmptySignal();
    void testSchedulerIntegration();
    void testCrossThreadCancelMarshalsToOwnerThread();

private:
    QCNetworkAccessManager *m_manager      = nullptr;
    QCNetworkRequestScheduler *m_scheduler = nullptr;
    QCNetworkMockHandler m_mock;

    QCNetworkReply *sendScheduledGet(const QCNetworkRequest &request);
    QCNetworkReply *sendScheduledGet(const QUrl &url,
                                     QCNetworkRequestPriority priority,
                                     const QString &lane = QString());
    static QString laneFromArgs(const QList<QVariant> &args, int index = 1);
    static QString hostKeyFromArgs(const QList<QVariant> &args, int index = 2);
    static QCNetworkRequestPriority priorityFromArgs(const QList<QVariant> &args, int index = 3);
};

void tst_QCNetworkScheduler::initTestCase()
{
    // 必须早于首次 QCNetworkRequestScheduler::instance()，用于锁定 canonical-name 合同。
    verifyPriorityMetaTypeContract();

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

QCNetworkReply *tst_QCNetworkScheduler::sendScheduledGet(const QCNetworkRequest &request)
{
    m_manager->enableRequestScheduler(true);
    return m_manager->sendGet(request);
}

QCNetworkReply *tst_QCNetworkScheduler::sendScheduledGet(const QUrl &url,
                                                        QCNetworkRequestPriority priority,
                                                        const QString &lane)
{
    QCNetworkRequest request(url);
    request.setPriority(priority);
    request.setLane(lane);
    m_mock.mockResponse(HttpMethod::Get, request.url(), QByteArray("OK"));
    return sendScheduledGet(request);
}

QString tst_QCNetworkScheduler::laneFromArgs(const QList<QVariant> &args, int index)
{
    return args.at(index).toString();
}

QString tst_QCNetworkScheduler::hostKeyFromArgs(const QList<QVariant> &args, int index)
{
    return args.at(index).toString();
}

QCNetworkRequestPriority tst_QCNetworkScheduler::priorityFromArgs(const QList<QVariant> &args,
                                                                 int index)
{
    return qvariant_cast<QCNetworkRequestPriority>(args.at(index));
}

/**
 * @brief 验证调度器开关状态可被 AccessManager 正确暴露。
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

    qDebug() << "Scheduler enable/disable contract verified";
}

/**
 * @brief 验证 QCNetworkRequestPriority 在库侧已具备零配置元类型合同。
 */
void tst_QCNetworkScheduler::testPriorityMetatypeContract()
{
    verifyPriorityMetaTypeContract();

    QSignalSpy queueSpy(m_scheduler, &QCNetworkRequestScheduler::requestQueued);
    auto *reply = sendScheduledGet(QUrl(QStringLiteral("http://metatype.test/queue")),
                                   QCNetworkRequestPriority::High,
                                   QStringLiteral("MetaLane"));

    QTRY_VERIFY_WITH_TIMEOUT(queueSpy.count() >= 1, 2000);
    QCOMPARE(static_cast<int>(priorityFromArgs(queueSpy.at(0))),
             static_cast<int>(QCNetworkRequestPriority::High));

    m_scheduler->cancelRequest(reply);
    QTRY_VERIFY_WITH_TIMEOUT(reply->isFinished(), 2000);
    reply->deleteLater();

    auto *emitter = new PriorityEmitter;
    PriorityReceiver receiver;
    QThread workerThread;
    emitter->moveToThread(&workerThread);
    QObject::connect(
        emitter,
        &PriorityEmitter::priorityReady,
        &receiver,
        &PriorityReceiver::accept,
        Qt::QueuedConnection);
    QSignalSpy queuedSpy(&receiver, &PriorityReceiver::received);

    workerThread.start();
    const auto stopWorker = qScopeGuard([&]() {
        workerThread.quit();
        workerThread.wait();
        delete emitter;
    });

    QMetaObject::invokeMethod(
        emitter,
        [emitter]() { emit emitter->priorityReady(QCNetworkRequestPriority::Critical); },
        Qt::QueuedConnection);

    QTRY_COMPARE_WITH_TIMEOUT(queuedSpy.count(), 1, 2000);
    QCOMPARE(static_cast<int>(qvariant_cast<QCNetworkRequestPriority>(queuedSpy.at(0).at(0))),
             static_cast<int>(QCNetworkRequestPriority::Critical));

    qDebug() << "QCNetworkRequestPriority metatype contract verified";
}

void tst_QCNetworkScheduler::testRequestStartedRequiresExecuteDispatch()
{
    m_scheduler->cancelAllRequests();
    m_mock.clear();

    const QCNetworkRequestScheduler::Config oldConfig = m_scheduler->config();
    QCNetworkRequestScheduler::Config config = oldConfig;
    config.maxConcurrentRequests             = 1;
    config.maxRequestsPerHost                = 1;
    m_scheduler->setConfig(config);

    QObject guard;
    QSignalSpy queuedSpy(m_scheduler, &QCNetworkRequestScheduler::requestQueued);
    QSignalSpy cancelledSpy(m_scheduler, &QCNetworkRequestScheduler::requestCancelled);
    QSignalSpy aboutToStartSpy(m_scheduler, &QCNetworkRequestScheduler::requestAboutToStart);
    QSignalSpy startedSpy(m_scheduler, &QCNetworkRequestScheduler::requestStarted);
    QSignalSpy finishedSpy(m_scheduler, &QCNetworkRequestScheduler::requestFinished);
    QObject::connect(
        m_scheduler,
        &QCNetworkRequestScheduler::requestQueued,
        &guard,
        [](QCNetworkReply *reply,
           const QString &,
           const QString &,
           QCNetworkRequestPriority) {
            if (reply) {
                reply->cancel();
            }
        },
        Qt::DirectConnection);

    QPointer<QCNetworkReply> reply = sendScheduledGet(
        QUrl(QStringLiteral("http://started-contract.test/drop-before-execute")),
        QCNetworkRequestPriority::Normal,
        QStringLiteral("StartedContractLane"));

    QTRY_COMPARE_WITH_TIMEOUT(queuedSpy.count(), 1, 2000);
    QTRY_COMPARE_WITH_TIMEOUT(cancelledSpy.count(), 1, 2000);
    QTRY_VERIFY_WITH_TIMEOUT(reply->isFinished(), 2000);
    QCOMPARE(aboutToStartSpy.count(), 0);
    QCOMPARE(startedSpy.count(), 0);
    QCOMPARE(finishedSpy.count(), 0);

    reply->deleteLater();

    m_scheduler->setConfig(oldConfig);

    qDebug() << "requestAboutToStart/requestStarted are now gated by execute dispatch";
}

void tst_QCNetworkScheduler::testCancelAfterStartQueuedDoesNotStart()
{
    m_scheduler->cancelAllRequests();
    m_mock.clear();

    const QCNetworkRequestScheduler::Config oldConfig = m_scheduler->config();
    QCNetworkRequestScheduler::Config config = oldConfig;
    config.maxConcurrentRequests             = 1;
    config.maxRequestsPerHost                = 1;
    m_scheduler->setConfig(config);

    QSignalSpy cancelledSpy(m_scheduler, &QCNetworkRequestScheduler::requestCancelled);
    QSignalSpy aboutToStartSpy(m_scheduler, &QCNetworkRequestScheduler::requestAboutToStart);
    QSignalSpy startedSpy(m_scheduler, &QCNetworkRequestScheduler::requestStarted);

    QPointer<QCNetworkReply> reply = sendScheduledGet(
        QUrl(QStringLiteral("http://cancel-after-queue.test/window1")),
        QCNetworkRequestPriority::Normal,
        QStringLiteral("CancelWindow1Lane"));

    // start lambda 已 queued，但尚未执行；在事件循环处理 queued handoff 前显式 cancel。
    m_scheduler->cancelRequest(reply.data());

    QTRY_COMPARE_WITH_TIMEOUT(cancelledSpy.count(), 1, 2000);
    QTRY_VERIFY_WITH_TIMEOUT(reply && reply->isFinished(), 2000);

    // 显式取消在 scheduler 侧生效后，不得再 aboutToStart/started/execute。
    QCOMPARE(aboutToStartSpy.count(), 0);
    QCOMPARE(startedSpy.count(), 0);

    if (reply) {
        reply->deleteLater();
    }
    m_scheduler->setConfig(oldConfig);

    qDebug() << "Cancel-after-queue-before-execute contract verified";
}

void tst_QCNetworkScheduler::testCancelFromAboutToStartSlotPreventsExecute()
{
    m_scheduler->cancelAllRequests();
    m_mock.clear();

    const QCNetworkRequestScheduler::Config oldConfig = m_scheduler->config();
    QCNetworkRequestScheduler::Config config = oldConfig;
    config.maxConcurrentRequests             = 1;
    config.maxRequestsPerHost                = 1;
    m_scheduler->setConfig(config);

    QObject guard;
    bool cancelledBySlot = false;

    QSignalSpy cancelledSpy(m_scheduler, &QCNetworkRequestScheduler::requestCancelled);
    QSignalSpy aboutToStartSpy(m_scheduler, &QCNetworkRequestScheduler::requestAboutToStart);
    QSignalSpy startedSpy(m_scheduler, &QCNetworkRequestScheduler::requestStarted);

    QObject::connect(
        m_scheduler,
        &QCNetworkRequestScheduler::requestAboutToStart,
        &guard,
        [this, &cancelledBySlot](QCNetworkReply *reply, const QString &, const QString &) {
            if (cancelledBySlot || !reply) {
                return;
            }
            cancelledBySlot = true;
            m_scheduler->cancelRequest(reply);
        },
        Qt::DirectConnection);

    QPointer<QCNetworkReply> reply = sendScheduledGet(
        QUrl(QStringLiteral("http://cancel-in-about-to-start.test/window2")),
        QCNetworkRequestPriority::Normal,
        QStringLiteral("CancelWindow2Lane"));

    QTRY_COMPARE_WITH_TIMEOUT(aboutToStartSpy.count(), 1, 2000);
    QTRY_COMPARE_WITH_TIMEOUT(cancelledSpy.count(), 1, 2000);
    QTRY_VERIFY_WITH_TIMEOUT(reply && reply->isFinished(), 2000);

    // 在 about-to-start 拦截点显式取消后，不得再执行 execute()（通过 started==0 可观测断言）。
    QCOMPARE(startedSpy.count(), 0);
    QVERIFY(cancelledBySlot);

    if (reply) {
        reply->deleteLater();
    }
    m_scheduler->setConfig(oldConfig);

    qDebug() << "Cancel-in-requestAboutToStart slot contract verified";
}

void tst_QCNetworkScheduler::testSchedulerAccessorThreadContract()
{
    const quintptr ownerThreadScheduler
        = reinterpret_cast<quintptr>(m_manager->schedulerOnOwnerThread());
    QVERIFY(ownerThreadScheduler != 0);

    QThread workerThread;
    auto *worker = new SchedulerAccessorWorker();
    worker->moveToThread(&workerThread);
    QObject::connect(&workerThread, &QThread::finished, worker, &QObject::deleteLater);
    QSignalSpy lookupSpy(worker, &SchedulerAccessorWorker::lookupFinished);

    workerThread.start();
    const auto stopWorker = qScopeGuard([&]() {
        workerThread.quit();
        workerThread.wait();
    });

    const bool invoked = QMetaObject::invokeMethod(
        worker, &SchedulerAccessorWorker::lookupWorkerScheduler, Qt::QueuedConnection);
    QVERIFY(invoked);

    QTRY_COMPARE_WITH_TIMEOUT(lookupSpy.count(), 1, 2000);
    const quintptr workerThreadScheduler = lookupSpy.at(0).at(0).value<quintptr>();
    QVERIFY(workerThreadScheduler != 0);
    QVERIFY(workerThreadScheduler != ownerThreadScheduler);

    qDebug() << "Scheduler accessor thread contract verified";
}

void tst_QCNetworkScheduler::testSchedulerAccessorRejectsOwnerThreadWithoutDispatcher()
{
    bool hadDispatcher    = true;
    quintptr schedulerPtr = 1;

    std::thread worker([&hadDispatcher, &schedulerPtr]() {
        hadDispatcher = QAbstractEventDispatcher::instance(QThread::currentThread()) != nullptr;

        QCNetworkAccessManager manager;
        schedulerPtr = reinterpret_cast<quintptr>(manager.schedulerOnOwnerThread());
    });
    worker.join();

    QVERIFY(!hadDispatcher);
    QCOMPARE(schedulerPtr, quintptr(0));

    qDebug() << "Scheduler accessor rejects owner thread without dispatcher";
}

/**
 * @brief 验证 pending 队列按优先级出队，且不抢占已 running 请求。
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
    auto *lowReply    = sendScheduledGet(lowReq);
    auto *normalReply = sendScheduledGet(normalReq);
    auto *highReply   = sendScheduledGet(highReq);

    // 由于并发限制为 1，只有第一个请求会进入 Running；后续不会“抢占”。
    QTRY_COMPARE_WITH_TIMEOUT(startedSpy.count(), 1, 2000);

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

    qDebug() << "Priority queue ordering verified (non-preemptive)";
}

/**
 * @brief 验证全局并发限制会把超额请求保留在 pending 队列。
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
        QCNetworkRequest req(QUrl(QStringLiteral("http://concurrent-%1.test/test").arg(i)));
        req.setPriority(QCNetworkRequestPriority::Normal);
        m_mock.mockResponse(HttpMethod::Get, req.url(), QByteArray("OK"));
        auto *reply = sendScheduledGet(req);
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

    qDebug() << "Global concurrent limit verified";
}

/**
 * @brief 验证同一 host 的运行中请求数量受 per-host 限制约束。
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
        QCNetworkRequest req(QUrl(QStringLiteral("http://same-host.test/test?i=%1").arg(i)));
        req.setPriority(QCNetworkRequestPriority::Normal);
        m_mock.mockResponse(HttpMethod::Get, req.url(), QByteArray("OK"));
        auto *reply = sendScheduledGet(req);
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

    qDebug() << "Per-host concurrent limit verified";
}

/**
 * @brief 验证 defer/undefer 只影响调度层，不表达传输级 pause。
 */
void tst_QCNetworkScheduler::testDeferUndefer()
{
    m_scheduler->cancelAllRequests();
    m_mock.clear();

    // 让请求保持在 Pending，避免被立即启动（本测试验证“调度层 defer/undefer”合同）。
    const QCNetworkRequestScheduler::Config oldConfig = m_scheduler->config();
    QCNetworkRequestScheduler::Config config = oldConfig;
    config.maxConcurrentRequests             = 0;
    config.maxRequestsPerHost                = 10;
    m_scheduler->setConfig(config);

    QSignalSpy queueSpy(m_scheduler, &QCNetworkRequestScheduler::requestQueued);

    // 创建一个请求
    QCNetworkRequest req(QUrl("http://defer.test/test"));
    req.setPriority(QCNetworkRequestPriority::High);
    m_mock.mockResponse(HttpMethod::Get, req.url(), QByteArray("OK"));
    auto *reply = sendScheduledGet(req);

    auto statsBefore  = m_scheduler->statistics();
    int pendingBefore = statsBefore.pendingRequests + statsBefore.runningRequests;

    // 延后请求（调度层语义，非传输级 pause）
    QVERIFY(m_scheduler->deferPendingRequest(reply));

    // 恢复调度
    m_scheduler->undeferRequest(reply);
    QTRY_VERIFY_WITH_TIMEOUT(queueSpy.count() >= 2, 2000);
    QCOMPARE(static_cast<int>(priorityFromArgs(queueSpy.back())),
             static_cast<int>(QCNetworkRequestPriority::High));

    auto statsAfter  = m_scheduler->statistics();
    int pendingAfter = statsAfter.pendingRequests + statsAfter.runningRequests;

    // 恢复后请求应该重新加入队列
    QVERIFY(pendingAfter >= 1);

    qDebug() << "Pending before:" << pendingBefore << "after:" << pendingAfter;

    // 清理
    m_scheduler->cancelRequest(reply);
    reply->deleteLater();

    m_scheduler->setConfig(oldConfig);

    qDebug() << "Defer/undefer scheduling contract verified";
}

/**
 * @brief 验证 deferPendingRequest() 仅对 Pending 生效；对 Running 返回 false 且不影响执行。
 */
void tst_QCNetworkScheduler::testDeferPendingRequestRejectsRunning()
{
    m_scheduler->cancelAllRequests();
    m_mock.clear();

    const QCNetworkRequestScheduler::Config oldConfig = m_scheduler->config();
    QCNetworkRequestScheduler::Config config = oldConfig;
    config.maxConcurrentRequests             = 1;
    config.maxRequestsPerHost                = 10;
    m_scheduler->setConfig(config);

    m_mock.setGlobalDelay(200);

    QSignalSpy startedSpy(m_scheduler, &QCNetworkRequestScheduler::requestStarted);

    auto *reply = sendScheduledGet(QUrl(QStringLiteral("http://defer-running.test/run")),
                                   QCNetworkRequestPriority::Normal,
                                   QStringLiteral("DeferTestLane"));

    QTRY_COMPARE_WITH_TIMEOUT(startedSpy.count(), 1, 2000);
    QVERIFY(m_scheduler->runningRequests().contains(reply));

    // Running 不允许 defer：必须返回 false，且不应把 reply 从 running 中移走或触发 cancel。
    QVERIFY(!m_scheduler->deferPendingRequest(reply));
    QVERIFY(m_scheduler->runningRequests().contains(reply));

    QTRY_VERIFY_WITH_TIMEOUT(reply->isFinished(), 2000);
    reply->deleteLater();

    m_mock.setGlobalDelay(0);
    m_scheduler->setConfig(oldConfig);

    qDebug() << "deferPendingRequest() rejects Running state as expected";
}

/**
 * @brief 验证外部直接 reply->cancel() 也会触发 scheduler 收敛，不遗留脏项。
 */
void tst_QCNetworkScheduler::testDirectCancelContract()
{
    m_scheduler->cancelAllRequests();
    m_mock.clear();

    {
        const QCNetworkRequestScheduler::Config oldConfig = m_scheduler->config();
        QCNetworkRequestScheduler::Config config = oldConfig;
        config.maxConcurrentRequests             = 0;
        config.maxRequestsPerHost                = 10;
        m_scheduler->setConfig(config);

        QSignalSpy cancelSpy(m_scheduler, &QCNetworkRequestScheduler::requestCancelled);
        QSignalSpy startedSpy(m_scheduler, &QCNetworkRequestScheduler::requestStarted);
        QSignalSpy finishedSpy(m_scheduler, &QCNetworkRequestScheduler::requestFinished);

        auto *pendingReply = sendScheduledGet(QUrl(QStringLiteral("http://direct-cancel-pending.test/one")),
                                              QCNetworkRequestPriority::Normal,
                                              QStringLiteral("DirectCancelLane"));
        auto *deferredReply = sendScheduledGet(QUrl(QStringLiteral("http://direct-cancel-deferred.test/two")),
                                               QCNetworkRequestPriority::High,
                                               QStringLiteral("DirectCancelLane"));
        QVERIFY(m_scheduler->deferPendingRequest(deferredReply));

        pendingReply->cancel();
        deferredReply->cancel();

        QTRY_COMPARE_WITH_TIMEOUT(cancelSpy.count(), 2, 2000);
        QCOMPARE(startedSpy.count(), 0);
        QCOMPARE(finishedSpy.count(), 0);
        QCOMPARE(m_scheduler->statistics().pendingRequests, 0);
        QCOMPARE(m_scheduler->statistics().runningRequests, 0);

        QTRY_VERIFY_WITH_TIMEOUT(pendingReply->isFinished(), 2000);
        QTRY_VERIFY_WITH_TIMEOUT(deferredReply->isFinished(), 2000);
        pendingReply->deleteLater();
        deferredReply->deleteLater();

        m_scheduler->setConfig(oldConfig);
    }

    m_scheduler->cancelAllRequests();
    m_mock.clear();
    m_mock.setGlobalDelay(200);

    {
        const QCNetworkRequestScheduler::Config oldConfig = m_scheduler->config();
        QCNetworkRequestScheduler::Config config = oldConfig;
        config.maxConcurrentRequests             = 1;
        config.maxRequestsPerHost                = 10;
        m_scheduler->setConfig(config);

        QSignalSpy cancelSpy(m_scheduler, &QCNetworkRequestScheduler::requestCancelled);
        QSignalSpy startedSpy(m_scheduler, &QCNetworkRequestScheduler::requestStarted);
        QSignalSpy finishedSpy(m_scheduler, &QCNetworkRequestScheduler::requestFinished);

        auto *runningReply = sendScheduledGet(QUrl(QStringLiteral("http://direct-cancel-running.test/run")),
                                              QCNetworkRequestPriority::Normal,
                                              QStringLiteral("DirectCancelLane"));
        auto *waitingReply = sendScheduledGet(QUrl(QStringLiteral("http://direct-cancel-running.test/wait")),
                                              QCNetworkRequestPriority::Normal,
                                              QStringLiteral("DirectCancelLane"));

        QTRY_COMPARE_WITH_TIMEOUT(startedSpy.count(), 1, 2000);
        QCOMPARE(qvariant_cast<QCNetworkReply *>(startedSpy.at(0).at(0)), runningReply);

        runningReply->cancel();

        QTRY_COMPARE_WITH_TIMEOUT(cancelSpy.count(), 1, 2000);
        QTRY_COMPARE_WITH_TIMEOUT(startedSpy.count(), 2, 2000);
        QCOMPARE(qvariant_cast<QCNetworkReply *>(startedSpy.at(1).at(0)), waitingReply);

        QTRY_VERIFY_WITH_TIMEOUT(runningReply->isFinished(), 2000);
        QTRY_VERIFY_WITH_TIMEOUT(waitingReply->isFinished(), 2000);
        for (int i = 0; i < finishedSpy.count(); ++i) {
            QVERIFY(qvariant_cast<QCNetworkReply *>(finishedSpy.at(i).at(0)) != runningReply);
        }

        runningReply->deleteLater();
        waitingReply->deleteLater();
        m_scheduler->setConfig(oldConfig);
    }

    m_mock.setGlobalDelay(0);

    qDebug() << "External cancel contract verified for pending/deferred/running replies";
}

/**
 * @brief 验证取消单个请求会发出取消信号并推动 reply 结束。
 */
void tst_QCNetworkScheduler::testCancelRequest()
{
    m_scheduler->cancelAllRequests();
    m_mock.clear();

    // 创建请求
    QCNetworkRequest req(QUrl("http://cancel-one.test/test"));
    req.setPriority(QCNetworkRequestPriority::Normal);
    m_mock.mockResponse(HttpMethod::Get, req.url(), QByteArray("OK"));
    auto *reply = sendScheduledGet(req);

    // 监听取消信号
    QSignalSpy cancelSpy(m_scheduler, &QCNetworkRequestScheduler::requestCancelled);

    // 取消请求
    m_scheduler->cancelRequest(reply);

    // 应该收到取消信号
    QVERIFY(cancelSpy.count() >= 1);
    QTRY_VERIFY_WITH_TIMEOUT(reply->isFinished(), 2000);

    reply->deleteLater();

    qDebug() << "Single-request cancel contract verified";
}

/**
 * @brief 验证 cancelAllRequests() 会清空 running/pending 状态。
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
        QCNetworkRequest req(QUrl(QStringLiteral("http://cancel-all-%1.test/test").arg(i)));
        req.setPriority(QCNetworkRequestPriority::Normal);
        m_mock.mockResponse(HttpMethod::Get, req.url(), QByteArray("OK"));
        auto *reply = sendScheduledGet(req);
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

    auto *freshReply = sendScheduledGet(QUrl(QStringLiteral("http://cancel-all-reset.test/fresh")),
                                        QCNetworkRequestPriority::High,
                                        QStringLiteral("CancelAllResetLane"));
    QTRY_VERIFY_WITH_TIMEOUT(freshReply->isFinished(), 2000);
    freshReply->deleteLater();

    for (auto *reply : replies) {
        reply->deleteLater();
    }

    qDebug() << "Cancel-all contract verified";
}

/**
 * @brief 验证带宽窗口按 progress delta 计数，不会把累计值重复累加成“假限流”。
 */
void tst_QCNetworkScheduler::testBandwidthWindowUsesProgressDeltas()
{
    m_scheduler->cancelAllRequests();
    m_mock.clear();

    const QByteArray oldEnv = qgetenv("QCURL_TEST_MOCK_CHAOS");
    const auto restoreEnv   = qScopeGuard([oldEnv]() {
        if (oldEnv.isEmpty()) {
            qunsetenv("QCURL_TEST_MOCK_CHAOS");
        } else {
            qputenv("QCURL_TEST_MOCK_CHAOS", oldEnv);
        }
    });
    qputenv("QCURL_TEST_MOCK_CHAOS",
            QByteArrayLiteral("seed=17;max_chunk_bytes=12;chunk_delay_ms=20"));

    const QCNetworkRequestScheduler::Config oldConfig = m_scheduler->config();
    QCNetworkRequestScheduler::Config config = oldConfig;
    config.maxConcurrentRequests             = 1;
    config.maxRequestsPerHost                = 10;
    config.maxBandwidthBytesPerSec           = 40;
    config.enableThrottling                  = true;
    m_scheduler->setConfig(config);

    const QByteArray payload("ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789");
    QVERIFY(payload.size() < config.maxBandwidthBytesPerSec);

    QCNetworkRequest firstReq(QUrl(QStringLiteral("http://bandwidth-delta.test/one")));
    firstReq.setPriority(QCNetworkRequestPriority::Normal);
    QCNetworkRequest secondReq(QUrl(QStringLiteral("http://bandwidth-delta.test/two")));
    secondReq.setPriority(QCNetworkRequestPriority::Normal);
    m_mock.mockResponse(HttpMethod::Get, firstReq.url(), payload);
    m_mock.mockResponse(HttpMethod::Get, secondReq.url(), payload);

    QSignalSpy startedSpy(m_scheduler, &QCNetworkRequestScheduler::requestStarted);
    QSignalSpy throttledSpy(m_scheduler, &QCNetworkRequestScheduler::bandwidthThrottled);

    auto *firstReply = sendScheduledGet(firstReq);
    auto *secondReply = sendScheduledGet(secondReq);

    QTRY_COMPARE_WITH_TIMEOUT(startedSpy.count(), 1, 2000);
    QTRY_COMPARE_WITH_TIMEOUT(startedSpy.count(), 2, 700);
    QCOMPARE(qvariant_cast<QCNetworkReply *>(startedSpy.at(1).at(0)), secondReply);
    QCOMPARE(throttledSpy.count(), 0);

    QTRY_VERIFY_WITH_TIMEOUT(firstReply->isFinished(), 2000);
    QTRY_VERIFY_WITH_TIMEOUT(secondReply->isFinished(), 2000);

    firstReply->deleteLater();
    secondReply->deleteLater();
    m_scheduler->setConfig(oldConfig);

    qDebug() << "Bandwidth window delta counting verified";
}

/**
 * @brief 验证 pending 请求调整优先级时不会破坏队列状态。
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
    auto *reply = sendScheduledGet(req);

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

    qDebug() << "Priority change contract verified";
}

/**
 * @brief 验证统计字段在离线调度路径下可读且语义自洽。
 */
void tst_QCNetworkScheduler::testStatistics()
{
    m_scheduler->cancelAllRequests();
    m_mock.clear();
    m_mock.setGlobalDelay(150);

    QCNetworkRequestScheduler::Config config = m_scheduler->config();
    config.maxConcurrentRequests             = 1;
    config.maxRequestsPerHost                = 10;
    m_scheduler->setConfig(config);
    const auto statsBefore = m_scheduler->statistics();

    // 创建几个请求
    QList<QCNetworkReply *> replies;
    for (int i = 0; i < 3; ++i) {
        QCNetworkRequest req(QUrl(QStringLiteral("http://stats-%1.test/test").arg(i)));
        req.setPriority(QCNetworkRequestPriority::Normal);
        m_mock.mockResponse(HttpMethod::Get, req.url(), QByteArray("OK"));
        auto *reply = sendScheduledGet(req);
        replies.append(reply);
    }

    QTRY_COMPARE_WITH_TIMEOUT(m_scheduler->statistics().runningRequests, 1, 2000);
    QTRY_COMPARE_WITH_TIMEOUT(m_scheduler->statistics().pendingRequests, 2, 2000);

    for (auto *reply : replies) {
        QTRY_VERIFY_WITH_TIMEOUT(reply->isFinished(), 4000);
    }

    const auto stats = m_scheduler->statistics();

    // 统计口径属于 smoke：验证稳定且可重复的粗粒度合同，不在这里绑定每个实现细节。
    QCOMPARE(stats.pendingRequests, 0);
    QCOMPARE(stats.runningRequests, 0);
    QCOMPARE(stats.completedRequests, statsBefore.completedRequests + 3);
    QCOMPARE(stats.cancelledRequests, statsBefore.cancelledRequests);
    QVERIFY(stats.totalBytesReceived >= statsBefore.totalBytesReceived);
    QCOMPARE(stats.totalBytesSent, statsBefore.totalBytesSent);
    QVERIFY(stats.avgResponseTime >= 0.0);

    // 清理
    m_scheduler->cancelAllRequests();
    for (auto *reply : replies) {
        reply->deleteLater();
    }
    m_mock.setGlobalDelay(0);

    qDebug() << "Scheduler statistics contract verified";
}

/**
 * @brief 验证 Critical 不突破硬上限，但仍会在 lane 内优先启动。
 */
void tst_QCNetworkScheduler::testCriticalPriority()
{
    m_scheduler->cancelAllRequests();
    m_mock.clear();
    m_mock.setGlobalDelay(150);

    QCNetworkRequestScheduler::Config config = m_scheduler->config();
    config.maxConcurrentRequests             = 1;
    config.maxRequestsPerHost                = 10;
    m_scheduler->setConfig(config);

    QSignalSpy startedSpy(m_scheduler, &QCNetworkRequestScheduler::requestStarted);

    auto *lowReply = sendScheduledGet(QUrl(QStringLiteral("http://critical-low.test/test")),
                                      QCNetworkRequestPriority::Low,
                                      QStringLiteral("CriticalLane"));
    auto *normalReply = sendScheduledGet(QUrl(QStringLiteral("http://critical-normal.test/test")),
                                         QCNetworkRequestPriority::Normal,
                                         QStringLiteral("CriticalLane"));
    auto *criticalReply = sendScheduledGet(QUrl(QStringLiteral("http://critical-fast.test/test")),
                                           QCNetworkRequestPriority::Critical,
                                           QStringLiteral("CriticalLane"));

    QTRY_COMPARE_WITH_TIMEOUT(startedSpy.count(), 1, 2000);
    QCOMPARE(m_scheduler->statistics().runningRequests, 1);
    QCOMPARE(m_scheduler->statistics().pendingRequests, 2);

    QTRY_COMPARE_WITH_TIMEOUT(startedSpy.count(), 3, 4000);
    QCOMPARE(qvariant_cast<QCNetworkReply *>(startedSpy.at(0).at(0)), lowReply);
    QCOMPARE(qvariant_cast<QCNetworkReply *>(startedSpy.at(1).at(0)), criticalReply);
    QCOMPARE(qvariant_cast<QCNetworkReply *>(startedSpy.at(2).at(0)), normalReply);

    QTRY_VERIFY_WITH_TIMEOUT(lowReply->isFinished(), 4000);
    QTRY_VERIFY_WITH_TIMEOUT(criticalReply->isFinished(), 4000);
    QTRY_VERIFY_WITH_TIMEOUT(normalReply->isFinished(), 4000);

    lowReply->deleteLater();
    normalReply->deleteLater();
    criticalReply->deleteLater();
    m_mock.setGlobalDelay(0);

    qDebug() << "Critical priority now respects hard limits and lane ordering";
}

/**
 * @brief 验证 reservation 会优先调度 Control lane。
 */
void tst_QCNetworkScheduler::testLaneReservationGating()
{
    m_scheduler->cancelAllRequests();
    m_mock.clear();
    m_mock.setGlobalDelay(150);

    QCNetworkRequestScheduler::LaneConfig controlCfg;
    controlCfg.reservedGlobal = 1;
    m_scheduler->setLaneConfig(QStringLiteral("ReservationControl"), controlCfg);
    m_scheduler->setLaneConfig(QStringLiteral("ReservationTransfer"),
                               QCNetworkRequestScheduler::LaneConfig{});

    QCNetworkRequestScheduler::Config config = m_scheduler->config();
    config.maxConcurrentRequests             = 0;
    config.maxRequestsPerHost                = 10;
    m_scheduler->setConfig(config);

    QSignalSpy startedSpy(m_scheduler, &QCNetworkRequestScheduler::requestStarted);

    auto *transferReply = sendScheduledGet(QUrl(QStringLiteral("http://reservation-transfer.test/test")),
                                           QCNetworkRequestPriority::VeryHigh,
                                           QStringLiteral("ReservationTransfer"));
    auto *controlReply = sendScheduledGet(QUrl(QStringLiteral("http://reservation-control.test/test")),
                                          QCNetworkRequestPriority::Low,
                                          QStringLiteral("ReservationControl"));

    config.maxConcurrentRequests = 1;
    m_scheduler->setConfig(config);

    QTRY_COMPARE_WITH_TIMEOUT(startedSpy.count(), 1, 2000);
    QCOMPARE(qvariant_cast<QCNetworkReply *>(startedSpy.at(0).at(0)), controlReply);
    QCOMPARE(laneFromArgs(startedSpy.at(0)), QStringLiteral("ReservationControl"));

    m_scheduler->cancelAllRequests();
    QTRY_VERIFY_WITH_TIMEOUT(controlReply->isFinished(), 2000);
    QTRY_VERIFY_WITH_TIMEOUT(transferReply->isFinished(), 2000);
    controlReply->deleteLater();
    transferReply->deleteLater();
    m_mock.setGlobalDelay(0);

    qDebug() << "Lane reservation gating verified";
}

/**
 * @brief 验证 DRR 会按 weight 比例分配 lane 启动份额。
 */
void tst_QCNetworkScheduler::testDrrFairnessByLane()
{
    m_scheduler->cancelAllRequests();
    m_mock.clear();
    m_mock.setGlobalDelay(120);

    QCNetworkRequestScheduler::LaneConfig controlCfg;
    controlCfg.weight = 3;
    controlCfg.quantum = 1;
    QCNetworkRequestScheduler::LaneConfig transferCfg;
    transferCfg.weight = 1;
    transferCfg.quantum = 1;
    m_scheduler->setLaneConfig(QStringLiteral("FairControl"), controlCfg);
    m_scheduler->setLaneConfig(QStringLiteral("FairTransfer"), transferCfg);

    QCNetworkRequestScheduler::Config config = m_scheduler->config();
    config.maxConcurrentRequests             = 0;
    config.maxRequestsPerHost                = 10;
    m_scheduler->setConfig(config);

    QList<QCNetworkReply *> replies;
    for (int i = 0; i < 6; ++i) {
        replies.append(sendScheduledGet(
            QUrl(QStringLiteral("http://fair-control-%1.test/test").arg(i)),
            QCNetworkRequestPriority::Normal,
            QStringLiteral("FairControl")));
    }
    for (int i = 0; i < 2; ++i) {
        replies.append(sendScheduledGet(
            QUrl(QStringLiteral("http://fair-transfer-%1.test/test").arg(i)),
            QCNetworkRequestPriority::Normal,
            QStringLiteral("FairTransfer")));
    }

    QSignalSpy startedSpy(m_scheduler, &QCNetworkRequestScheduler::requestStarted);

    config.maxConcurrentRequests = 1;
    m_scheduler->setConfig(config);

    QTRY_COMPARE_WITH_TIMEOUT(startedSpy.count(), 8, 5000);

    const QStringList expectedLanes = {
        QStringLiteral("FairControl"),
        QStringLiteral("FairControl"),
        QStringLiteral("FairControl"),
        QStringLiteral("FairTransfer"),
        QStringLiteral("FairControl"),
        QStringLiteral("FairControl"),
        QStringLiteral("FairControl"),
        QStringLiteral("FairTransfer"),
    };

    QStringList actualLanes;
    for (int i = 0; i < startedSpy.count(); ++i) {
        actualLanes.append(laneFromArgs(startedSpy.at(i)));
    }
    QCOMPARE(actualLanes, expectedLanes);

    for (auto *reply : replies) {
        QTRY_VERIFY_WITH_TIMEOUT(reply->isFinished(), 5000);
        reply->deleteLater();
    }
    m_mock.setGlobalDelay(0);

    qDebug() << "DRR fairness contract verified";
}

/**
 * @brief 验证同 lane 内会跳过被 per-host 限流的队首 host，避免 HOL。
 */
void tst_QCNetworkScheduler::testPerHostHeadOfLineAvoidance()
{
    m_scheduler->cancelAllRequests();
    m_mock.clear();
    m_mock.setGlobalDelay(150);

    QCNetworkRequestScheduler::Config config = m_scheduler->config();
    config.maxConcurrentRequests             = 0;
    config.maxRequestsPerHost                = 1;
    m_scheduler->setConfig(config);

    QSignalSpy startedSpy(m_scheduler, &QCNetworkRequestScheduler::requestStarted);

    auto *replyA1 = sendScheduledGet(QUrl(QStringLiteral("http://hol-a.test/test-1")),
                                     QCNetworkRequestPriority::Normal,
                                     QStringLiteral("HolLane"));
    auto *replyA2 = sendScheduledGet(QUrl(QStringLiteral("http://hol-a.test/test-2")),
                                     QCNetworkRequestPriority::Normal,
                                     QStringLiteral("HolLane"));
    auto *replyB1 = sendScheduledGet(QUrl(QStringLiteral("http://hol-b.test/test-1")),
                                     QCNetworkRequestPriority::Normal,
                                     QStringLiteral("HolLane"));

    config.maxConcurrentRequests = 2;
    m_scheduler->setConfig(config);

    QTRY_COMPARE_WITH_TIMEOUT(startedSpy.count(), 2, 2000);

    QSet<QString> startedHosts;
    startedHosts.insert(hostKeyFromArgs(startedSpy.at(0)));
    startedHosts.insert(hostKeyFromArgs(startedSpy.at(1)));
    QCOMPARE(startedHosts,
             QSet<QString>({QStringLiteral("http://hol-a.test:80"),
                            QStringLiteral("http://hol-b.test:80")}));

    m_scheduler->cancelAllRequests();
    QTRY_VERIFY_WITH_TIMEOUT(replyA1->isFinished(), 2000);
    QTRY_VERIFY_WITH_TIMEOUT(replyA2->isFinished(), 2000);
    QTRY_VERIFY_WITH_TIMEOUT(replyB1->isFinished(), 2000);
    replyA1->deleteLater();
    replyA2->deleteLater();
    replyB1->deleteLater();
    m_mock.setGlobalDelay(0);

    qDebug() << "Per-host HOL avoidance verified";
}

/**
 * @brief 验证 hostKey 使用 origin，而非仅 host。
 */
void tst_QCNetworkScheduler::testHostKeyUsesOrigin()
{
    m_scheduler->cancelAllRequests();
    m_mock.clear();
    m_mock.setGlobalDelay(150);

    QCNetworkRequestScheduler::Config config = m_scheduler->config();
    config.maxConcurrentRequests             = 0;
    config.maxRequestsPerHost                = 1;
    m_scheduler->setConfig(config);

    QSignalSpy startedSpy(m_scheduler, &QCNetworkRequestScheduler::requestStarted);

    auto *httpReply = sendScheduledGet(QUrl(QStringLiteral("http://origin.test/path")),
                                       QCNetworkRequestPriority::Normal,
                                       QStringLiteral("OriginLane"));
    auto *httpsReply = sendScheduledGet(QUrl(QStringLiteral("https://origin.test/path")),
                                        QCNetworkRequestPriority::Normal,
                                        QStringLiteral("OriginLane"));
    auto *wsReply = sendScheduledGet(QUrl(QStringLiteral("ws://origin.test/path")),
                                     QCNetworkRequestPriority::Normal,
                                     QStringLiteral("OriginLane"));
    auto *wssReply = sendScheduledGet(QUrl(QStringLiteral("wss://origin.test/path")),
                                      QCNetworkRequestPriority::Normal,
                                      QStringLiteral("OriginLane"));
    auto *customPortReply = sendScheduledGet(QUrl(QStringLiteral("http://origin.test:8080/path")),
                                             QCNetworkRequestPriority::Normal,
                                             QStringLiteral("OriginLane"));
    auto *ipv6Reply = sendScheduledGet(QUrl(QStringLiteral("http://[2001:db8::1]/path")),
                                       QCNetworkRequestPriority::Normal,
                                       QStringLiteral("OriginLane"));
    auto *unknownSchemeReply = sendScheduledGet(QUrl(QStringLiteral("custom://origin.test/path")),
                                                QCNetworkRequestPriority::Normal,
                                                QStringLiteral("OriginLane"));

    config.maxConcurrentRequests = 7;
    m_scheduler->setConfig(config);

    QTRY_COMPARE_WITH_TIMEOUT(startedSpy.count(), 7, 2000);

    QSet<QString> startedHosts;
    for (int i = 0; i < startedSpy.count(); ++i) {
        startedHosts.insert(hostKeyFromArgs(startedSpy.at(i)));
    }
    QCOMPARE(startedHosts,
             QSet<QString>({QStringLiteral("http://origin.test:80"),
                            QStringLiteral("https://origin.test:443"),
                            QStringLiteral("ws://origin.test:80"),
                            QStringLiteral("wss://origin.test:443"),
                            QStringLiteral("http://origin.test:8080"),
                            QStringLiteral("http://[2001:db8::1]:80"),
                            QStringLiteral("custom://origin.test:0")}));

    m_scheduler->cancelAllRequests();
    QTRY_VERIFY_WITH_TIMEOUT(httpReply->isFinished(), 2000);
    QTRY_VERIFY_WITH_TIMEOUT(httpsReply->isFinished(), 2000);
    QTRY_VERIFY_WITH_TIMEOUT(wsReply->isFinished(), 2000);
    QTRY_VERIFY_WITH_TIMEOUT(wssReply->isFinished(), 2000);
    QTRY_VERIFY_WITH_TIMEOUT(customPortReply->isFinished(), 2000);
    QTRY_VERIFY_WITH_TIMEOUT(ipv6Reply->isFinished(), 2000);
    QTRY_VERIFY_WITH_TIMEOUT(unknownSchemeReply->isFinished(), 2000);
    httpReply->deleteLater();
    httpsReply->deleteLater();
    wsReply->deleteLater();
    wssReply->deleteLater();
    customPortReply->deleteLater();
    ipv6Reply->deleteLater();
    unknownSchemeReply->deleteLater();
    m_mock.setGlobalDelay(0);

    qDebug() << "Origin hostKey contract verified";
}

/**
 * @brief 验证不可满足的 reservation 不会让其他 host 的请求饿死。
 */
void tst_QCNetworkScheduler::testReservationFallbackProgress()
{
    m_scheduler->cancelAllRequests();
    m_mock.clear();
    m_mock.setGlobalDelay(150);

    QCNetworkRequestScheduler::LaneConfig controlCfg;
    controlCfg.reservedPerHost = 1;
    m_scheduler->setLaneConfig(QStringLiteral("FallbackControl"), controlCfg);
    m_scheduler->setLaneConfig(QStringLiteral("FallbackTransfer"),
                               QCNetworkRequestScheduler::LaneConfig{});

    QCNetworkRequestScheduler::Config config = m_scheduler->config();
    config.maxConcurrentRequests             = 2;
    config.maxRequestsPerHost                = 1;
    m_scheduler->setConfig(config);

    QSignalSpy startedSpy(m_scheduler, &QCNetworkRequestScheduler::requestStarted);

    auto *blockingTransfer = sendScheduledGet(QUrl(QStringLiteral("http://blocked-host.test/run")),
                                              QCNetworkRequestPriority::Normal,
                                              QStringLiteral("FallbackTransfer"));
    QTRY_COMPARE_WITH_TIMEOUT(startedSpy.count(), 1, 2000);

    auto *blockedControl = sendScheduledGet(QUrl(QStringLiteral("http://blocked-host.test/control")),
                                            QCNetworkRequestPriority::High,
                                            QStringLiteral("FallbackControl"));
    auto *freeTransfer = sendScheduledGet(QUrl(QStringLiteral("http://free-host.test/run")),
                                          QCNetworkRequestPriority::Normal,
                                          QStringLiteral("FallbackTransfer"));

    QTRY_COMPARE_WITH_TIMEOUT(startedSpy.count(), 2, 2000);
    QCOMPARE(qvariant_cast<QCNetworkReply *>(startedSpy.at(1).at(0)), freeTransfer);
    QCOMPARE(hostKeyFromArgs(startedSpy.at(1)), QStringLiteral("http://free-host.test:80"));

    m_scheduler->cancelAllRequests();
    QTRY_VERIFY_WITH_TIMEOUT(blockingTransfer->isFinished(), 2000);
    QTRY_VERIFY_WITH_TIMEOUT(blockedControl->isFinished(), 2000);
    QTRY_VERIFY_WITH_TIMEOUT(freeTransfer->isFinished(), 2000);
    blockingTransfer->deleteLater();
    blockedControl->deleteLater();
    freeTransfer->deleteLater();
    m_mock.setGlobalDelay(0);

    qDebug() << "Reservation fallback progress verified";
}

/**
 * @brief 验证 cancelLaneRequests 覆盖 pending/deferred/running 三种状态。
 */
void tst_QCNetworkScheduler::testCancelLaneRequests()
{
    m_scheduler->cancelAllRequests();
    m_mock.clear();

    QCNetworkRequestScheduler::Config config = m_scheduler->config();
    config.maxConcurrentRequests             = 0;
    config.maxRequestsPerHost                = 10;
    m_scheduler->setConfig(config);

    QSignalSpy cancelSpy(m_scheduler, &QCNetworkRequestScheduler::requestCancelled);

    auto *pendingTransfer = sendScheduledGet(QUrl(QStringLiteral("http://cancel-pending.test/one")),
                                             QCNetworkRequestPriority::Normal,
                                             QStringLiteral("CancelTransfer"));
    auto *deferredTransfer = sendScheduledGet(QUrl(QStringLiteral("http://cancel-deferred.test/two")),
                                              QCNetworkRequestPriority::Normal,
                                              QStringLiteral("CancelTransfer"));
    auto *controlPending = sendScheduledGet(QUrl(QStringLiteral("http://cancel-control.test/three")),
                                            QCNetworkRequestPriority::Normal,
                                            QStringLiteral("CancelControl"));

    QVERIFY(m_scheduler->deferPendingRequest(deferredTransfer));

    QCOMPARE(m_scheduler->cancelLaneRequests(QStringLiteral("CancelTransfer"),
                                             QCNetworkRequestScheduler::CancelLaneScope::PendingOnly),
             2);
    QTRY_COMPARE_WITH_TIMEOUT(cancelSpy.count(), 2, 2000);
    QCOMPARE(laneFromArgs(cancelSpy.at(0)), QStringLiteral("CancelTransfer"));
    QCOMPARE(laneFromArgs(cancelSpy.at(1)), QStringLiteral("CancelTransfer"));
    QCOMPARE(m_scheduler->statistics().pendingRequests, 1);
    QCOMPARE(m_scheduler->statistics().runningRequests, 0);

    m_scheduler->cancelAllRequests();
    QTRY_VERIFY_WITH_TIMEOUT(pendingTransfer->isFinished(), 2000);
    QTRY_VERIFY_WITH_TIMEOUT(deferredTransfer->isFinished(), 2000);
    QTRY_VERIFY_WITH_TIMEOUT(controlPending->isFinished(), 2000);
    pendingTransfer->deleteLater();
    deferredTransfer->deleteLater();
    controlPending->deleteLater();

    m_mock.clear();
    m_mock.setGlobalDelay(150);
    cancelSpy.clear();

    config.maxConcurrentRequests = 1;
    m_scheduler->setConfig(config);

    QSignalSpy startedSpy(m_scheduler, &QCNetworkRequestScheduler::requestStarted);

    auto *runningTransfer = sendScheduledGet(QUrl(QStringLiteral("http://cancel-running.test/run")),
                                             QCNetworkRequestPriority::Normal,
                                             QStringLiteral("CancelTransfer"));
    QTRY_COMPARE_WITH_TIMEOUT(startedSpy.count(), 1, 2000);

    auto *waitingControl = sendScheduledGet(QUrl(QStringLiteral("http://cancel-running.test/control")),
                                            QCNetworkRequestPriority::Normal,
                                            QStringLiteral("CancelControl"));

    QCOMPARE(m_scheduler->cancelLaneRequests(
                 QStringLiteral("CancelTransfer"),
                 QCNetworkRequestScheduler::CancelLaneScope::PendingAndRunning),
             1);
    QTRY_COMPARE_WITH_TIMEOUT(cancelSpy.count(), 1, 2000);
    QCOMPARE(laneFromArgs(cancelSpy.at(0)), QStringLiteral("CancelTransfer"));
    QTRY_COMPARE_WITH_TIMEOUT(startedSpy.count(), 2, 2000);
    QCOMPARE(qvariant_cast<QCNetworkReply *>(startedSpy.at(1).at(0)), waitingControl);
    QCOMPARE(laneFromArgs(startedSpy.at(1)), QStringLiteral("CancelControl"));

    m_scheduler->cancelAllRequests();
    QTRY_VERIFY_WITH_TIMEOUT(runningTransfer->isFinished(), 2000);
    QTRY_VERIFY_WITH_TIMEOUT(waitingControl->isFinished(), 2000);
    runningTransfer->deleteLater();
    waitingControl->deleteLater();
    m_mock.setGlobalDelay(0);

    qDebug() << "cancelLaneRequests contract verified";
}

/**
 * @brief 验证队列完全清空时会发出 queueEmpty 信号。
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
    auto *reply = sendScheduledGet(req);

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

    qDebug() << "queueEmpty signal contract verified";
}

/**
 * @brief 验证 AccessManager 在启用调度器后能正确转发异步请求。
 */
void tst_QCNetworkScheduler::testSchedulerIntegration()
{
    m_mock.clear();

    // 启用调度器
    m_manager->enableRequestScheduler(true);
    QVERIFY(m_manager->isSchedulerEnabled());

    auto runAsyncMethod = [this](HttpMethod method,
                                 const QUrl &url,
                                 const QByteArray &body = QByteArray()) {
        QCNetworkRequest req(url);
        req.setPriority(QCNetworkRequestPriority::High);
        m_mock.mockResponse(method, req.url(), QByteArray("OK"));

        QSignalSpy queuedSpy(m_scheduler, &QCNetworkRequestScheduler::requestQueued);
        QSignalSpy startedSpy(m_scheduler, &QCNetworkRequestScheduler::requestStarted);

        QCNetworkReply *reply = nullptr;
        switch (method) {
            case HttpMethod::Head:
                reply = m_manager->sendHead(req);
                break;
            case HttpMethod::Get:
                reply = m_manager->sendGet(req);
                break;
            case HttpMethod::Post:
                reply = m_manager->sendPost(req, body);
                break;
            case HttpMethod::Put:
                reply = m_manager->sendPut(req, body);
                break;
            case HttpMethod::Delete:
                reply = m_manager->sendDelete(req, body);
                break;
            case HttpMethod::Patch:
                reply = m_manager->sendPatch(req, body);
                break;
        }

        QVERIFY(reply != nullptr);
        QVERIFY(reply->parent() == m_manager);
        QTRY_VERIFY_WITH_TIMEOUT(queuedSpy.count() + startedSpy.count() >= 1, 2000);
        QTRY_VERIFY_WITH_TIMEOUT(reply->isFinished(), 2000);
        QCOMPARE(reply->error(), NetworkError::NoError);
        reply->deleteLater();
    };

    runAsyncMethod(HttpMethod::Head, QUrl("http://integration.test/head"));
    runAsyncMethod(HttpMethod::Get, QUrl("http://integration.test/get"));
    runAsyncMethod(HttpMethod::Post, QUrl("http://integration.test/post"), QByteArray("POST"));
    runAsyncMethod(HttpMethod::Put, QUrl("http://integration.test/put"), QByteArray("PUT"));
    runAsyncMethod(HttpMethod::Delete, QUrl("http://integration.test/delete"), QByteArray("DEL"));
    runAsyncMethod(HttpMethod::Patch, QUrl("http://integration.test/patch"), QByteArray("PATCH"));

    // 禁用调度器
    m_manager->enableRequestScheduler(false);
    QVERIFY(!m_manager->isSchedulerEnabled());

    qDebug() << "AccessManager integration contract verified";
}

void tst_QCNetworkScheduler::testCrossThreadCancelMarshalsToOwnerThread()
{
    m_scheduler->cancelAllRequests();
    m_mock.clear();

    const QCNetworkRequestScheduler::Config oldConfig = m_scheduler->config();
    QCNetworkRequestScheduler::Config config = oldConfig;
    config.maxConcurrentRequests             = 0;
    config.maxRequestsPerHost                = 1;
    m_scheduler->setConfig(config);

    QSignalSpy cancelSpy(m_scheduler, &QCNetworkRequestScheduler::requestCancelled);
    QSignalSpy startedSpy(m_scheduler, &QCNetworkRequestScheduler::requestStarted);

    QThread *signalThread = nullptr;
    QObject probe;
    QObject::connect(
        m_scheduler,
        &QCNetworkRequestScheduler::requestCancelled,
        &probe,
        [&signalThread](QCNetworkReply *, const QString &, const QString &) {
            signalThread = QThread::currentThread();
        },
        Qt::DirectConnection);

    auto *reply = sendScheduledGet(QUrl(QStringLiteral("http://cross-thread-cancel.test/pending")),
                                   QCNetworkRequestPriority::Normal,
                                   QStringLiteral("CrossThreadLane"));
    QCOMPARE(startedSpy.count(), 0);

    QThread workerThread;
    auto *workerContext = new QObject;
    workerContext->moveToThread(&workerThread);
    QObject::connect(&workerThread, &QThread::finished, workerContext, &QObject::deleteLater);
    workerThread.start();
    const auto stopWorker = qScopeGuard([&]() {
        workerThread.quit();
        workerThread.wait();
    });

    QMetaObject::invokeMethod(
        workerContext,
        [scheduler = m_scheduler, reply]() { scheduler->cancelRequest(reply); },
        Qt::QueuedConnection);

    QTRY_COMPARE_WITH_TIMEOUT(cancelSpy.count(), 1, 2000);
    QTRY_VERIFY_WITH_TIMEOUT(reply->isFinished(), 2000);
    QCOMPARE(signalThread, m_scheduler->thread());
    QCOMPARE(qvariant_cast<QCNetworkReply *>(cancelSpy.at(0).at(0)), reply);

    reply->deleteLater();
    m_scheduler->setConfig(oldConfig);

    qDebug() << "Cross-thread scheduler cancel marshalled back to owner thread";
}

QTEST_MAIN(tst_QCNetworkScheduler)
#include "tst_QCNetworkScheduler.moc"
