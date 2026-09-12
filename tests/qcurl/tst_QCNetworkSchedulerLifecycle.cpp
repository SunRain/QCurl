// SPDX-License-Identifier: MIT
// Copyright (c) 2025 QCurl Project

#include "qcnetwork_scheduler_test.h"

#include <QPointer>

using namespace QCurl;

namespace {

QCNetworkAccessManager *makeManager(QObject *owner, QCNetworkMockHandler *mock)
{
    auto *manager = new QCNetworkAccessManager(owner);
    TestSupport::setMockHandler(*manager, mock);
    manager->enableRequestScheduler(true);
    auto policy = manager->schedulerPolicy();
    policy.setMaxConcurrentRequests(1);
    if (!manager->setSchedulerPolicy(policy)) {
        qFatal("invalid scheduler fixture policy");
    }
    mock->setCaptureEnabled(true);
    mock->setGlobalDelay(80);
    return manager;
}

QCNetworkReply *get(QCNetworkAccessManager *manager, QCNetworkMockHandler &mock, const char *path)
{
    const QUrl url(QStringLiteral("http://lifetime.test/%1").arg(QString::fromUtf8(path)));
    mock.mockResponse(HttpMethod::Get, url, QByteArrayLiteral("OK"));
    return manager->get(QCNetworkRequest(url));
}

} // namespace

void tst_QCNetworkScheduler::testRequestStartedRequiresExecuteDispatch()
{
    SchedulerHarness h;
    QSignalSpy started(&h.manager(), &QCNetworkAccessManager::schedulerRequestStarted);
    auto *reply = h.get(QUrl(QStringLiteral("http://execute.test/")));
    QCOMPARE(started.count(), 0);
    QCOMPARE(h.mock().capturedRequests().size(), 0);
    QVERIFY(!reply->isRunning());
    QTRY_COMPARE(started.count(), 1);
    QCOMPARE(h.mock().capturedRequests().size(), 1);
}

void tst_QCNetworkScheduler::testCancelAfterStartQueuedDoesNotStart()
{
    SchedulerHarness h;
    auto *reply = h.get(QUrl(QStringLiteral("http://queued-cancel.test/")));
    QSignalSpy about(&h.manager(), &QCNetworkAccessManager::schedulerRequestAboutToStart);
    QSignalSpy started(&h.manager(), &QCNetworkAccessManager::schedulerRequestStarted);
    QCOMPARE(h.manager().cancelScheduledRequest(reply), SchedulerCommandResult::Applied);
    QCOMPARE(h.manager().cancelScheduledRequest(reply), SchedulerCommandResult::NotTracked);
    QTRY_VERIFY(reply->isFinished());
    QCOMPARE(about.count(), 0);
    QCOMPARE(started.count(), 0);
    QCOMPARE(h.mock().capturedRequests().size(), 0);
    QCOMPARE(h.manager().schedulerStatistics().cancelledRequests(), 1);
}

void tst_QCNetworkScheduler::testCancelFromAboutToStartSlotPreventsExecute()
{
    SchedulerHarness h;
    QSignalSpy started(&h.manager(), &QCNetworkAccessManager::schedulerRequestStarted);
    connect(&h.manager(),
            &QCNetworkAccessManager::schedulerRequestAboutToStart,
            this,
            [&](QCNetworkReply *reply) {
                QCOMPARE(h.manager().cancelScheduledRequest(reply), SchedulerCommandResult::Applied);
            });
    auto *reply = h.get(QUrl(QStringLiteral("http://sync-cancel.test/")));
    QTRY_VERIFY(reply->isFinished());
    QCOMPARE(started.count(), 0);
    QCOMPARE(h.mock().capturedRequests().size(), 0);
}

void tst_QCNetworkScheduler::testDirectCancelContract_data()
{
    QTest::addColumn<int>("state");
    QTest::newRow("running") << 0;
    QTest::newRow("pending") << 1;
    QTest::newRow("deferred") << 2;
}

void tst_QCNetworkScheduler::testDirectCancelContract()
{
    QFETCH(int, state);
    SchedulerHarness h;
    QVERIFY(h.configure());
    h.mock().setGlobalDelay(2000);
    if (state != 0) {
        h.occupySlot();
    }
    auto *reply = h.get(QUrl(QStringLiteral("http://direct-cancel.test/")));
    if (state == 2) {
        QCOMPARE(h.manager().deferScheduledRequest(reply), SchedulerCommandResult::Applied);
    } else if (state == 0) {
        QTRY_VERIFY(reply->isRunning());
    }
    QSignalSpy cancelled(&h.manager(), &QCNetworkAccessManager::schedulerRequestCancelled);
    reply->cancel();
    QTRY_COMPARE(cancelled.count(), 1);
    QCOMPARE(h.manager().cancelScheduledRequest(reply), SchedulerCommandResult::NotTracked);
    QCOMPARE(h.manager().schedulerStatistics().cancelledRequests(), 1);
    QCOMPARE(h.manager().schedulerStatistics().pendingRequests(), 0);
    delete reply;
    QCOMPARE(h.manager().schedulerStatistics().cancelledRequests(), 1);
}

void tst_QCNetworkScheduler::testRequestQueuedReplyDeletionStopsContinuation()
{
    SchedulerHarness h;
    QSignalSpy empty(&h.manager(), &QCNetworkAccessManager::schedulerPendingQueueEmpty);
    connect(&h.manager(),
            &QCNetworkAccessManager::schedulerRequestQueued,
            this,
            [](QCNetworkReply *reply) { delete reply; });
    QCOMPARE(h.get(QUrl(QStringLiteral("http://delete-queued.test/"))), nullptr);
    QCOMPARE(h.manager().schedulerStatistics().pendingRequests(), 0);
    QCOMPARE(h.manager().schedulerStatistics().runningRequests(), 0);
    QCOMPARE(empty.count(), 1);
    QCoreApplication::sendPostedEvents();
    QCOMPARE(h.mock().capturedRequests().size(), 0);
}

