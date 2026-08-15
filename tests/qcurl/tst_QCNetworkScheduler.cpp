// SPDX-License-Identifier: MIT
// Copyright (c) 2025 QCurl Project

#include "../src/QCNetworkAccessManager.h"
#include "../src/QCNetworkMockHandler.h"
#include "../src/QCNetworkReply.h"
#include "../src/QCNetworkRequest.h"
#include "../src/QCNetworkRequestPriority.h"
#include "../src/QCNetworkRequestScheduler.h"
#include "../src/QCNetworkSchedulerPolicy.h"
#include "../src/private/QCNetworkRequestSchedulerTestAccess_p.h"
#include "qcnetwork_mock_test_support.h"

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

#include <future>
#include <thread>

using namespace QCurl;

namespace {

void verifyPriorityMetaTypeContract()
{
    // 按名称和按类型查到的 id 必须一致，queued connection/QSignalSpy 才能共享同一合同。
    const QMetaType byName = QMetaType::fromName("QCurl::QCNetworkRequestPriority");
    QVERIFY(byName.isValid());

    const QMetaType byType = QMetaType::fromType<QCNetworkRequestPriority>();
    QVERIFY(byType.isValid());
    QCOMPARE(byName.id(), byType.id());
    QCOMPARE(QString::fromLatin1(byType.name()), QStringLiteral("QCurl::QCNetworkRequestPriority"));
}

template<typename T>
void verifyCanonicalMetaTypeContract(const char *canonicalName)
{
    const QMetaType byName = QMetaType::fromName(canonicalName);
    QVERIFY(byName.isValid());

    const QMetaType byType = QMetaType::fromType<T>();
    QVERIFY(byType.isValid());
    QCOMPARE(byName.id(), byType.id());
    QCOMPARE(QString::fromLatin1(byType.name()), QString::fromLatin1(canonicalName));
}

} // namespace

class PriorityEmitter : public QObject
{
    Q_OBJECT

public:
    void emitPriority(QCNetworkRequestPriority priority) { Q_EMIT priorityReady(priority); }

Q_SIGNALS:
    void priorityReady(QCNetworkRequestPriority priority);
};

class PriorityReceiver : public QObject
{
    Q_OBJECT

public Q_SLOTS:
    void accept(QCNetworkRequestPriority priority) { Q_EMIT received(priority); }

Q_SIGNALS:
    void received(QCNetworkRequestPriority priority);
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

private Q_SLOTS:
    void initTestCase();
    void cleanupTestCase();

    // 测试用例
    void testSchedulerEnabled();
    void testPriorityMetatypeContract();
    void testRequestStartedRequiresExecuteDispatch();
    void testCancelAfterStartQueuedDoesNotStart();
    void testCancelFromAboutToStartSlotPreventsExecute();
    void testRequestQueuedReplyDeletionStopsContinuation();
    void testRequestQueuedSchedulerDeletionStopsContinuation();
    void testUndeferRequestQueuedSchedulerDeletionStopsContinuation();
    void testCancelRequestCancelledSchedulerDeletionStopsContinuation();
    void testFinalizeCancelledSchedulerDeletionStopsContinuation();
    void testManagerOwnedSchedulerForTestingOwnerThreadContract();
    void testSchedulerConstructionDoesNotStartThrottleTimer();
    void testManagerLevelSchedulerPolicyConfiguresAdmission();
    void testUnknownLaneFailsClosed();
    void testManagerCancelLaneRequestsReturnsStructuredFailClosedResult();
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
    void testInvalidSchedulerPriorityFailsClosed();
    void testSchedulerCommandResultFailuresAreSideEffectFree();
    void testSchedulerCommandResultNoChangeAndInvalidArguments();
    void testSchedulerCommandResultRejectsAffinityMismatch();
    void testCrossThreadSchedulerCommandsAreRejectedWithoutMutation();
    void testCrossThreadCancelIsRejectedWithoutMutation();
    void testPrivateOwnerThreadPathsRejectWrongThreadWithoutMutation();
    void testProgressTrackingAutoConnectionAndTeardown();

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
    // 必须早于首次 QCNetworkRequestScheduler::instanceForTesting()，用于锁定 canonical-name 合同。
    verifyPriorityMetaTypeContract();

    m_manager   = new QCNetworkAccessManager(this);
    m_scheduler = m_manager->schedulerForTesting();
    QVERIFY(m_scheduler != nullptr);

    m_mock.clear();
    m_mock.setCaptureEnabled(false);
    m_mock.setGlobalDelay(0);
    QCurl::TestSupport::setMockHandler(*m_manager, &m_mock);

    qRegisterMetaType<QCNetworkReply *>("QCNetworkReply*");

    QCNetworkRequestScheduler::Config config;
    config.setMaxConcurrentRequests(2);
    config.setMaxRequestsPerHost(1);
    config.setMaxBandwidthBytesPerSec(0);
    config.setEnableThrottling(true);
    m_scheduler->setConfigForTesting(config);

    qDebug() << "Test initialized with config:"
             << "maxConcurrent=" << config.maxConcurrentRequests()
             << "maxPerHost=" << config.maxRequestsPerHost();
}

void tst_QCNetworkScheduler::cleanupTestCase()
{
    QCOMPARE(m_scheduler->cancelAllRequests(), QCNetworkRequestScheduler::CommandResult::NoChange);
    m_manager = nullptr;
}

QCNetworkReply *tst_QCNetworkScheduler::sendScheduledGet(const QCNetworkRequest &request)
{
    m_manager->enableRequestScheduler(true);
    m_manager->registerSchedulerLaneForTesting(request.lane());
    return m_manager->get(request);
}

QCNetworkReply *tst_QCNetworkScheduler::sendScheduledGet(const QUrl &url,
                                                         QCNetworkRequestPriority priority,
                                                         const QString &lane)
{
    QCNetworkRequest request(url);
    request.setPriority(priority);
    if (!lane.isEmpty()) {
        QCNetworkLaneKey laneKey;
        const bool laneOk = QCNetworkLaneKey::fromName(lane, &laneKey);
        Q_ASSERT(laneOk);
        request.setLane(laneKey);
    }
    m_mock.mockResponse(HttpMethod::Get, request.url(), QByteArrayLiteral("OK"));
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
    QVERIFY(!m_manager->isSchedulerEnabled());

    m_manager->enableRequestScheduler(true);
    QVERIFY(m_manager->isSchedulerEnabled());

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
    verifyCanonicalMetaTypeContract<QCNetworkLaneKey>("QCurl::QCNetworkLaneKey");
    verifyCanonicalMetaTypeContract<QCNetworkSchedulerPolicy>("QCurl::QCNetworkSchedulerPolicy");
    verifyCanonicalMetaTypeContract<QCNetworkSchedulerPolicy::LaneConfig>(
        "QCurl::QCNetworkSchedulerPolicy::LaneConfig");
    verifyCanonicalMetaTypeContract<QCNetworkSchedulerStatistics>(
        "QCurl::QCNetworkSchedulerStatistics");
    verifyCanonicalMetaTypeContract<QCNetworkLaneCancelResult>("QCurl::QCNetworkLaneCancelResult");

    QSignalSpy queueSpy(m_scheduler, &QCNetworkRequestScheduler::requestQueued);
    auto *reply = sendScheduledGet(QUrl(QStringLiteral("http://metatype.test/queue")),
                                   QCNetworkRequestPriority::High,
                                   QStringLiteral("MetaLane"));

    QTRY_VERIFY_WITH_TIMEOUT(queueSpy.count() >= 1, 2000);
    QCOMPARE(static_cast<int>(priorityFromArgs(queueSpy.at(0))),
             static_cast<int>(QCNetworkRequestPriority::High));

    QCOMPARE(m_scheduler->cancelRequest(reply), QCNetworkRequestScheduler::CommandResult::Applied);
    QTRY_VERIFY_WITH_TIMEOUT(reply->isFinished(), 2000);
    reply->deleteLater();

    // 再补一个真实 queued connection 回归，确保跨线程投递时不会退化成匿名 enum 类型。
    PriorityEmitter emitter;
    PriorityReceiver receiver;
    QObject::connect(&emitter,
                     &PriorityEmitter::priorityReady,
                     &receiver,
                     &PriorityReceiver::accept,
                     Qt::QueuedConnection);
    QSignalSpy queuedSpy(&receiver, &PriorityReceiver::received);

    std::thread emitterThread(
        [&emitter]() { emitter.emitPriority(QCNetworkRequestPriority::Critical); });
    emitterThread.join();

    QTRY_COMPARE_WITH_TIMEOUT(queuedSpy.count(), 1, 2000);
    QCOMPARE(static_cast<int>(qvariant_cast<QCNetworkRequestPriority>(queuedSpy.at(0).at(0))),
             static_cast<int>(QCNetworkRequestPriority::Critical));

    qDebug() << "QCNetworkRequestPriority metatype contract verified";
}

void tst_QCNetworkScheduler::testRequestStartedRequiresExecuteDispatch()
{
    static_cast<void>(m_scheduler->cancelAllRequests());
    m_mock.clear();

    const QCNetworkRequestScheduler::Config oldConfig = m_scheduler->configForTesting();
    QCNetworkRequestScheduler::Config config          = oldConfig;
    config.setMaxConcurrentRequests(1);
    config.setMaxRequestsPerHost(1);
    m_scheduler->setConfigForTesting(config);

    QObject guard;
    QSignalSpy queuedSpy(m_scheduler, &QCNetworkRequestScheduler::requestQueued);
    QSignalSpy cancelledSpy(m_scheduler, &QCNetworkRequestScheduler::requestCancelled);
    QSignalSpy aboutToStartSpy(m_scheduler, &QCNetworkRequestScheduler::requestAboutToStart);
    QSignalSpy startedSpy(m_scheduler, &QCNetworkRequestScheduler::requestStarted);
    QSignalSpy finishedSpy(m_scheduler, &QCNetworkRequestScheduler::requestFinished);
    // 在 requestQueued 的直连槽里直接 cancel，模拟 queued start handoff 尚未执行前 reply 就失效。
    QObject::connect(
        m_scheduler,
        &QCNetworkRequestScheduler::requestQueued,
        &guard,
        [](QCNetworkReply *reply, const QString &, const QString &, QCNetworkRequestPriority) {
            if (reply) {
                reply->cancel();
            }
        },
        Qt::DirectConnection);

    QPointer<QCNetworkReply> reply
        = sendScheduledGet(QUrl(QStringLiteral("http://started-contract.test/drop-before-execute")),
                           QCNetworkRequestPriority::Normal,
                           QStringLiteral("StartedContractLane"));

    QTRY_COMPARE_WITH_TIMEOUT(queuedSpy.count(), 1, 2000);
    QTRY_COMPARE_WITH_TIMEOUT(cancelledSpy.count(), 1, 2000);
    QTRY_VERIFY_WITH_TIMEOUT(reply->isFinished(), 2000);
    QCOMPARE(aboutToStartSpy.count(), 0);
    QCOMPARE(startedSpy.count(), 0);
    QCOMPARE(finishedSpy.count(), 0);

    reply->deleteLater();

    m_scheduler->setConfigForTesting(oldConfig);

    qDebug() << "requestAboutToStart/requestStarted are now gated by execute dispatch";
}

