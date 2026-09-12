// SPDX-License-Identifier: MIT
// Copyright (c) 2025 QCurl Project

#include "qcnetwork_scheduler_test.h"

#include <QScopeGuard>
#include <QThread>

#include <array>
#include <thread>

using namespace QCurl;

namespace {

void verifyStatisticsUnchanged(const QCNetworkSchedulerStatistics &actual,
                               const QCNetworkSchedulerStatistics &before)
{
    QCOMPARE(actual.pendingRequests(), before.pendingRequests());
    QCOMPARE(actual.runningRequests(), before.runningRequests());
    QCOMPARE(actual.cancelledRequests(), before.cancelledRequests());
    QCOMPARE(actual.completedRequests(), before.completedRequests());
    QCOMPARE(actual.totalBytesReceived(), before.totalBytesReceived());
    QCOMPARE(actual.totalBytesSent(), before.totalBytesSent());
    QCOMPARE(actual.avgResponseTime(), before.avgResponseTime());
}

void verifyUntracked(QCNetworkAccessManager &manager, QCNetworkReply *reply)
{
    QCOMPARE(manager.deferScheduledRequest(reply), SchedulerCommandResult::NotTracked);
    QCOMPARE(manager.undeferScheduledRequest(reply), SchedulerCommandResult::NotTracked);
    QCOMPARE(manager.cancelScheduledRequest(reply), SchedulerCommandResult::NotTracked);
    QCOMPARE(manager.setScheduledRequestPriority(reply, QCNetworkRequestPriority::Critical),
             SchedulerCommandResult::NotTracked);
}

} // namespace

void tst_QCNetworkScheduler::testDeferUndefer()
{
    SchedulerHarness h;
    QVERIFY(h.configure());
    auto *blocker = h.occupySlot();
    auto *reply = h.get(QUrl(QStringLiteral("http://defer.test/")), QCNetworkRequestPriority::High);
    QSignalSpy queued(&h.manager(), &QCNetworkAccessManager::schedulerRequestQueued);
    QCOMPARE(h.manager().deferScheduledRequest(reply), SchedulerCommandResult::Applied);
    QCOMPARE(h.manager().deferScheduledRequest(reply), SchedulerCommandResult::InvalidState);
    QCOMPARE(h.manager().schedulerStatistics().pendingRequests(), 0);
    QCOMPARE(h.manager().cancelScheduledRequest(blocker), SchedulerCommandResult::Applied);
    QCOMPARE(h.manager().schedulerStatistics().runningRequests(), 0);
    QVERIFY(!reply->isFinished());
    QCOMPARE(h.manager().undeferScheduledRequest(reply), SchedulerCommandResult::Applied);
    QCOMPARE(h.manager().undeferScheduledRequest(reply), SchedulerCommandResult::InvalidState);
    QCOMPARE(queued.count(), 1);
    QCOMPARE(int(qvariant_cast<QCNetworkRequestPriority>(queued.at(0).at(3))),
             int(QCNetworkRequestPriority::High));
    QCOMPARE(h.manager().deferScheduledRequest(reply), SchedulerCommandResult::InvalidState);
    QTRY_VERIFY(reply->isFinished());
}

void tst_QCNetworkScheduler::testChangePriority()
{
    SchedulerHarness h;
    QVERIFY(h.configure());
    auto *blocker = h.occupySlot();
    auto *low     = h.get(QUrl(QStringLiteral("http://priority.test/low")),
                          QCNetworkRequestPriority::Low);
    h.get(QUrl(QStringLiteral("http://priority.test/normal")));
    QSignalSpy queued(&h.manager(), &QCNetworkAccessManager::schedulerRequestQueued);
    QSignalSpy changed(&h.manager(), &QCNetworkAccessManager::schedulerRequestPriorityChanged);
    QSignalSpy started(&h.manager(), &QCNetworkAccessManager::schedulerRequestStarted);
    QCOMPARE(h.manager().setScheduledRequestPriority(low, QCNetworkRequestPriority::Low),
             SchedulerCommandResult::NoChange);
    QCOMPARE(h.manager().setScheduledRequestPriority(low, static_cast<QCNetworkRequestPriority>(99)),
             SchedulerCommandResult::InvalidArgument);
    QCOMPARE(changed.count(), 0);
    QCOMPARE(h.manager().setScheduledRequestPriority(low, QCNetworkRequestPriority::Critical),
             SchedulerCommandResult::Applied);
    QCOMPARE(changed.count(), 1);
    QCOMPARE(queued.count(), 0);
    QCOMPARE(h.manager().cancelScheduledRequest(blocker), SchedulerCommandResult::Applied);
    QTRY_VERIFY(started.count() > 0);
    QCOMPARE(qvariant_cast<QCNetworkReply *>(started.at(0).at(0)), low);
}