void tst_QCNetworkScheduler::testFinishedDeletionPreservesTerminalAccounting()
{
    SchedulerHarness h;
    QPointer<QCNetworkReply> reply = h.get(QUrl(QStringLiteral("http://finished-delete.test/")));
    connect(reply.data(), &QCNetworkReply::finished, this, [reply]() { delete reply.data(); });
    QTRY_VERIFY(reply.isNull());
    QCOMPARE(h.manager().schedulerStatistics().completedRequests(), 1);
    QCOMPARE(h.manager().schedulerStatistics().totalBytesReceived(), qint64(2));
    QCOMPARE(h.manager().schedulerStatistics().runningRequests(), 0);
    QCoreApplication::sendPostedEvents();
    QCOMPARE(h.manager().schedulerStatistics().completedRequests(), 1);
}

void tst_QCNetworkScheduler::testRequestQueuedManagerDeletionStopsContinuation()
{
    QObject owner;
    QCNetworkMockHandler mock;
    auto *manager = makeManager(&owner, &mock);
    QPointer<QCNetworkAccessManager> guard(manager);
    connect(manager, &QCNetworkAccessManager::schedulerRequestQueued, &owner, [manager]() {
        delete manager;
    });
    QCOMPARE(get(manager, mock, "delete-manager"), nullptr);
    QVERIFY(guard.isNull());
    QCoreApplication::sendPostedEvents();
    QCOMPARE(mock.capturedRequests().size(), 0);
}

void tst_QCNetworkScheduler::testUndeferRequestQueuedManagerDeletionStopsContinuation()
{
    QObject owner;
    QCNetworkMockHandler mock;
    auto *manager = makeManager(&owner, &mock);
    get(manager, mock, "blocker");
    QPointer<QCNetworkReply> reply = get(manager, mock, "deferred");
    QCOMPARE(manager->deferScheduledRequest(reply.data()), SchedulerCommandResult::Applied);
    QPointer<QCNetworkAccessManager> guard(manager);
    connect(manager, &QCNetworkAccessManager::schedulerRequestQueued, &owner, [manager]() {
        delete manager;
    });
    QCOMPARE(manager->undeferScheduledRequest(reply.data()), SchedulerCommandResult::Applied);
    QVERIFY(guard.isNull());
    QVERIFY(reply.isNull());
}

void tst_QCNetworkScheduler::testCancelledManagerDeletionStopsContinuation()
{
    for (bool direct : {false, true}) {
        QObject owner;
        QCNetworkMockHandler mock;
        auto *manager = makeManager(&owner, &mock);
        auto *reply   = get(manager, mock, "cancelled");
        QPointer<QCNetworkAccessManager> guard(manager);
        connect(manager, &QCNetworkAccessManager::schedulerRequestCancelled, &owner, [manager]() {
            delete manager;
        });
        if (direct) {
            reply->cancel();
        } else {
            QCOMPARE(manager->cancelScheduledRequest(reply), SchedulerCommandResult::Applied);
        }
        QTRY_VERIFY(guard.isNull());
    }
}

void tst_QCNetworkScheduler::testAboutToStartDestruction_data()
{
    QTest::addColumn<bool>("destroyManager");
    QTest::newRow("reply") << false;
    QTest::newRow("manager") << true;
}

void tst_QCNetworkScheduler::testAboutToStartDestruction()
{
    QFETCH(bool, destroyManager);
    QObject owner;
    QCNetworkMockHandler mock;
    auto *manager = makeManager(&owner, &mock);
    QSignalSpy started(manager, &QCNetworkAccessManager::schedulerRequestStarted);
    connect(manager,
            &QCNetworkAccessManager::schedulerRequestAboutToStart,
            &owner,
            [manager, destroyManager](QCNetworkReply *reply) {
                if (destroyManager) {
                    delete manager;
                } else {
                    delete reply;
                }
            });
    QPointer<QCNetworkReply> reply = get(manager, mock, "about-to-start");
    QTRY_VERIFY(reply.isNull());
    QCOMPARE(started.count(), 0);
    QCOMPARE(mock.capturedRequests().size(), 0);
}

void tst_QCNetworkScheduler::testExecutionBoundaryDestruction_data()
{
    testAboutToStartDestruction_data();
}

void tst_QCNetworkScheduler::testExecutionBoundaryDestruction()
{
    QFETCH(bool, destroyManager);
    QObject owner;
    QCNetworkMockHandler mock;
    auto *manager = makeManager(&owner, &mock);
    QSignalSpy started(manager, &QCNetworkAccessManager::schedulerRequestStarted);
    QPointer<QCNetworkReply> reply = get(manager, mock, "execute-boundary");
    connect(reply.data(),
            &QCNetworkReply::stateChanged,
            &owner,
            [manager, reply, destroyManager](ReplyState state) {
                if (state != ReplyState::Running) {
                    return;
                }
                if (destroyManager) {
                    delete manager;
                } else {
                    delete reply.data();
                }
            });
    QTRY_VERIFY(reply.isNull());
    // Mock 已进入 execute，但在提交后的生存复查失败时不能发出 Started。
    QCOMPARE(mock.capturedRequests().size(), 1);
    QCOMPARE(started.count(), 0);
}