void tst_QCNetworkScheduler::testCancelAfterStartQueuedDoesNotStart()
{
    static_cast<void>(m_scheduler->cancelAllRequests());
    m_mock.clear();

    const QCNetworkRequestScheduler::Config oldConfig = m_scheduler->configForTesting();
    QCNetworkRequestScheduler::Config config          = oldConfig;
    config.setMaxConcurrentRequests(1);
    config.setMaxRequestsPerHost(1);
    m_scheduler->setConfigForTesting(config);

    QSignalSpy cancelledSpy(m_scheduler, &QCNetworkRequestScheduler::requestCancelled);
    QSignalSpy aboutToStartSpy(m_scheduler, &QCNetworkRequestScheduler::requestAboutToStart);
    QSignalSpy startedSpy(m_scheduler, &QCNetworkRequestScheduler::requestStarted);

    QPointer<QCNetworkReply> reply
        = sendScheduledGet(QUrl(QStringLiteral("http://cancel-after-queue.test/window1")),
                           QCNetworkRequestPriority::Normal,
                           QStringLiteral("CancelWindow1Lane"));

    // start lambda 已 queued，但尚未执行；在事件循环处理 queued handoff 前显式 cancel。
    QCOMPARE(m_scheduler->cancelRequest(reply.data()),
             QCNetworkRequestScheduler::CommandResult::Applied);

    QTRY_COMPARE_WITH_TIMEOUT(cancelledSpy.count(), 1, 2000);
    QTRY_VERIFY_WITH_TIMEOUT(reply && reply->isFinished(), 2000);

    // 显式取消在 scheduler 侧生效后，不得再 aboutToStart/started/execute。
    QCOMPARE(aboutToStartSpy.count(), 0);
    QCOMPARE(startedSpy.count(), 0);

    if (reply) {
        reply->deleteLater();
    }
    m_scheduler->setConfigForTesting(oldConfig);

    qDebug() << "Cancel-after-queue-before-execute contract verified";
}

void tst_QCNetworkScheduler::testCancelFromAboutToStartSlotPreventsExecute()
{
    static_cast<void>(m_scheduler->cancelAllRequests());
    m_mock.clear();

    const QCNetworkRequestScheduler::Config oldConfig = m_scheduler->configForTesting();
    QCNetworkRequestScheduler::Config config          = oldConfig;
    config.setMaxConcurrentRequests(1);
    config.setMaxRequestsPerHost(1);
    m_scheduler->setConfigForTesting(config);

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
            QCOMPARE(m_scheduler->cancelRequest(reply),
                     QCNetworkRequestScheduler::CommandResult::Applied);
        },
        Qt::DirectConnection);

    QPointer<QCNetworkReply> reply
        = sendScheduledGet(QUrl(QStringLiteral("http://cancel-in-about-to-start.test/window2")),
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
    m_scheduler->setConfigForTesting(oldConfig);

    qDebug() << "Cancel-in-requestAboutToStart slot contract verified";
}

void tst_QCNetworkScheduler::testRequestQueuedReplyDeletionStopsContinuation()
{
    static_cast<void>(m_scheduler->cancelAllRequests());
    m_mock.clear();

    const QCNetworkRequestScheduler::Config oldConfig = m_scheduler->configForTesting();
    QCNetworkRequestScheduler::Config config          = oldConfig;
    config.setMaxConcurrentRequests(0);
    config.setMaxRequestsPerHost(1);
    m_scheduler->setConfigForTesting(config);

    QPointer<QCNetworkReply> reply
        = sendScheduledGet(QUrl(
                               QStringLiteral("http://request-queued-reply-lifetime.test/pending")),
                           QCNetworkRequestPriority::Low,
                           QStringLiteral("ReplyLifetimeLane"));
    QVERIFY(reply);

    QObject slotContext;
    QSignalSpy emptySpy(m_scheduler, &QCNetworkRequestScheduler::queueEmpty);
    bool replyDeleted = false;
    QObject::connect(
        m_scheduler,
        &QCNetworkRequestScheduler::requestQueued,
        &slotContext,
        [&replyDeleted](QCNetworkReply *queuedReply,
                        const QString &,
                        const QString &,
                        QCNetworkRequestPriority) {
            if (replyDeleted || !queuedReply) {
                return;
            }
            replyDeleted = true;
            delete static_cast<QObject *>(queuedReply);
        },
        Qt::DirectConnection);

    QCOMPARE(m_scheduler->changePriority(reply.data(), QCNetworkRequestPriority::High),
             QCNetworkRequestScheduler::CommandResult::Applied);

    QVERIFY(replyDeleted);
    QVERIFY(reply.isNull());
    QCOMPARE(emptySpy.count(), 0);

    m_scheduler->setConfigForTesting(oldConfig);
}

void tst_QCNetworkScheduler::testRequestQueuedSchedulerDeletionStopsContinuation()
{
    QCNetworkMockHandler mock;
    QObject owner;
    auto *manager = new QCNetworkAccessManager(&owner);
    mock.setCaptureEnabled(false);
    mock.setGlobalDelay(0);
    QCurl::TestSupport::setMockHandler(*manager, &mock);
    manager->enableRequestScheduler(true);

    auto *scheduler = manager->schedulerForTesting();
    QVERIFY(scheduler);

    QCNetworkRequestScheduler::Config config;
    config.setMaxConcurrentRequests(0);
    config.setMaxRequestsPerHost(1);
    config.setMaxBandwidthBytesPerSec(0);
    config.setEnableThrottling(true);
    scheduler->setConfigForTesting(config);

    const QUrl url(QStringLiteral("http://request-queued-scheduler-lifetime.test/pending"));
    mock.mockResponse(HttpMethod::Get, url, QByteArrayLiteral("OK"));
    QPointer<QCNetworkReply> reply = manager->get(QCNetworkRequest(url));
    QVERIFY(reply);

    QObject slotContext;
    QPointer<QCNetworkRequestScheduler> schedulerGuard(scheduler);
    QSignalSpy cancelledSpy(scheduler, &QCNetworkRequestScheduler::requestCancelled);
    QSignalSpy emptySpy(scheduler, &QCNetworkRequestScheduler::queueEmpty);
    bool schedulerDeleted = false;
    QObject::connect(
        scheduler,
        &QCNetworkRequestScheduler::requestQueued,
        &slotContext,
        [scheduler, &schedulerDeleted](QCNetworkReply *,
                                       const QString &,
                                       const QString &,
                                       QCNetworkRequestPriority) {
            if (schedulerDeleted) {
                return;
            }
            schedulerDeleted = true;
            delete static_cast<QObject *>(scheduler);
        },
        Qt::DirectConnection);

    QCOMPARE(scheduler->changePriority(reply.data(), QCNetworkRequestPriority::High),
             QCNetworkRequestScheduler::CommandResult::Applied);

    QVERIFY(schedulerDeleted);
    QVERIFY(schedulerGuard.isNull());
    QCOMPARE(cancelledSpy.count(), 0);
    QCOMPARE(emptySpy.count(), 0);
}

/// @brief 验证 undeferRequest() 在 requestQueued 直连槽删除 Scheduler 后立即停止。
void tst_QCNetworkScheduler::testUndeferRequestQueuedSchedulerDeletionStopsContinuation()
{
    QCNetworkMockHandler mock;
    QObject owner;
    auto *manager = new QCNetworkAccessManager(&owner);
    mock.setCaptureEnabled(false);
    mock.setGlobalDelay(0);
    QCurl::TestSupport::setMockHandler(*manager, &mock);
    manager->enableRequestScheduler(true);

    auto *scheduler = manager->schedulerForTesting();
    QVERIFY(scheduler);

    QCNetworkRequestScheduler::Config config;
    config.setMaxConcurrentRequests(0);
    config.setMaxRequestsPerHost(1);
    scheduler->setConfigForTesting(config);

    const QUrl url(QStringLiteral("http://undefer-scheduler-lifetime.test/pending"));
    mock.mockResponse(HttpMethod::Get, url, QByteArrayLiteral("OK"));
    QPointer<QCNetworkReply> reply = manager->get(QCNetworkRequest(url));
    QVERIFY(reply);
    QCOMPARE(scheduler->deferPendingRequest(reply.data()),
             QCNetworkRequestScheduler::CommandResult::Applied);

    QPointer<QCNetworkRequestScheduler> schedulerGuard(scheduler);
    QObject slotContext;
    QObject::connect(
        scheduler,
        &QCNetworkRequestScheduler::requestQueued,
        &slotContext,
        [scheduler](QCNetworkReply *, const QString &, const QString &, QCNetworkRequestPriority) {
            delete static_cast<QObject *>(scheduler);
        },
        Qt::DirectConnection);

    QCOMPARE(scheduler->undeferRequest(reply.data()),
             QCNetworkRequestScheduler::CommandResult::Applied);

    QVERIFY(schedulerGuard.isNull());
}

/// @brief 验证 cancelRequest() 在 requestCancelled 直连槽删除 Scheduler 后立即停止。
void tst_QCNetworkScheduler::testCancelRequestCancelledSchedulerDeletionStopsContinuation()
{
    QCNetworkMockHandler mock;
    QObject owner;
    auto *manager = new QCNetworkAccessManager(&owner);
    mock.setCaptureEnabled(false);
    mock.setGlobalDelay(200);
    QCurl::TestSupport::setMockHandler(*manager, &mock);
    manager->enableRequestScheduler(true);

    auto *scheduler = manager->schedulerForTesting();
    QVERIFY(scheduler);

    QCNetworkRequestScheduler::Config config;
    config.setMaxConcurrentRequests(1);
    config.setMaxRequestsPerHost(1);
    scheduler->setConfigForTesting(config);

    const QUrl runningUrl(QStringLiteral("http://cancel-scheduler-lifetime.test/running"));
    const QUrl waitingUrl(QStringLiteral("http://cancel-scheduler-lifetime.test/waiting"));
    mock.mockResponse(HttpMethod::Get, runningUrl, QByteArrayLiteral("OK"));
    mock.mockResponse(HttpMethod::Get, waitingUrl, QByteArrayLiteral("OK"));
    QSignalSpy startedSpy(scheduler, &QCNetworkRequestScheduler::requestStarted);
    QPointer<QCNetworkReply> runningReply = manager->get(QCNetworkRequest(runningUrl));
    QPointer<QCNetworkReply> waitingReply = manager->get(QCNetworkRequest(waitingUrl));
    QVERIFY(runningReply);
    QVERIFY(waitingReply);
    QTRY_COMPARE_WITH_TIMEOUT(startedSpy.count(), 1, 2000);

    QPointer<QCNetworkRequestScheduler> schedulerGuard(scheduler);
    QObject slotContext;
    QObject::connect(
        scheduler,
        &QCNetworkRequestScheduler::requestCancelled,
        &slotContext,
        [scheduler](QCNetworkReply *, const QString &, const QString &) {
            delete static_cast<QObject *>(scheduler);
        },
        Qt::DirectConnection);

    QCOMPARE(scheduler->cancelRequest(runningReply.data()),
             QCNetworkRequestScheduler::CommandResult::Applied);

    QVERIFY(schedulerGuard.isNull());
}

/// @brief 验证完成归并路径在 requestCancelled 直连槽删除 Scheduler 后立即停止。
void tst_QCNetworkScheduler::testFinalizeCancelledSchedulerDeletionStopsContinuation()
{
    QCNetworkMockHandler mock;
    QObject owner;
    auto *manager = new QCNetworkAccessManager(&owner);
    mock.setCaptureEnabled(false);
    mock.setGlobalDelay(0);
    QCurl::TestSupport::setMockHandler(*manager, &mock);
    manager->enableRequestScheduler(true);

    auto *scheduler = manager->schedulerForTesting();
    QVERIFY(scheduler);

    QCNetworkRequestScheduler::Config config;
    config.setMaxConcurrentRequests(0);
    config.setMaxRequestsPerHost(1);
    scheduler->setConfigForTesting(config);

    const QUrl url(QStringLiteral("http://finalize-scheduler-lifetime.test/pending"));
    mock.mockResponse(HttpMethod::Get, url, QByteArrayLiteral("OK"));
    QPointer<QCNetworkReply> reply = manager->get(QCNetworkRequest(url));
    QVERIFY(reply);

    QPointer<QCNetworkRequestScheduler> schedulerGuard(scheduler);
    QObject slotContext;
    QObject::connect(
        scheduler,
        &QCNetworkRequestScheduler::requestCancelled,
        &slotContext,
        [scheduler](QCNetworkReply *, const QString &, const QString &) {
            delete static_cast<QObject *>(scheduler);
        },
        Qt::DirectConnection);

    reply->cancel();

    QTRY_VERIFY_WITH_TIMEOUT(schedulerGuard.isNull(), 2000);
}