void tst_QCNetworkScheduler::testCommandReentryKeepsCommitResult()
{
    SchedulerHarness h;
    QVERIFY(h.configure());
    h.occupySlot();
    auto *reply    = h.get(QUrl(QStringLiteral("http://reentry.test/")));
    bool reentered = false;
    connect(&h.manager(),
            &QCNetworkAccessManager::schedulerRequestPriorityChanged,
            this,
            [&](QCNetworkReply *changed) {
                QCOMPARE(h.manager().setScheduledRequestPriority(changed,
                                                                 QCNetworkRequestPriority::High),
                         SchedulerCommandResult::NoChange);
                QCOMPARE(h.manager().deferScheduledRequest(changed),
                         SchedulerCommandResult::Applied);
                QCOMPARE(h.manager().schedulerStatistics().pendingRequests(), 0);
                reentered = true;
            });
    QCOMPARE(h.manager().setScheduledRequestPriority(reply, QCNetworkRequestPriority::High),
             SchedulerCommandResult::Applied);
    QVERIFY(reentered);
    QCOMPARE(h.manager().setScheduledRequestPriority(reply, QCNetworkRequestPriority::Low),
             SchedulerCommandResult::InvalidState);
    connect(&h.manager(),
            &QCNetworkAccessManager::schedulerRequestQueued,
            this,
            [&](QCNetworkReply *queued) {
                QCOMPARE(h.manager().schedulerStatistics().pendingRequests(), 1);
                QCOMPARE(h.manager().deferScheduledRequest(queued), SchedulerCommandResult::Applied);
            });
    QCOMPARE(h.manager().undeferScheduledRequest(reply), SchedulerCommandResult::Applied);
    QCOMPARE(h.manager().schedulerStatistics().pendingRequests(), 0);
}

void tst_QCNetworkScheduler::testSchedulerCommandResultFailuresAreSideEffectFree()
{
    SchedulerHarness h;
    SchedulerHarness foreign;
    QVERIFY(h.configure());
    QVERIFY(foreign.configure());
    h.occupySlot();
    foreign.occupySlot();
    auto *reply            = h.get(QUrl(QStringLiteral("http://owned.test/")));
    auto *other            = foreign.get(QUrl(QStringLiteral("http://foreign.test/")));
    const auto before      = h.manager().schedulerStatistics();
    const auto otherBefore = foreign.manager().schedulerStatistics();
    QSignalSpy cancelled(&foreign.manager(), &QCNetworkAccessManager::schedulerRequestCancelled);
    QCOMPARE(h.manager().deferScheduledRequest(nullptr), SchedulerCommandResult::NullReply);
    QCOMPARE(h.manager().undeferScheduledRequest(nullptr), SchedulerCommandResult::NullReply);
    QCOMPARE(h.manager().cancelScheduledRequest(nullptr), SchedulerCommandResult::NullReply);
    QCOMPARE(h.manager().setScheduledRequestPriority(nullptr,
                                                     static_cast<QCNetworkRequestPriority>(99)),
             SchedulerCommandResult::NullReply);
    verifyUntracked(h.manager(), other);
    verifyUntracked(foreign.manager(), reply);
    QCOMPARE(h.manager().setScheduledRequestPriority(other,
                                                     static_cast<QCNetworkRequestPriority>(99)),
             SchedulerCommandResult::InvalidArgument);
    verifyStatisticsUnchanged(h.manager().schedulerStatistics(), before);
    verifyStatisticsUnchanged(foreign.manager().schedulerStatistics(), otherBefore);
    QCOMPARE(cancelled.count(), 0);
    h.manager().enableRequestScheduler(false);
    auto *untracked = h.get(QUrl(QStringLiteral("http://same-parent.test/")));
    h.manager().enableRequestScheduler(true);
    QCOMPARE(untracked->parent(), &h.manager());
    verifyUntracked(h.manager(), untracked);
    QCOMPARE(h.manager().cancelScheduledRequest(reply), SchedulerCommandResult::Applied);
    verifyUntracked(h.manager(), reply);
}

void tst_QCNetworkScheduler::testSchedulerCommandResultRejectsAffinityMismatch()
{
    SchedulerHarness h;
    QVERIFY(h.configure());
    h.occupySlot();
    auto *reply = h.get(QUrl(QStringLiteral("http://affinity.test/")));
    QThread worker;
    QObject context;
    QThread *owner = QThread::currentThread();
    reply->setParent(nullptr);
    reply->moveToThread(&worker);
    context.moveToThread(&worker);
    QCOMPARE(reply->thread(), &worker);
    QCOMPARE(context.thread(), &worker);
    worker.start();
    const auto restore = qScopeGuard([&]() {
        QMetaObject::invokeMethod(
            &context,
            [&]() {
                reply->moveToThread(owner);
                context.moveToThread(owner);
            },
            Qt::BlockingQueuedConnection);
        worker.quit();
        worker.wait();
        reply->setParent(&h.manager());
    });
    const auto before  = h.manager().schedulerStatistics();
    QCOMPARE(h.manager().deferScheduledRequest(reply),
             SchedulerCommandResult::ThreadAffinityMismatch);
    QCOMPARE(h.manager().undeferScheduledRequest(reply),
             SchedulerCommandResult::ThreadAffinityMismatch);
    QCOMPARE(h.manager().cancelScheduledRequest(reply),
             SchedulerCommandResult::ThreadAffinityMismatch);
    QCOMPARE(h.manager().setScheduledRequestPriority(reply, QCNetworkRequestPriority::High),
             SchedulerCommandResult::ThreadAffinityMismatch);
    verifyStatisticsUnchanged(h.manager().schedulerStatistics(), before);
}

void tst_QCNetworkScheduler::testCrossThreadSchedulerCommandsAreRejectedWithoutMutation()
{
    SchedulerHarness h;
    QVERIFY(h.configure());
    h.occupySlot();
    auto *reply       = h.get(QUrl(QStringLiteral("http://wrong-thread.test/")));
    const auto before = h.manager().schedulerStatistics();
    std::array<SchedulerCommandResult, 6> results;
    QSignalSpy cancelled(&h.manager(), &QCNetworkAccessManager::schedulerRequestCancelled);
    QCNetworkLaneCancelResult laneResult;
    const auto policy  = h.manager().schedulerPolicy();
    bool policyApplied = true;
    std::thread thread([&]() {
        results[0] = h.manager().deferScheduledRequest(reply);
        results[1] = h.manager().undeferScheduledRequest(reply);
        results[2] = h.manager().cancelScheduledRequest(reply);
        results[3] = h.manager().setScheduledRequestPriority(reply, QCNetworkRequestPriority::High);
        results[4] = h.manager().cancelScheduledRequest(nullptr);
        results[5] = h.manager().setScheduledRequestPriority(nullptr,
                                                             static_cast<QCNetworkRequestPriority>(
                                                                 99));
        laneResult = h.manager().cancelLaneRequests(
            QCNetworkLaneKey::defaultLane(),
            QCNetworkAccessManager::SchedulerCancelScope::PendingAndRunning);
        policyApplied = h.manager().setSchedulerPolicy(policy);
    });
    thread.join();
    for (auto result : results) {
        QCOMPARE(result, SchedulerCommandResult::WrongThread);
    }
    QCOMPARE(laneResult.status(), QCNetworkLaneCancelResult::Status::NonOwnerThread);
    QVERIFY(!policyApplied);
    verifyStatisticsUnchanged(h.manager().schedulerStatistics(), before);
    QCOMPARE(cancelled.count(), 0);
}