void tst_QCNetworkScheduler::testManagerOwnedSchedulerForTestingOwnerThreadContract()
{
    const quintptr ownerThreadScheduler = reinterpret_cast<quintptr>(
        m_manager->schedulerForTesting());
    QVERIFY(ownerThreadScheduler != 0);

    quintptr workerThreadScheduler = 0;
    std::thread worker([&workerThreadScheduler]() {
        workerThreadScheduler = reinterpret_cast<quintptr>(
            QCNetworkRequestScheduler::instanceForTesting());
    });
    worker.join();

    QVERIFY(workerThreadScheduler != 0);
    QVERIFY(workerThreadScheduler != ownerThreadScheduler);

    qDebug() << "Manager-owned scheduler test hook owner-thread contract verified";
}

void tst_QCNetworkScheduler::testSchedulerConstructionDoesNotStartThrottleTimer()
{
    bool timerActive = true;
    std::thread worker([&timerActive]() {
        const auto *timer = QCNetworkRequestScheduler::instanceForTesting()->findChild<QTimer *>();
        timerActive       = timer && timer->isActive();
    });
    worker.join();

    QCOMPARE(timerActive, false);

    qDebug() << "Scheduler constructor keeps throttle timer stopped until owner-thread lifecycle "
                "starts it";
}

void tst_QCNetworkScheduler::testManagerLevelSchedulerPolicyConfiguresAdmission()
{
    static_cast<void>(m_scheduler->cancelAllRequests());
    m_mock.clear();

    QCNetworkSchedulerPolicy policy = QCNetworkSchedulerPolicy::defaultPolicy();
    policy.setMaxConcurrentRequests(1);
    policy.setMaxRequestsPerHost(1);

    QCNetworkSchedulerPolicy::LaneConfig control;
    control.setWeight(3);
    control.setQuantum(2);
    control.setReservedGlobal(1);
    QVERIFY(policy.setLaneConfig(QCNetworkLaneKey::control(), control));

    QString error;
    QVERIFY(m_manager->setSchedulerPolicy(policy, &error));
    QCOMPARE(error, QString());
    QCOMPARE(m_manager->schedulerPolicy().maxConcurrentRequests(), 1);
    QCNetworkSchedulerPolicy::LaneConfig appliedControlConfig;
    QVERIFY(m_manager->schedulerPolicy().laneConfig(QCNetworkLaneKey::control(),
                                                    &appliedControlConfig));
    QCOMPARE(appliedControlConfig.weight(), 3);

    const auto schedulerConfig = m_scheduler->configForTesting();
    QCOMPARE(schedulerConfig.maxConcurrentRequests(), 1);
    QCOMPARE(schedulerConfig.maxRequestsPerHost(), 1);
    QCOMPARE(m_scheduler->laneConfigForTesting(QStringLiteral("Control")).weight(), 3);
    QCOMPARE(m_scheduler->laneConfigForTesting(QStringLiteral("Control")).quantum(), 2);

    QCNetworkRequest first(QUrl(QStringLiteral("http://manager-policy.test/first")));
    QCNetworkRequest second(QUrl(QStringLiteral("http://manager-policy.test/second")));
    first.setLane(QCNetworkLaneKey::control());
    second.setLane(QCNetworkLaneKey::control());
    m_mock.mockResponse(HttpMethod::Get, first.url(), QByteArrayLiteral("OK"));
    m_mock.mockResponse(HttpMethod::Get, second.url(), QByteArrayLiteral("OK"));

    QSignalSpy queuedSpy(m_scheduler, &QCNetworkRequestScheduler::requestQueued);
    QSignalSpy startedSpy(m_scheduler, &QCNetworkRequestScheduler::requestStarted);
    auto *firstReply  = sendScheduledGet(first);
    auto *secondReply = sendScheduledGet(second);

    QTRY_COMPARE_WITH_TIMEOUT(queuedSpy.count(), 2, 2000);
    QTRY_VERIFY_WITH_TIMEOUT(startedSpy.count() >= 1, 2000);
    QCOMPARE(m_manager->schedulerStatistics().pendingRequests(), 1);
    QCOMPARE(m_manager->schedulerStatistics().runningRequests(), 1);

    const QCNetworkLaneCancelResult cancelResult = m_manager->cancelLaneRequests(
        QCNetworkLaneKey::control(),
        QCNetworkAccessManager::SchedulerCancelScope::PendingAndRunning);
    QVERIFY(cancelResult.isSuccess());
    QCOMPARE(cancelResult.cancelledRequests(), 2);
    QTRY_VERIFY_WITH_TIMEOUT(firstReply->isFinished(), 2000);
    QTRY_VERIFY_WITH_TIMEOUT(secondReply->isFinished(), 2000);
    firstReply->deleteLater();
    secondReply->deleteLater();

    QVERIFY(m_manager->setSchedulerPolicy(QCNetworkSchedulerPolicy::defaultPolicy(), &error));
}

void tst_QCNetworkScheduler::testUnknownLaneFailsClosed()
{
    static_cast<void>(m_scheduler->cancelAllRequests());
    m_mock.clear();

    QVERIFY(m_manager->setSchedulerPolicy(QCNetworkSchedulerPolicy::defaultPolicy()));
    m_manager->enableRequestScheduler(true);

    QCNetworkRequest request(QUrl(QStringLiteral("http://unknown-lane.test/fail-closed")));
    QCNetworkLaneKey unregisteredLane;
    QVERIFY(QCNetworkLaneKey::fromName(QStringLiteral("UnregisteredLane"), &unregisteredLane));
    request.setLane(unregisteredLane);
    m_mock.mockResponse(HttpMethod::Get, request.url(), QByteArrayLiteral("OK"));

    auto *reply = m_manager->get(request);

    QTRY_VERIFY_WITH_TIMEOUT(reply->isFinished(), 2000);
    QCOMPARE(reply->error(), NetworkError::InvalidRequest);
    QVERIFY(reply->errorString().contains(QStringLiteral("UnregisteredLane")));
    QCOMPARE(m_manager->schedulerStatistics().pendingRequests(), 0);
    QCOMPARE(m_manager->schedulerStatistics().runningRequests(), 0);

    reply->deleteLater();
}

void tst_QCNetworkScheduler::testManagerCancelLaneRequestsReturnsStructuredFailClosedResult()
{
    static_cast<void>(m_scheduler->cancelAllRequests());
    m_mock.clear();

    QCNetworkRequestScheduler::Config config = m_scheduler->configForTesting();
    config.setMaxConcurrentRequests(0);
    config.setMaxRequestsPerHost(10);
    m_scheduler->setConfigForTesting(config);
    m_manager->enableRequestScheduler(true);

    auto *defaultReply = sendScheduledGet(QUrl(QStringLiteral(
                                              "http://manager-cancel-fail-closed.test/default")),
                                          QCNetworkRequestPriority::Normal);
    QTRY_COMPARE_WITH_TIMEOUT(m_manager->schedulerStatistics().pendingRequests(), 1, 2000);

    const QCNetworkLaneCancelResult invalidLaneResult = m_manager->cancelLaneRequests(
        QCNetworkLaneKey(), QCNetworkAccessManager::SchedulerCancelScope::PendingAndRunning);
    QCOMPARE(invalidLaneResult.status(), QCNetworkLaneCancelResult::Status::InvalidLane);
    QCOMPARE(invalidLaneResult.cancelledRequests(), 0);
    QVERIFY(!invalidLaneResult.isSuccess());
    QVERIFY(!defaultReply->isFinished());
    QCOMPARE(m_manager->schedulerStatistics().pendingRequests(), 1);

    QCNetworkLaneKey unregisteredLane;
    QVERIFY(QCNetworkLaneKey::fromName(QStringLiteral("UnregisteredCancelLane"), &unregisteredLane));
    const QCNetworkLaneCancelResult unregisteredResult = m_manager->cancelLaneRequests(
        unregisteredLane, QCNetworkAccessManager::SchedulerCancelScope::PendingAndRunning);
    QCOMPARE(unregisteredResult.status(), QCNetworkLaneCancelResult::Status::UnregisteredLane);
    QCOMPARE(unregisteredResult.cancelledRequests(), 0);
    QVERIFY(!defaultReply->isFinished());
    QCOMPARE(m_manager->schedulerStatistics().pendingRequests(), 1);

    const QCNetworkLaneCancelResult noMatchResult
        = m_manager->cancelLaneRequests(QCNetworkLaneKey::control(),
                                        QCNetworkAccessManager::SchedulerCancelScope::PendingOnly);
    QVERIFY(noMatchResult.isSuccess());
    QCOMPARE(noMatchResult.status(), QCNetworkLaneCancelResult::Status::Success);
    QCOMPARE(noMatchResult.cancelledRequests(), 0);
    QVERIFY(!defaultReply->isFinished());

    m_manager->enableRequestScheduler(false);
    const QCNetworkLaneCancelResult disabledResult
        = m_manager->cancelLaneRequests(QCNetworkLaneKey::control(),
                                        QCNetworkAccessManager::SchedulerCancelScope::PendingOnly);
    QCOMPARE(disabledResult.status(), QCNetworkLaneCancelResult::Status::SchedulerDisabled);
    QCOMPARE(disabledResult.cancelledRequests(), 0);
    QVERIFY(!disabledResult.isSuccess());
    m_manager->enableRequestScheduler(true);

    static_cast<void>(m_scheduler->cancelAllRequests());
    QTRY_VERIFY_WITH_TIMEOUT(defaultReply->isFinished(), 2000);
    defaultReply->deleteLater();

    qDebug() << "Manager lane cancel structured fail-closed result verified";
}

/**
 * @brief 验证 pending 队列按优先级出队，且不抢占已 running 请求。
 */