void tst_QCNetworkScheduler::testManagerCancelLaneRequestsReturnsStructuredFailClosedResult()
{
    SchedulerHarness h;
    QVERIFY(h.configure());
    h.occupySlot();
    auto *pending     = h.get(QUrl(QStringLiteral("http://cancel-lane.test/")));
    const auto before = h.manager().schedulerStatistics();
    const auto scope  = QCNetworkAccessManager::SchedulerCancelScope::PendingOnly;
    QCOMPARE(h.manager().cancelLaneRequests({}, scope).status(),
             QCNetworkLaneCancelResult::Status::InvalidLane);
    QCNetworkLaneKey missing;
    QVERIFY(QCNetworkLaneKey::fromName(QStringLiteral("Missing"), &missing));
    QCOMPARE(h.manager().cancelLaneRequests(missing, scope).status(),
             QCNetworkLaneCancelResult::Status::UnregisteredLane);
    QCOMPARE(h.manager()
                 .cancelLaneRequests(QCNetworkLaneKey::control(),
                                     static_cast<QCNetworkAccessManager::SchedulerCancelScope>(99))
                 .status(),
             QCNetworkLaneCancelResult::Status::InvalidScope);
    const auto empty = h.manager().cancelLaneRequests(QCNetworkLaneKey::control(), scope);
    QVERIFY(empty.isSuccess());
    QCOMPARE(empty.cancelledRequests(), 0);
    h.manager().enableRequestScheduler(false);
    QCOMPARE(h.manager().cancelLaneRequests(QCNetworkLaneKey::defaultLane(), scope).status(),
             QCNetworkLaneCancelResult::Status::SchedulerDisabled);
    verifyStatisticsUnchanged(h.manager().schedulerStatistics(), before);
    QVERIFY(!pending->isFinished());
}

void tst_QCNetworkScheduler::testCancelLaneRequests()
{
    SchedulerHarness h;
    QVERIFY(h.configure());
    auto *running  = h.get(QUrl(QStringLiteral("http://lane.test/running")),
                           QCNetworkRequestPriority::Normal,
                           QCNetworkLaneKey::control());
    auto *pending  = h.get(QUrl(QStringLiteral("http://lane.test/pending")),
                           QCNetworkRequestPriority::Normal,
                           QCNetworkLaneKey::control());
    auto *deferred = h.get(QUrl(QStringLiteral("http://lane.test/deferred")),
                           QCNetworkRequestPriority::Normal,
                           QCNetworkLaneKey::control());
    QCOMPARE(h.manager().deferScheduledRequest(deferred), SchedulerCommandResult::Applied);
    auto *other = h.get(QUrl(QStringLiteral("http://lane.test/other")),
                        QCNetworkRequestPriority::Normal,
                        QCNetworkLaneKey::transfer());
    const auto first
        = h.manager().cancelLaneRequests(QCNetworkLaneKey::control(),
                                         QCNetworkAccessManager::SchedulerCancelScope::PendingOnly);
    QVERIFY(first.isSuccess());
    QCOMPARE(first.cancelledRequests(), 2);
    QVERIFY(!running->isFinished() && !other->isFinished());
    QCOMPARE(h.manager().schedulerStatistics().runningRequests(), 1);
    const auto second = h.manager().cancelLaneRequests(
        QCNetworkLaneKey::control(),
        QCNetworkAccessManager::SchedulerCancelScope::PendingAndRunning);
    QCOMPARE(second.cancelledRequests(), 1);
    QCOMPARE(h.manager().schedulerStatistics().cancelledRequests(), 3);
    QTRY_VERIFY(running->isFinished() && pending->isFinished() && deferred->isFinished());
    QCOMPARE(other->error(), NetworkError::NoError);
}