void tst_QCNetworkScheduler::testPriorityQueueOrdering()
{
    static_cast<void>(m_scheduler->cancelAllRequests());

    // 固化测试契约：优先级为“非抢占”
    // - 已 Running 的请求不会被更高优先级请求中断
    // - 优先级只影响 pending 出队顺序
    QCNetworkRequestScheduler::Config config = m_scheduler->configForTesting();
    config.setMaxConcurrentRequests(1);
    config.setMaxRequestsPerHost(10);
    m_scheduler->setConfigForTesting(config);

    m_mock.clear();

    QCNetworkRequest lowReq(QUrl(QStringLiteral("http://prio-low.test/test")));
    QCNetworkRequest normalReq(QUrl(QStringLiteral("http://prio-normal.test/test")));
    QCNetworkRequest highReq(QUrl(QStringLiteral("http://prio-high.test/test")));

    lowReq.setPriority(QCNetworkRequestPriority::Low);
    normalReq.setPriority(QCNetworkRequestPriority::Normal);
    highReq.setPriority(QCNetworkRequestPriority::High);

    m_mock.mockResponse(HttpMethod::Get, lowReq.url(), QByteArrayLiteral("OK"));
    m_mock.mockResponse(HttpMethod::Get, normalReq.url(), QByteArrayLiteral("OK"));
    m_mock.mockResponse(HttpMethod::Get, highReq.url(), QByteArrayLiteral("OK"));

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

    static_cast<void>(m_scheduler->cancelAllRequests());
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
    static_cast<void>(m_scheduler->cancelAllRequests());
    m_mock.clear();

    QCNetworkRequestScheduler::Config config = m_scheduler->configForTesting();
    config.setMaxConcurrentRequests(2);
    config.setMaxRequestsPerHost(10);
    m_scheduler->setConfigForTesting(config);

    QList<QCNetworkReply *> replies;
    for (int i = 0; i < 5; ++i) {
        QCNetworkRequest req(QUrl(QStringLiteral("http://concurrent-%1.test/test").arg(i)));
        req.setPriority(QCNetworkRequestPriority::Normal);
        m_mock.mockResponse(HttpMethod::Get, req.url(), QByteArrayLiteral("OK"));
        auto *reply = sendScheduledGet(req);
        replies.append(reply);
    }

    auto stats = m_scheduler->statistics();

    // 离线门禁：调度层统计应稳定可重复（不依赖真实网络时序）
    QCOMPARE(stats.runningRequests(), 2);
    QCOMPARE(stats.pendingRequests(), 3);

    qDebug() << "Running:" << stats.runningRequests() << "Pending:" << stats.pendingRequests();

    static_cast<void>(m_scheduler->cancelAllRequests());
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
    static_cast<void>(m_scheduler->cancelAllRequests());
    m_mock.clear();

    QCNetworkRequestScheduler::Config config = m_scheduler->configForTesting();
    config.setMaxConcurrentRequests(10);
    config.setMaxRequestsPerHost(1);
    m_scheduler->setConfigForTesting(config);

    QList<QCNetworkReply *> replies;
    for (int i = 0; i < 3; ++i) {
        QCNetworkRequest req(QUrl(QStringLiteral("http://same-host.test/test?i=%1").arg(i)));
        req.setPriority(QCNetworkRequestPriority::Normal);
        m_mock.mockResponse(HttpMethod::Get, req.url(), QByteArrayLiteral("OK"));
        auto *reply = sendScheduledGet(req);
        replies.append(reply);
    }

    auto running = m_scheduler->runningRequests();

    QVERIFY(running.size() <= 1);

    qDebug() << "Running requests to same host:" << running.size();

    static_cast<void>(m_scheduler->cancelAllRequests());
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
    static_cast<void>(m_scheduler->cancelAllRequests());
    m_mock.clear();

    // 让请求保持在 Pending，避免被立即启动（本测试验证“调度层 defer/undefer”合同）。
    const QCNetworkRequestScheduler::Config oldConfig = m_scheduler->configForTesting();
    QCNetworkRequestScheduler::Config config          = oldConfig;
    config.setMaxConcurrentRequests(0);
    config.setMaxRequestsPerHost(10);
    m_scheduler->setConfigForTesting(config);

    QSignalSpy queueSpy(m_scheduler, &QCNetworkRequestScheduler::requestQueued);

    QCNetworkRequest req(QUrl(QStringLiteral("http://defer.test/test")));
    req.setPriority(QCNetworkRequestPriority::High);
    m_mock.mockResponse(HttpMethod::Get, req.url(), QByteArrayLiteral("OK"));
    auto *reply = sendScheduledGet(req);

    auto statsBefore  = m_scheduler->statistics();
    int pendingBefore = statsBefore.pendingRequests() + statsBefore.runningRequests();

    // 延后请求（调度层语义，非传输级 pause）
    QCOMPARE(m_scheduler->deferPendingRequest(reply),
             QCNetworkRequestScheduler::CommandResult::Applied);

    QCOMPARE(m_scheduler->undeferRequest(reply), QCNetworkRequestScheduler::CommandResult::Applied);
    QTRY_VERIFY_WITH_TIMEOUT(queueSpy.count() >= 2, 2000);
    QCOMPARE(static_cast<int>(priorityFromArgs(queueSpy.back())),
             static_cast<int>(QCNetworkRequestPriority::High));

    auto statsAfter  = m_scheduler->statistics();
    int pendingAfter = statsAfter.pendingRequests() + statsAfter.runningRequests();

    QVERIFY(pendingAfter >= 1);

    qDebug() << "Pending before:" << pendingBefore << "after:" << pendingAfter;

    QCOMPARE(m_scheduler->cancelRequest(reply), QCNetworkRequestScheduler::CommandResult::Applied);
    reply->deleteLater();

    m_scheduler->setConfigForTesting(oldConfig);

    qDebug() << "Defer/undefer scheduling contract verified";
}

/**
 * @brief 验证 deferPendingRequest() 仅对 Pending 生效；对 Running 返回 InvalidState 且不影响执行。
 */
void tst_QCNetworkScheduler::testDeferPendingRequestRejectsRunning()
{
    static_cast<void>(m_scheduler->cancelAllRequests());
    m_mock.clear();

    const QCNetworkRequestScheduler::Config oldConfig = m_scheduler->configForTesting();
    QCNetworkRequestScheduler::Config config          = oldConfig;
    config.setMaxConcurrentRequests(1);
    config.setMaxRequestsPerHost(10);
    m_scheduler->setConfigForTesting(config);

    m_mock.setGlobalDelay(200);

    QSignalSpy startedSpy(m_scheduler, &QCNetworkRequestScheduler::requestStarted);

    auto *reply = sendScheduledGet(QUrl(QStringLiteral("http://defer-running.test/run")),
                                   QCNetworkRequestPriority::Normal,
                                   QStringLiteral("DeferTestLane"));

    QTRY_COMPARE_WITH_TIMEOUT(startedSpy.count(), 1, 2000);
    QVERIFY(m_scheduler->runningRequests().contains(reply));

    // Running 不允许 defer：必须返回 false，且不应把 reply 从 running 中移走或触发 cancel。
    QCOMPARE(m_scheduler->deferPendingRequest(reply),
             QCNetworkRequestScheduler::CommandResult::InvalidState);
    QVERIFY(m_scheduler->runningRequests().contains(reply));

    QTRY_VERIFY_WITH_TIMEOUT(reply->isFinished(), 2000);
    reply->deleteLater();

    m_mock.setGlobalDelay(0);
    m_scheduler->setConfigForTesting(oldConfig);

    qDebug() << "deferPendingRequest() rejects Running state as expected";
}

/**
 * @brief 验证外部直接 reply->cancel() 也会触发 scheduler 收敛，不遗留脏项。
 */
void tst_QCNetworkScheduler::testDirectCancelContract()
{
    static_cast<void>(m_scheduler->cancelAllRequests());
    m_mock.clear();

    {
        const QCNetworkRequestScheduler::Config oldConfig = m_scheduler->configForTesting();
        QCNetworkRequestScheduler::Config config          = oldConfig;
        config.setMaxConcurrentRequests(0);
        config.setMaxRequestsPerHost(10);
        m_scheduler->setConfigForTesting(config);

        QSignalSpy cancelSpy(m_scheduler, &QCNetworkRequestScheduler::requestCancelled);
        QSignalSpy startedSpy(m_scheduler, &QCNetworkRequestScheduler::requestStarted);
        QSignalSpy finishedSpy(m_scheduler, &QCNetworkRequestScheduler::requestFinished);

        auto *pendingReply  = sendScheduledGet(QUrl(QStringLiteral(
                                                   "http://direct-cancel-pending.test/one")),
                                               QCNetworkRequestPriority::Normal,
                                               QStringLiteral("DirectCancelLane"));
        auto *deferredReply = sendScheduledGet(QUrl(QStringLiteral(
                                                   "http://direct-cancel-deferred.test/two")),
                                               QCNetworkRequestPriority::High,
                                               QStringLiteral("DirectCancelLane"));
        QCOMPARE(m_scheduler->deferPendingRequest(deferredReply),
                 QCNetworkRequestScheduler::CommandResult::Applied);

        pendingReply->cancel();
        deferredReply->cancel();

        QTRY_COMPARE_WITH_TIMEOUT(cancelSpy.count(), 2, 2000);
        QCOMPARE(startedSpy.count(), 0);
        QCOMPARE(finishedSpy.count(), 0);
        QCOMPARE(m_scheduler->statistics().pendingRequests(), 0);
        QCOMPARE(m_scheduler->statistics().runningRequests(), 0);

        QTRY_VERIFY_WITH_TIMEOUT(pendingReply->isFinished(), 2000);
        QTRY_VERIFY_WITH_TIMEOUT(deferredReply->isFinished(), 2000);
        pendingReply->deleteLater();
        deferredReply->deleteLater();

        m_scheduler->setConfigForTesting(oldConfig);
    }

    static_cast<void>(m_scheduler->cancelAllRequests());
    m_mock.clear();
    m_mock.setGlobalDelay(200);

    {
        const QCNetworkRequestScheduler::Config oldConfig = m_scheduler->configForTesting();
        QCNetworkRequestScheduler::Config config          = oldConfig;
        config.setMaxConcurrentRequests(1);
        config.setMaxRequestsPerHost(10);
        m_scheduler->setConfigForTesting(config);

        QSignalSpy cancelSpy(m_scheduler, &QCNetworkRequestScheduler::requestCancelled);
        QSignalSpy startedSpy(m_scheduler, &QCNetworkRequestScheduler::requestStarted);
        QSignalSpy finishedSpy(m_scheduler, &QCNetworkRequestScheduler::requestFinished);

        auto *runningReply = sendScheduledGet(QUrl(QStringLiteral(
                                                  "http://direct-cancel-running.test/run")),
                                              QCNetworkRequestPriority::Normal,
                                              QStringLiteral("DirectCancelLane"));
        auto *waitingReply = sendScheduledGet(QUrl(QStringLiteral(
                                                  "http://direct-cancel-running.test/wait")),
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
        m_scheduler->setConfigForTesting(oldConfig);
    }

    m_mock.setGlobalDelay(0);

    qDebug() << "External cancel contract verified for pending/deferred/running replies";
}

/**
 * @brief 验证取消单个请求会发出取消信号并推动 reply 结束。
 */
void tst_QCNetworkScheduler::testCancelRequest()
{
    static_cast<void>(m_scheduler->cancelAllRequests());
    m_mock.clear();

    QCNetworkRequest req(QUrl(QStringLiteral("http://cancel-one.test/test")));
    req.setPriority(QCNetworkRequestPriority::Normal);
    m_mock.mockResponse(HttpMethod::Get, req.url(), QByteArrayLiteral("OK"));
    auto *reply = sendScheduledGet(req);

    QSignalSpy cancelSpy(m_scheduler, &QCNetworkRequestScheduler::requestCancelled);

    QCOMPARE(m_scheduler->cancelRequest(reply), QCNetworkRequestScheduler::CommandResult::Applied);

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
    static_cast<void>(m_scheduler->cancelAllRequests());
    m_mock.clear();

    QCNetworkRequestScheduler::Config config = m_scheduler->configForTesting();
    config.setMaxConcurrentRequests(2);
    config.setMaxRequestsPerHost(10);
    m_scheduler->setConfigForTesting(config);

    QList<QCNetworkReply *> replies;
    for (int i = 0; i < 5; ++i) {
        QCNetworkRequest req(QUrl(QStringLiteral("http://cancel-all-%1.test/test").arg(i)));
        req.setPriority(QCNetworkRequestPriority::Normal);
        m_mock.mockResponse(HttpMethod::Get, req.url(), QByteArrayLiteral("OK"));
        auto *reply = sendScheduledGet(req);
        replies.append(reply);
    }

    auto statsBefore = m_scheduler->statistics();
    int totalBefore  = statsBefore.pendingRequests() + statsBefore.runningRequests();

    QVERIFY(totalBefore >= 5);

    static_cast<void>(m_scheduler->cancelAllRequests());

    auto statsAfter = m_scheduler->statistics();
    int totalAfter  = statsAfter.pendingRequests() + statsAfter.runningRequests();

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
    static_cast<void>(m_scheduler->cancelAllRequests());
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

    const QCNetworkRequestScheduler::Config oldConfig = m_scheduler->configForTesting();
    QCNetworkRequestScheduler::Config config          = oldConfig;
    config.setMaxConcurrentRequests(1);
    config.setMaxRequestsPerHost(10);
    config.setMaxBandwidthBytesPerSec(40);
    config.setEnableThrottling(true);
    m_scheduler->setConfigForTesting(config);

    const QByteArray payload("ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789");
    QVERIFY(payload.size() < config.maxBandwidthBytesPerSec());

    QCNetworkRequest firstReq(QUrl(QStringLiteral("http://bandwidth-delta.test/one")));
    firstReq.setPriority(QCNetworkRequestPriority::Normal);
    QCNetworkRequest secondReq(QUrl(QStringLiteral("http://bandwidth-delta.test/two")));
    secondReq.setPriority(QCNetworkRequestPriority::Normal);
    m_mock.mockResponse(HttpMethod::Get, firstReq.url(), payload);
    m_mock.mockResponse(HttpMethod::Get, secondReq.url(), payload);

    QSignalSpy startedSpy(m_scheduler, &QCNetworkRequestScheduler::requestStarted);
    QSignalSpy throttledSpy(m_scheduler, &QCNetworkRequestScheduler::bandwidthThrottled);

    auto *firstReply  = sendScheduledGet(firstReq);
    auto *secondReply = sendScheduledGet(secondReq);

    QTRY_COMPARE_WITH_TIMEOUT(startedSpy.count(), 1, 2000);
    QTRY_COMPARE_WITH_TIMEOUT(startedSpy.count(), 2, 700);
    QCOMPARE(qvariant_cast<QCNetworkReply *>(startedSpy.at(1).at(0)), secondReply);
    QCOMPARE(throttledSpy.count(), 0);

    QTRY_VERIFY_WITH_TIMEOUT(firstReply->isFinished(), 2000);
    QTRY_VERIFY_WITH_TIMEOUT(secondReply->isFinished(), 2000);

    firstReply->deleteLater();
    secondReply->deleteLater();
    m_scheduler->setConfigForTesting(oldConfig);

    qDebug() << "Bandwidth window delta counting verified";
}

/**
 * @brief 验证 pending 请求调整优先级时不会破坏队列状态。
 */
void tst_QCNetworkScheduler::testChangePriority()
{
    static_cast<void>(m_scheduler->cancelAllRequests());
    m_mock.clear();

    // 使请求保持在 pending，避免“已 Running 无法调整优先级”的不确定性
    QCNetworkRequestScheduler::Config config = m_scheduler->configForTesting();
    config.setMaxConcurrentRequests(0);
    config.setMaxRequestsPerHost(10);
    m_scheduler->setConfigForTesting(config);

    QCNetworkRequest req(QUrl(QStringLiteral("http://priority-change.test/test")));
    req.setPriority(QCNetworkRequestPriority::Low);
    m_mock.mockResponse(HttpMethod::Get, req.url(), QByteArrayLiteral("OK"));
    auto *reply = sendScheduledGet(req);

    QSignalSpy queueSpy(m_scheduler, &QCNetworkRequestScheduler::requestQueued);

    QCOMPARE(m_scheduler->changePriority(reply, QCNetworkRequestPriority::High),
             QCNetworkRequestScheduler::CommandResult::Applied);

    qDebug() << "Priority change signal count:" << queueSpy.count();
    QVERIFY(queueSpy.count() >= 1);

    QCOMPARE(m_scheduler->cancelRequest(reply), QCNetworkRequestScheduler::CommandResult::Applied);
    QTRY_VERIFY_WITH_TIMEOUT(reply->isFinished(), 2000);
    reply->deleteLater();

    qDebug() << "Priority change contract verified";
}

/**
 * @brief 验证统计字段在离线调度路径下可读且语义自洽。
 */
void tst_QCNetworkScheduler::testStatistics()
{
    static_cast<void>(m_scheduler->cancelAllRequests());
    m_mock.clear();
    m_mock.setGlobalDelay(150);

    QCNetworkRequestScheduler::Config config = m_scheduler->configForTesting();
    config.setMaxConcurrentRequests(1);
    config.setMaxRequestsPerHost(10);
    m_scheduler->setConfigForTesting(config);
    const auto statsBefore = m_scheduler->statistics();

    QList<QCNetworkReply *> replies;
    for (int i = 0; i < 3; ++i) {
        QCNetworkRequest req(QUrl(QStringLiteral("http://stats-%1.test/test").arg(i)));
        req.setPriority(QCNetworkRequestPriority::Normal);
        m_mock.mockResponse(HttpMethod::Get, req.url(), QByteArrayLiteral("OK"));
        auto *reply = sendScheduledGet(req);
        replies.append(reply);
    }

    QTRY_COMPARE_WITH_TIMEOUT(m_scheduler->statistics().runningRequests(), 1, 2000);
    QTRY_COMPARE_WITH_TIMEOUT(m_scheduler->statistics().pendingRequests(), 2, 2000);

    for (auto *reply : replies) {
        QTRY_VERIFY_WITH_TIMEOUT(reply->isFinished(), 4000);
    }

    const auto stats = m_scheduler->statistics();

    // 统计口径属于 smoke：验证稳定且可重复的粗粒度合同，不在这里绑定每个实现细节。
    QCOMPARE(stats.pendingRequests(), 0);
    QCOMPARE(stats.runningRequests(), 0);
    QCOMPARE(stats.completedRequests(), statsBefore.completedRequests() + 3);
    QCOMPARE(stats.cancelledRequests(), statsBefore.cancelledRequests());
    QVERIFY(stats.totalBytesReceived() >= statsBefore.totalBytesReceived());
    QCOMPARE(stats.totalBytesSent(), statsBefore.totalBytesSent());
    QVERIFY(stats.avgResponseTime() >= 0.0);

    static_cast<void>(m_scheduler->cancelAllRequests());
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
    static_cast<void>(m_scheduler->cancelAllRequests());
    m_mock.clear();
    m_mock.setGlobalDelay(150);

    QCNetworkRequestScheduler::Config config = m_scheduler->configForTesting();
    config.setMaxConcurrentRequests(1);
    config.setMaxRequestsPerHost(10);
    m_scheduler->setConfigForTesting(config);

    QSignalSpy startedSpy(m_scheduler, &QCNetworkRequestScheduler::requestStarted);

    auto *lowReply      = sendScheduledGet(QUrl(QStringLiteral("http://critical-low.test/test")),
                                           QCNetworkRequestPriority::Low,
                                           QStringLiteral("CriticalLane"));
    auto *normalReply   = sendScheduledGet(QUrl(QStringLiteral("http://critical-normal.test/test")),
                                           QCNetworkRequestPriority::Normal,
                                           QStringLiteral("CriticalLane"));
    auto *criticalReply = sendScheduledGet(QUrl(QStringLiteral("http://critical-fast.test/test")),
                                           QCNetworkRequestPriority::Critical,
                                           QStringLiteral("CriticalLane"));

    QTRY_COMPARE_WITH_TIMEOUT(startedSpy.count(), 1, 2000);
    QCOMPARE(m_scheduler->statistics().runningRequests(), 1);
    QCOMPARE(m_scheduler->statistics().pendingRequests(), 2);

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
    static_cast<void>(m_scheduler->cancelAllRequests());
    m_mock.clear();
    m_mock.setGlobalDelay(150);

    QCNetworkRequestScheduler::LaneConfig controlCfg;
    controlCfg.setReservedGlobal(1);
    m_scheduler->setLaneConfigForTesting(QStringLiteral("ReservationControl"), controlCfg);
    m_scheduler->setLaneConfigForTesting(QStringLiteral("ReservationTransfer"),
                                         QCNetworkRequestScheduler::LaneConfig{});

    QCNetworkRequestScheduler::Config config = m_scheduler->configForTesting();
    config.setMaxConcurrentRequests(0);
    config.setMaxRequestsPerHost(10);
    m_scheduler->setConfigForTesting(config);

    QSignalSpy startedSpy(m_scheduler, &QCNetworkRequestScheduler::requestStarted);

    auto *transferReply = sendScheduledGet(QUrl(QStringLiteral(
                                               "http://reservation-transfer.test/test")),
                                           QCNetworkRequestPriority::VeryHigh,
                                           QStringLiteral("ReservationTransfer"));
    auto *controlReply  = sendScheduledGet(QUrl(QStringLiteral(
                                               "http://reservation-control.test/test")),
                                           QCNetworkRequestPriority::Low,
                                           QStringLiteral("ReservationControl"));

    // 放开唯一并发槽位后，应先兑现 Control lane 的 reservation，而不是按 priority 抢跑 Transfer。
    config.setMaxConcurrentRequests(1);
    m_scheduler->setConfigForTesting(config);

    QTRY_COMPARE_WITH_TIMEOUT(startedSpy.count(), 1, 2000);
    QCOMPARE(qvariant_cast<QCNetworkReply *>(startedSpy.at(0).at(0)), controlReply);
    QCOMPARE(laneFromArgs(startedSpy.at(0)), QStringLiteral("ReservationControl"));

    static_cast<void>(m_scheduler->cancelAllRequests());
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
    static_cast<void>(m_scheduler->cancelAllRequests());
    m_mock.clear();
    m_mock.setGlobalDelay(120);

    QCNetworkRequestScheduler::LaneConfig controlCfg;
    controlCfg.setWeight(3);
    controlCfg.setQuantum(1);
    QCNetworkRequestScheduler::LaneConfig transferCfg;
    transferCfg.setWeight(1);
    transferCfg.setQuantum(1);
    m_scheduler->setLaneConfigForTesting(QStringLiteral("FairControl"), controlCfg);
    m_scheduler->setLaneConfigForTesting(QStringLiteral("FairTransfer"), transferCfg);

    QCNetworkRequestScheduler::Config config = m_scheduler->configForTesting();
    config.setMaxConcurrentRequests(0);
    config.setMaxRequestsPerHost(10);
    m_scheduler->setConfigForTesting(config);

    QList<QCNetworkReply *> replies;
    for (int i = 0; i < 6; ++i) {
        replies.append(
            sendScheduledGet(QUrl(QStringLiteral("http://fair-control-%1.test/test").arg(i)),
                             QCNetworkRequestPriority::Normal,
                             QStringLiteral("FairControl")));
    }
    for (int i = 0; i < 2; ++i) {
        replies.append(
            sendScheduledGet(QUrl(QStringLiteral("http://fair-transfer-%1.test/test").arg(i)),
                             QCNetworkRequestPriority::Normal,
                             QStringLiteral("FairTransfer")));
    }

    QSignalSpy startedSpy(m_scheduler, &QCNetworkRequestScheduler::requestStarted);

    config.setMaxConcurrentRequests(1);
    m_scheduler->setConfigForTesting(config);

    QTRY_COMPARE_WITH_TIMEOUT(startedSpy.count(), 8, 5000);

    // 预期顺序是 3:1 的 lane 份额，而不是简单的全局 FIFO。
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
    static_cast<void>(m_scheduler->cancelAllRequests());
    m_mock.clear();
    m_mock.setGlobalDelay(150);

    QCNetworkRequestScheduler::Config config = m_scheduler->configForTesting();
    config.setMaxConcurrentRequests(0);
    config.setMaxRequestsPerHost(1);
    m_scheduler->setConfigForTesting(config);

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

    config.setMaxConcurrentRequests(2);
    m_scheduler->setConfigForTesting(config);

    QTRY_COMPARE_WITH_TIMEOUT(startedSpy.count(), 2, 2000);

    QSet<QString> startedHosts;
    startedHosts.insert(hostKeyFromArgs(startedSpy.at(0)));
    startedHosts.insert(hostKeyFromArgs(startedSpy.at(1)));
    QCOMPARE(startedHosts,
             QSet<QString>(
                 {QStringLiteral("http://hol-a.test:80"), QStringLiteral("http://hol-b.test:80")}));

    static_cast<void>(m_scheduler->cancelAllRequests());
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
    static_cast<void>(m_scheduler->cancelAllRequests());
    m_mock.clear();
    m_mock.setGlobalDelay(150);

    QCNetworkRequestScheduler::Config config = m_scheduler->configForTesting();
    config.setMaxConcurrentRequests(0);
    config.setMaxRequestsPerHost(1);
    m_scheduler->setConfigForTesting(config);

    QSignalSpy startedSpy(m_scheduler, &QCNetworkRequestScheduler::requestStarted);

    auto *httpReply       = sendScheduledGet(QUrl(QStringLiteral("http://origin.test/path")),
                                             QCNetworkRequestPriority::Normal,
                                             QStringLiteral("OriginLane"));
    auto *httpsReply      = sendScheduledGet(QUrl(QStringLiteral("https://origin.test/path")),
                                             QCNetworkRequestPriority::Normal,
                                             QStringLiteral("OriginLane"));
    auto *customPortReply = sendScheduledGet(QUrl(QStringLiteral("http://origin.test:8080/path")),
                                             QCNetworkRequestPriority::Normal,
                                             QStringLiteral("OriginLane"));
    auto *httpsCustomPortReply = sendScheduledGet(QUrl(QStringLiteral(
                                                      "https://origin.test:8443/path")),
                                                  QCNetworkRequestPriority::Normal,
                                                  QStringLiteral("OriginLane"));
    auto *ipv6Reply            = sendScheduledGet(QUrl(QStringLiteral("http://[2001:db8::1]/path")),
                                                  QCNetworkRequestPriority::Normal,
                                                  QStringLiteral("OriginLane"));
    auto *secondPortReply = sendScheduledGet(QUrl(QStringLiteral("http://origin.test:81/path")),
                                             QCNetworkRequestPriority::Normal,
                                             QStringLiteral("OriginLane"));
    auto *ipv6HttpsReply = sendScheduledGet(QUrl(QStringLiteral("https://[2001:db8::1]:9443/path")),
                                            QCNetworkRequestPriority::Normal,
                                            QStringLiteral("OriginLane"));

    config.setMaxConcurrentRequests(7);
    m_scheduler->setConfigForTesting(config);

    QTRY_COMPARE_WITH_TIMEOUT(startedSpy.count(), 7, 2000);

    // Core scheduler 只接收 HTTP/HTTPS；hostKey 仍必须带 scheme + effectivePort，
    // 否则同一 host 的不同 origin 会被错误地合并。
    QSet<QString> startedHosts;
    for (int i = 0; i < startedSpy.count(); ++i) {
        startedHosts.insert(hostKeyFromArgs(startedSpy.at(i)));
    }
    QCOMPARE(startedHosts,
             QSet<QString>({QStringLiteral("http://origin.test:80"),
                            QStringLiteral("https://origin.test:443"),
                            QStringLiteral("http://origin.test:8080"),
                            QStringLiteral("https://origin.test:8443"),
                            QStringLiteral("http://[2001:db8::1]:80"),
                            QStringLiteral("http://origin.test:81"),
                            QStringLiteral("https://[2001:db8::1]:9443")}));

    static_cast<void>(m_scheduler->cancelAllRequests());
    QTRY_VERIFY_WITH_TIMEOUT(httpReply->isFinished(), 2000);
    QTRY_VERIFY_WITH_TIMEOUT(httpsReply->isFinished(), 2000);
    QTRY_VERIFY_WITH_TIMEOUT(customPortReply->isFinished(), 2000);
    QTRY_VERIFY_WITH_TIMEOUT(httpsCustomPortReply->isFinished(), 2000);
    QTRY_VERIFY_WITH_TIMEOUT(ipv6Reply->isFinished(), 2000);
    QTRY_VERIFY_WITH_TIMEOUT(secondPortReply->isFinished(), 2000);
    QTRY_VERIFY_WITH_TIMEOUT(ipv6HttpsReply->isFinished(), 2000);
    httpReply->deleteLater();
    httpsReply->deleteLater();
    customPortReply->deleteLater();
    httpsCustomPortReply->deleteLater();
    ipv6Reply->deleteLater();
    secondPortReply->deleteLater();
    ipv6HttpsReply->deleteLater();
    m_mock.setGlobalDelay(0);

    qDebug() << "Origin hostKey contract verified";
}

/**
 * @brief 验证不可满足的 reservation 不会让其他 host 的请求饿死。
 */
void tst_QCNetworkScheduler::testReservationFallbackProgress()
{
    static_cast<void>(m_scheduler->cancelAllRequests());
    m_mock.clear();
    m_mock.setGlobalDelay(150);

    QCNetworkRequestScheduler::LaneConfig controlCfg;
    controlCfg.setReservedPerHost(1);
    m_scheduler->setLaneConfigForTesting(QStringLiteral("FallbackControl"), controlCfg);
    m_scheduler->setLaneConfigForTesting(QStringLiteral("FallbackTransfer"),
                                         QCNetworkRequestScheduler::LaneConfig{});

    QCNetworkRequestScheduler::Config config = m_scheduler->configForTesting();
    config.setMaxConcurrentRequests(2);
    config.setMaxRequestsPerHost(1);
    m_scheduler->setConfigForTesting(config);

    QSignalSpy startedSpy(m_scheduler, &QCNetworkRequestScheduler::requestStarted);

    auto *blockingTransfer = sendScheduledGet(QUrl(QStringLiteral("http://blocked-host.test/run")),
                                              QCNetworkRequestPriority::Normal,
                                              QStringLiteral("FallbackTransfer"));
    QTRY_COMPARE_WITH_TIMEOUT(startedSpy.count(), 1, 2000);

    auto *blockedControl = sendScheduledGet(QUrl(
                                                QStringLiteral("http://blocked-host.test/control")),
                                            QCNetworkRequestPriority::High,
                                            QStringLiteral("FallbackControl"));
    auto *freeTransfer   = sendScheduledGet(QUrl(QStringLiteral("http://free-host.test/run")),
                                            QCNetworkRequestPriority::Normal,
                                            QStringLiteral("FallbackTransfer"));

    QTRY_COMPARE_WITH_TIMEOUT(startedSpy.count(), 2, 2000);
    QCOMPARE(qvariant_cast<QCNetworkReply *>(startedSpy.at(1).at(0)), freeTransfer);
    QCOMPARE(hostKeyFromArgs(startedSpy.at(1)), QStringLiteral("http://free-host.test:80"));

    static_cast<void>(m_scheduler->cancelAllRequests());
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
    static_cast<void>(m_scheduler->cancelAllRequests());
    m_mock.clear();

    QCNetworkRequestScheduler::Config config = m_scheduler->configForTesting();
    config.setMaxConcurrentRequests(0);
    config.setMaxRequestsPerHost(10);
    m_scheduler->setConfigForTesting(config);

    QSignalSpy cancelSpy(m_scheduler, &QCNetworkRequestScheduler::requestCancelled);

    auto *pendingTransfer = sendScheduledGet(QUrl(QStringLiteral("http://cancel-pending.test/one")),
                                             QCNetworkRequestPriority::Normal,
                                             QStringLiteral("CancelTransfer"));
    auto *deferredTransfer = sendScheduledGet(QUrl(QStringLiteral(
                                                  "http://cancel-deferred.test/two")),
                                              QCNetworkRequestPriority::Normal,
                                              QStringLiteral("CancelTransfer"));
    auto *controlPending = sendScheduledGet(QUrl(
                                                QStringLiteral("http://cancel-control.test/three")),
                                            QCNetworkRequestPriority::Normal,
                                            QStringLiteral("CancelControl"));

    // PendingOnly 只清同 lane 的 pending/deferred，不能误伤别的 lane，也不能打断 running。
    QCOMPARE(m_scheduler->deferPendingRequest(deferredTransfer),
             QCNetworkRequestScheduler::CommandResult::Applied);

    int cancelledRequests = -1;
    QCOMPARE(m_scheduler->cancelLaneRequests(QStringLiteral("CancelTransfer"),
                                             QCNetworkRequestScheduler::CancelLaneScope::PendingOnly,
                                             &cancelledRequests),
             QCNetworkRequestScheduler::CommandResult::Applied);
    QCOMPARE(cancelledRequests, 2);
    QTRY_COMPARE_WITH_TIMEOUT(cancelSpy.count(), 2, 2000);
    QCOMPARE(laneFromArgs(cancelSpy.at(0)), QStringLiteral("CancelTransfer"));
    QCOMPARE(laneFromArgs(cancelSpy.at(1)), QStringLiteral("CancelTransfer"));
    QCOMPARE(m_scheduler->statistics().pendingRequests(), 1);
    QCOMPARE(m_scheduler->statistics().runningRequests(), 0);

    static_cast<void>(m_scheduler->cancelAllRequests());
    QTRY_VERIFY_WITH_TIMEOUT(pendingTransfer->isFinished(), 2000);
    QTRY_VERIFY_WITH_TIMEOUT(deferredTransfer->isFinished(), 2000);
    QTRY_VERIFY_WITH_TIMEOUT(controlPending->isFinished(), 2000);
    pendingTransfer->deleteLater();
    deferredTransfer->deleteLater();
    controlPending->deleteLater();

    m_mock.clear();
    m_mock.setGlobalDelay(150);
    cancelSpy.clear();

    config.setMaxConcurrentRequests(1);
    m_scheduler->setConfigForTesting(config);

    QSignalSpy startedSpy(m_scheduler, &QCNetworkRequestScheduler::requestStarted);

    auto *runningTransfer = sendScheduledGet(QUrl(QStringLiteral("http://cancel-running.test/run")),
                                             QCNetworkRequestPriority::Normal,
                                             QStringLiteral("CancelTransfer"));
    QTRY_COMPARE_WITH_TIMEOUT(startedSpy.count(), 1, 2000);

    auto *waitingControl = sendScheduledGet(QUrl(QStringLiteral(
                                                "http://cancel-running.test/control")),
                                            QCNetworkRequestPriority::Normal,
                                            QStringLiteral("CancelControl"));

    cancelledRequests = -1;
    QCOMPARE(m_scheduler
                 ->cancelLaneRequests(QStringLiteral("CancelTransfer"),
                                      QCNetworkRequestScheduler::CancelLaneScope::PendingAndRunning,
                                      &cancelledRequests),
             QCNetworkRequestScheduler::CommandResult::Applied);
    QCOMPARE(cancelledRequests, 1);
    QTRY_COMPARE_WITH_TIMEOUT(cancelSpy.count(), 1, 2000);
    QCOMPARE(laneFromArgs(cancelSpy.at(0)), QStringLiteral("CancelTransfer"));
    QTRY_COMPARE_WITH_TIMEOUT(startedSpy.count(), 2, 2000);
    QCOMPARE(qvariant_cast<QCNetworkReply *>(startedSpy.at(1).at(0)), waitingControl);
    QCOMPARE(laneFromArgs(startedSpy.at(1)), QStringLiteral("CancelControl"));

    static_cast<void>(m_scheduler->cancelAllRequests());
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
    static_cast<void>(m_scheduler->cancelAllRequests());
    m_mock.clear();

    QSignalSpy emptySpy(m_scheduler, &QCNetworkRequestScheduler::queueEmpty);

    QCNetworkRequest req(QUrl(QStringLiteral("http://queue-empty.test/test")));
    req.setPriority(QCNetworkRequestPriority::Normal);
    m_mock.mockResponse(HttpMethod::Get, req.url(), QByteArrayLiteral("OK"));
    auto *reply = sendScheduledGet(req);

    QCOMPARE(m_scheduler->cancelRequest(reply), QCNetworkRequestScheduler::CommandResult::Applied);

    // 等待信号（避免固定 sleep 引入 flaky）
    if (emptySpy.count() == 0) {
        QVERIFY(emptySpy.wait(2000));
    }

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

    m_manager->enableRequestScheduler(true);
    QVERIFY(m_manager->isSchedulerEnabled());

    auto runAsyncMethod =
        [this](HttpMethod method, const QUrl &url, const QByteArray &body = QByteArray()) {
            QCNetworkRequest req(url);
            req.setPriority(QCNetworkRequestPriority::High);
            const HttpMethod mockMethod = method == HttpMethod::Delete && !body.isEmpty()
                                              ? HttpMethod::Custom
                                              : method;
            m_mock.mockResponse(mockMethod, req.url(), QByteArrayLiteral("OK"));

            QSignalSpy queuedSpy(m_scheduler, &QCNetworkRequestScheduler::requestQueued);
            QSignalSpy startedSpy(m_scheduler, &QCNetworkRequestScheduler::requestStarted);

            QCNetworkReply *reply = nullptr;
            switch (method) {
                case HttpMethod::Head:
                    reply = m_manager->head(req);
                    break;
                case HttpMethod::Get:
                    reply = m_manager->get(req);
                    break;
                case HttpMethod::Post:
                    reply = m_manager->post(req, body);
                    break;
                case HttpMethod::Put:
                    reply = m_manager->put(req, body);
                    break;
                case HttpMethod::Delete:
                    reply = m_manager->sendCustomRequest(req, QByteArrayLiteral("DELETE"), body);
                    break;
                case HttpMethod::Patch:
                    reply = m_manager->patch(req, body);
                    break;
                case HttpMethod::Custom:
                    reply = nullptr;
                    break;
            }

            QVERIFY(reply != nullptr);
            QVERIFY(reply->parent() == m_manager);
            QTRY_VERIFY_WITH_TIMEOUT(queuedSpy.count() + startedSpy.count() >= 1, 2000);
            QTRY_VERIFY_WITH_TIMEOUT(reply->isFinished(), 2000);
            QCOMPARE(reply->error(), NetworkError::NoError);
            reply->deleteLater();
        };

    runAsyncMethod(HttpMethod::Head, QUrl(QStringLiteral("http://integration.test/head")));
    runAsyncMethod(HttpMethod::Get, QUrl(QStringLiteral("http://integration.test/get")));
    runAsyncMethod(HttpMethod::Post,
                   QUrl(QStringLiteral("http://integration.test/post")),
                   QByteArrayLiteral("POST"));
    runAsyncMethod(HttpMethod::Put,
                   QUrl(QStringLiteral("http://integration.test/put")),
                   QByteArrayLiteral("PUT"));
    runAsyncMethod(HttpMethod::Delete,
                   QUrl(QStringLiteral("http://integration.test/delete")),
                   QByteArrayLiteral("DEL"));
    runAsyncMethod(HttpMethod::Patch,
                   QUrl(QStringLiteral("http://integration.test/patch")),
                   QByteArrayLiteral("PATCH"));

    m_manager->enableRequestScheduler(false);
    QVERIFY(!m_manager->isSchedulerEnabled());

    qDebug() << "AccessManager integration contract verified";
}

/**
 * @brief 验证无效优先级不会进入 scheduler 队列，并由 reply 显式报告 InvalidRequest。
 */
void tst_QCNetworkScheduler::testInvalidSchedulerPriorityFailsClosed()
{
    static_cast<void>(m_scheduler->cancelAllRequests());
    m_mock.clear();

    const QUrl url(QStringLiteral("http://invalid-priority.test/reply"));
    QCNetworkRequest request(url);
    request.setPriority(static_cast<QCNetworkRequestPriority>(99));
    m_mock.mockResponse(HttpMethod::Get, url, QByteArrayLiteral("OK"));

    QSignalSpy queuedSpy(m_scheduler, &QCNetworkRequestScheduler::requestQueued);
    QCNetworkReply *reply = sendScheduledGet(request);

    QTRY_VERIFY_WITH_TIMEOUT(reply->isFinished(), 2000);
    QCOMPARE(reply->error(), NetworkError::InvalidRequest);
    QCOMPARE(queuedSpy.count(), 0);
    QCOMPARE(m_scheduler->statistics().pendingRequests(), 0);
    QCOMPARE(m_scheduler->statistics().runningRequests(), 0);
    reply->deleteLater();
}

/**
 * @brief 验证空 reply、未跟踪 reply 和无效命令状态均显式失败且不改变调度状态。
 */
void tst_QCNetworkScheduler::testSchedulerCommandResultFailuresAreSideEffectFree()
{
    static_cast<void>(m_scheduler->cancelAllRequests());

    QCOMPARE(m_scheduler->deferPendingRequest(nullptr),
             QCNetworkRequestScheduler::CommandResult::NullReply);
    QCOMPARE(m_scheduler->undeferRequest(nullptr),
             QCNetworkRequestScheduler::CommandResult::NullReply);
    QCOMPARE(m_scheduler->cancelRequest(nullptr),
             QCNetworkRequestScheduler::CommandResult::NullReply);
    QCOMPARE(m_scheduler->changePriority(nullptr, QCNetworkRequestPriority::Normal),
             QCNetworkRequestScheduler::CommandResult::NullReply);

    QCNetworkAccessManager manager;
    QCNetworkMockHandler mock;
    const QUrl untrackedUrl(QStringLiteral("http://untracked.test/reply"));
    mock.setCaptureEnabled(false);
    mock.mockResponse(HttpMethod::Get, untrackedUrl, QByteArrayLiteral("OK"));
    QCurl::TestSupport::setMockHandler(manager, &mock);
    QCNetworkReply *untrackedReply = manager.get(QCNetworkRequest(untrackedUrl));
    QVERIFY(untrackedReply);

    QCOMPARE(m_scheduler->deferPendingRequest(untrackedReply),
             QCNetworkRequestScheduler::CommandResult::NotTracked);
    QCOMPARE(m_scheduler->undeferRequest(untrackedReply),
             QCNetworkRequestScheduler::CommandResult::NotTracked);
    QCOMPARE(m_scheduler->cancelRequest(untrackedReply),
             QCNetworkRequestScheduler::CommandResult::NotTracked);
    QCOMPARE(m_scheduler->changePriority(untrackedReply, QCNetworkRequestPriority::High),
             QCNetworkRequestScheduler::CommandResult::NotTracked);
    QCOMPARE(m_scheduler->statistics().pendingRequests(), 0);
    QCOMPARE(m_scheduler->statistics().runningRequests(), 0);

    untrackedReply->cancel();
    QTRY_VERIFY_WITH_TIMEOUT(untrackedReply->isFinished(), 2000);
    untrackedReply->deleteLater();
}

/**
 * @brief 验证幂等命令返回 NoChange，非法 lane scope 与优先级返回 InvalidArgument。
 */
void tst_QCNetworkScheduler::testSchedulerCommandResultNoChangeAndInvalidArguments()
{
    static_cast<void>(m_scheduler->cancelAllRequests());
    m_mock.clear();

    const QCNetworkRequestScheduler::Config oldConfig = m_scheduler->configForTesting();
    QCNetworkRequestScheduler::Config config          = oldConfig;
    config.setMaxConcurrentRequests(0);
    config.setMaxRequestsPerHost(1);
    m_scheduler->setConfigForTesting(config);

    QCNetworkReply *reply = sendScheduledGet(QUrl(QStringLiteral(
                                                 "http://scheduler-command-result.test/pending")),
                                             QCNetworkRequestPriority::Normal,
                                             QStringLiteral("CommandResultLane"));
    QVERIFY(reply);
    QCOMPARE(m_scheduler->statistics().pendingRequests(), 1);

    QCOMPARE(m_scheduler->changePriority(reply, QCNetworkRequestPriority::Normal),
             QCNetworkRequestScheduler::CommandResult::NoChange);
    QCOMPARE(m_scheduler->changePriority(reply, static_cast<QCNetworkRequestPriority>(99)),
             QCNetworkRequestScheduler::CommandResult::InvalidArgument);

    int cancelledRequests = -1;
    QCOMPARE(m_scheduler->cancelLaneRequests(QStringLiteral("CommandResultLane"),
                                             static_cast<QCNetworkRequestScheduler::CancelLaneScope>(
                                                 99),
                                             &cancelledRequests),
             QCNetworkRequestScheduler::CommandResult::InvalidArgument);
    QCOMPARE(cancelledRequests, 0);
    QVERIFY(!reply->isFinished());
    QCOMPARE(m_scheduler->statistics().pendingRequests(), 1);

    QCOMPARE(m_scheduler->cancelLaneRequests(QStringLiteral("UnknownCommandResultLane"),
                                             QCNetworkRequestScheduler::CancelLaneScope::PendingOnly,
                                             &cancelledRequests),
             QCNetworkRequestScheduler::CommandResult::NoChange);
    QCOMPARE(cancelledRequests, 0);
    QVERIFY(!reply->isFinished());
    QCOMPARE(m_scheduler->statistics().pendingRequests(), 1);

    QCOMPARE(m_scheduler->cancelRequest(reply), QCNetworkRequestScheduler::CommandResult::Applied);
    QTRY_VERIFY_WITH_TIMEOUT(reply->isFinished(), 2000);
    QCOMPARE(m_scheduler->cancelAllRequests(), QCNetworkRequestScheduler::CommandResult::NoChange);

    reply->deleteLater();
    m_scheduler->setConfigForTesting(oldConfig);
}

/**
 * @brief 验证 reply affinity 不匹配时命令同步拒绝，且不会保存或改变该 reply。
 */
void tst_QCNetworkScheduler::testSchedulerCommandResultRejectsAffinityMismatch()
{
    static_cast<void>(m_scheduler->cancelAllRequests());

    std::promise<QCNetworkReply *> replyPromise;
    std::future<QCNetworkReply *> replyFuture = replyPromise.get_future();
    std::promise<void> releasePromise;
    std::future<void> releaseFuture = releasePromise.get_future();
    bool replyReleased = false;
    std::thread foreignWorker([&replyPromise, releaseFuture = std::move(releaseFuture)]() mutable {
        auto *reply = new QCNetworkReply(
            QCNetworkReply::TestOnlyKey{},
            QCNetworkRequest(QUrl(QStringLiteral("http://foreign-affinity.test/reply"))),
            HttpMethod::Get);
        replyPromise.set_value(reply);
        releaseFuture.wait();
        delete reply;
    });
    const auto joinWorker = qScopeGuard([&]() {
        if (!replyReleased) {
            releasePromise.set_value();
        }
        if (foreignWorker.joinable()) {
            foreignWorker.join();
        }
    });
    QCNetworkReply *foreignReply = replyFuture.get();
    const QPointer<QCNetworkReply> foreignReplyGuard(foreignReply);
    QVERIFY(foreignReply->thread() != m_scheduler->thread());

    QCOMPARE(m_scheduler->deferPendingRequest(foreignReply),
             QCNetworkRequestScheduler::CommandResult::ThreadAffinityMismatch);
    QCOMPARE(m_scheduler->undeferRequest(foreignReply),
             QCNetworkRequestScheduler::CommandResult::ThreadAffinityMismatch);
    QCOMPARE(m_scheduler->cancelRequest(foreignReply),
             QCNetworkRequestScheduler::CommandResult::ThreadAffinityMismatch);
    QCOMPARE(m_scheduler->changePriority(foreignReply, QCNetworkRequestPriority::High),
             QCNetworkRequestScheduler::CommandResult::ThreadAffinityMismatch);
    QCOMPARE(m_scheduler->statistics().pendingRequests(), 0);
    QCOMPARE(m_scheduler->statistics().runningRequests(), 0);

    // 由 affinity 线程析构，并以 join 建立 QPointer 观察前的同步边界。
    replyReleased = true;
    releasePromise.set_value();
    foreignWorker.join();
    QVERIFY(foreignReplyGuard.isNull());
}

/**
 * @brief 验证其他调度命令在错误线程同步失败，且不会改变 pending reply 或优先级。
 */
void tst_QCNetworkScheduler::testCrossThreadSchedulerCommandsAreRejectedWithoutMutation()
{
    static_cast<void>(m_scheduler->cancelAllRequests());
    m_mock.clear();

    const QCNetworkRequestScheduler::Config oldConfig = m_scheduler->configForTesting();
    QCNetworkRequestScheduler::Config config          = oldConfig;
    config.setMaxConcurrentRequests(0);
    config.setMaxRequestsPerHost(1);
    m_scheduler->setConfigForTesting(config);

    QSignalSpy queuedSpy(m_scheduler, &QCNetworkRequestScheduler::requestQueued);
    QCNetworkReply *reply = sendScheduledGet(QUrl(QStringLiteral(
                                                 "http://cross-thread-command.test/pending")),
                                             QCNetworkRequestPriority::Low,
                                             QStringLiteral("CrossThreadCommandLane"));
    QCOMPARE(queuedSpy.count(), 1);

    QCNetworkRequestScheduler::CommandResult deferResult
        = QCNetworkRequestScheduler::CommandResult::Applied;
    QCNetworkRequestScheduler::CommandResult undeferResult
        = QCNetworkRequestScheduler::CommandResult::Applied;
    QCNetworkRequestScheduler::CommandResult priorityResult
        = QCNetworkRequestScheduler::CommandResult::Applied;
    QCNetworkRequestScheduler::CommandResult laneResult
        = QCNetworkRequestScheduler::CommandResult::Applied;
    QCNetworkRequestScheduler::CommandResult allResult
        = QCNetworkRequestScheduler::CommandResult::Applied;
    int cancelledRequests = -1;

    std::thread worker([&]() {
        deferResult    = m_scheduler->deferPendingRequest(reply);
        undeferResult  = m_scheduler->undeferRequest(reply);
        priorityResult = m_scheduler->changePriority(reply, QCNetworkRequestPriority::Critical);
        laneResult = m_scheduler
                         ->cancelLaneRequests(QStringLiteral("CrossThreadCommandLane"),
                                              QCNetworkRequestScheduler::CancelLaneScope::PendingOnly,
                                              &cancelledRequests);
        allResult = m_scheduler->cancelAllRequests();
    });
    worker.join();

    QCOMPARE(deferResult, QCNetworkRequestScheduler::CommandResult::WrongThread);
    QCOMPARE(undeferResult, QCNetworkRequestScheduler::CommandResult::WrongThread);
    QCOMPARE(priorityResult, QCNetworkRequestScheduler::CommandResult::WrongThread);
    QCOMPARE(laneResult, QCNetworkRequestScheduler::CommandResult::WrongThread);
    QCOMPARE(allResult, QCNetworkRequestScheduler::CommandResult::WrongThread);
    QCOMPARE(cancelledRequests, 0);
    QCOMPARE(queuedSpy.count(), 1);
    QCOMPARE(m_scheduler->statistics().pendingRequests(), 1);
    QVERIFY(!reply->isFinished());

    QCOMPARE(m_scheduler->cancelRequest(reply), QCNetworkRequestScheduler::CommandResult::Applied);
    QTRY_VERIFY_WITH_TIMEOUT(reply->isFinished(), 2000);
    reply->deleteLater();
    m_scheduler->setConfigForTesting(oldConfig);
}

void tst_QCNetworkScheduler::testCrossThreadCancelIsRejectedWithoutMutation()
{
    static_cast<void>(m_scheduler->cancelAllRequests());
    m_mock.clear();

    const QCNetworkRequestScheduler::Config oldConfig = m_scheduler->configForTesting();
    QCNetworkRequestScheduler::Config config          = oldConfig;
    config.setMaxConcurrentRequests(0);
    config.setMaxRequestsPerHost(1);
    m_scheduler->setConfigForTesting(config);

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

    QCNetworkRequestScheduler::CommandResult commandResult
        = QCNetworkRequestScheduler::CommandResult::Applied;
    std::thread worker([scheduler = m_scheduler, reply, &commandResult]() {
        commandResult = scheduler->cancelRequest(reply);
    });
    worker.join();

    QCOMPARE(commandResult, QCNetworkRequestScheduler::CommandResult::WrongThread);
    QTest::qWait(50);
    QCOMPARE(cancelSpy.count(), 0);
    QCOMPARE(startedSpy.count(), 0);
    QCOMPARE(signalThread, nullptr);
    QVERIFY(!reply->isFinished());
    QCOMPARE(m_scheduler->statistics().pendingRequests(), 1);

    QCOMPARE(m_scheduler->cancelRequest(reply), QCNetworkRequestScheduler::CommandResult::Applied);
    QTRY_VERIFY_WITH_TIMEOUT(reply->isFinished(), 2000);

    reply->deleteLater();
    m_scheduler->setConfigForTesting(oldConfig);

    qDebug() << "Cross-thread scheduler cancel is rejected without queue or reply mutation";
}

/**
 * @brief 验证 private owner-thread 入口不会把 live QObject 从错误线程排回调度线程。
 */
void tst_QCNetworkScheduler::testPrivateOwnerThreadPathsRejectWrongThreadWithoutMutation()
{
    static_cast<void>(m_scheduler->cancelAllRequests());
    m_mock.clear();

    const QCNetworkRequestScheduler::Config oldConfig = m_scheduler->configForTesting();
    QCNetworkRequestScheduler::Config config          = oldConfig;
    config.setMaxConcurrentRequests(0);
    config.setMaxRequestsPerHost(1);
    m_scheduler->setConfigForTesting(config);

    const auto verifyRejected = [this](auto invoke, const QString &path) {
        QCNetworkReply *reply = sendScheduledGet(
            QUrl(QStringLiteral("http://private-owner-thread.test/%1").arg(path)),
            QCNetworkRequestPriority::Normal,
            QStringLiteral("PrivateOwnerThreadLane"));
        QVERIFY(reply);
        QCOMPARE(m_scheduler->statistics().pendingRequests(), 1);

        QSignalSpy aboutToStartSpy(m_scheduler,
                                   &QCNetworkRequestScheduler::requestAboutToStart);
        QSignalSpy finishedSpy(m_scheduler, &QCNetworkRequestScheduler::requestFinished);
        std::thread worker([&]() { invoke(reply); });
        worker.join();

        bool barrierReached = false;
        QVERIFY(QMetaObject::invokeMethod(
            m_scheduler,
            [&barrierReached]() { barrierReached = true; },
            Qt::QueuedConnection));
        QTRY_VERIFY_WITH_TIMEOUT(barrierReached, 1000);

        QCOMPARE(aboutToStartSpy.count(), 0);
        QCOMPARE(finishedSpy.count(), 0);
        QCOMPARE(m_scheduler->statistics().pendingRequests(), 1);
        QCOMPARE(m_scheduler->statistics().runningRequests(), 0);
        QVERIFY(!reply->isFinished());

        QCOMPARE(m_scheduler->cancelRequest(reply),
                 QCNetworkRequestScheduler::CommandResult::Applied);
        QTRY_VERIFY_WITH_TIMEOUT(reply->isFinished(), 2000);
        reply->deleteLater();
    };

    verifyRejected(
        [this](QCNetworkReply *reply) {
            QCNetworkRequestSchedulerTestAccess::startRequest(m_scheduler, reply);
        },
        QStringLiteral("start"));
    verifyRejected(
        [this](QCNetworkReply *reply) {
            QCNetworkRequestSchedulerTestAccess::finishRequest(m_scheduler, reply);
        },
        QStringLiteral("finish"));
    verifyRejected(
        [this](QCNetworkReply *reply) {
            QCNetworkRequestSchedulerTestAccess::destroyReply(m_scheduler, reply);
        },
        QStringLiteral("destroy"));

    m_scheduler->setConfigForTesting(oldConfig);
}

/**
 * @brief 验证 progress 使用 AutoConnection，并在 rebind/disconnect 后保持单一有效连接。
 */
void tst_QCNetworkScheduler::testProgressTrackingAutoConnectionAndTeardown()
{
    auto *sameThreadReply = new QCNetworkReply(
        QCNetworkReply::TestOnlyKey{},
        QCNetworkRequest(QUrl(QStringLiteral("http://progress-contract.test/same-thread"))),
        HttpMethod::Get);
    QCNetworkRequestSchedulerTestAccess::resetProgressWindow(m_scheduler);
    QCNetworkRequestSchedulerTestAccess::connectProgress(m_scheduler, sameThreadReply);

    Q_EMIT sameThreadReply->downloadProgress(10, 100);
    QCOMPARE(QCNetworkRequestSchedulerTestAccess::progressWindow(m_scheduler), 10);
    Q_EMIT sameThreadReply->downloadProgress(15, 100);
    Q_EMIT sameThreadReply->uploadProgress(4, 50);
    QCOMPARE(QCNetworkRequestSchedulerTestAccess::progressWindow(m_scheduler), 19);

    QCNetworkRequestSchedulerTestAccess::connectProgress(m_scheduler, sameThreadReply);
    Q_EMIT sameThreadReply->downloadProgress(7, 100);
    QCOMPARE(QCNetworkRequestSchedulerTestAccess::progressWindow(m_scheduler), 26);

    QCNetworkRequestSchedulerTestAccess::disconnectProgress(m_scheduler, sameThreadReply);
    Q_EMIT sameThreadReply->downloadProgress(30, 100);
    QCoreApplication::processEvents();
    QCOMPARE(QCNetworkRequestSchedulerTestAccess::progressWindow(m_scheduler), 26);
    delete sameThreadReply;

    auto *crossThreadReply = new QCNetworkReply(
        QCNetworkReply::TestOnlyKey{},
        QCNetworkRequest(QUrl(QStringLiteral("http://progress-contract.test/cross-thread"))),
        HttpMethod::Get);

    QCNetworkRequestSchedulerTestAccess::resetProgressWindow(m_scheduler);
    QCNetworkRequestSchedulerTestAccess::connectProgress(m_scheduler, crossThreadReply);
    std::thread progressEmitter(
        [crossThreadReply]() { Q_EMIT crossThreadReply->downloadProgress(9, 100); });
    progressEmitter.join();
    QCOMPARE(QCNetworkRequestSchedulerTestAccess::progressWindow(m_scheduler), 0);
    QTRY_COMPARE_WITH_TIMEOUT(QCNetworkRequestSchedulerTestAccess::progressWindow(m_scheduler),
                              9,
                              1000);

    QCNetworkRequestSchedulerTestAccess::disconnectProgress(m_scheduler, crossThreadReply);
    std::thread disconnectedEmitter(
        [crossThreadReply]() { Q_EMIT crossThreadReply->downloadProgress(20, 100); });
    disconnectedEmitter.join();
    QCoreApplication::processEvents();
    QCOMPARE(QCNetworkRequestSchedulerTestAccess::progressWindow(m_scheduler), 9);

    delete crossThreadReply;
}

QTEST_MAIN(tst_QCNetworkScheduler)
#include "tst_QCNetworkScheduler.moc"
