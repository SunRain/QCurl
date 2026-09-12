// SPDX-License-Identifier: MIT
// Copyright (c) 2025 QCurl Project

#include "qcnetwork_scheduler_test.h"

using namespace QCurl;

void tst_QCNetworkScheduler::testSchedulerEnabled()
{
    QCNetworkAccessManager manager;
    QVERIFY(!manager.isSchedulerEnabled());
    manager.enableRequestScheduler(true);
    QVERIFY(manager.isSchedulerEnabled());
    manager.enableRequestScheduler(false);
    QVERIFY(!manager.isSchedulerEnabled());
}

void tst_QCNetworkScheduler::testPriorityMetatypeContract()
{
    SchedulerHarness h;
    const auto byName = QMetaType::fromName("QCurl::QCNetworkRequestPriority");
    QCOMPARE(byName, QMetaType::fromType<QCNetworkRequestPriority>());
    QCOMPARE(QMetaType::fromName("QCurl::QCNetworkLaneKey"),
             QMetaType::fromType<QCNetworkLaneKey>());
    QCOMPARE(QMetaType::fromName("QCurl::SchedulerCommandResult"),
             QMetaType::fromType<SchedulerCommandResult>());
    QCNetworkLaneKey saved;
    QCNetworkRequestPriority priority = QCNetworkRequestPriority::VeryLow;
    connect(
        &h.manager(),
        &QCNetworkAccessManager::schedulerRequestQueued,
        this,
        [&](QCNetworkReply *,
            const QCNetworkLaneKey &lane,
            const QString &,
            QCNetworkRequestPriority value) {
            saved    = lane;
            priority = value;
        },
        Qt::QueuedConnection);
    h.get(QUrl(QStringLiteral("http://metatype.test/")),
          QCNetworkRequestPriority::High,
          QCNetworkLaneKey::control());
    QTRY_COMPARE(saved, QCNetworkLaneKey::control());
    QCOMPARE(int(priority), int(QCNetworkRequestPriority::High));
}

void tst_QCNetworkScheduler::testManagerLevelSchedulerPolicyConfiguresAdmission()
{
    SchedulerHarness h;
    QVERIFY(h.configure(1, 1));
    auto policy = h.manager().schedulerPolicy();
    QCNetworkSchedulerPolicy::LaneConfig control;
    control.setWeight(3);
    control.setReservedGlobal(1);
    QVERIFY(policy.setLaneConfig(QCNetworkLaneKey::control(), control));
    QVERIFY(h.manager().setSchedulerPolicy(policy));
    QVERIFY(h.manager().schedulerPolicy() == policy);
    auto *first  = h.get(QUrl(QStringLiteral("http://policy.test/first")),
                         QCNetworkRequestPriority::Normal,
                         QCNetworkLaneKey::control());
    auto *second = h.get(QUrl(QStringLiteral("http://policy.test/second")),
                         QCNetworkRequestPriority::Normal,
                         QCNetworkLaneKey::control());
    QCOMPARE(h.manager().schedulerStatistics().runningRequests(), 1);
    QCOMPARE(h.manager().schedulerStatistics().pendingRequests(), 1);
    const auto cancelled = h.manager().cancelLaneRequests(
        QCNetworkLaneKey::control(),
        QCNetworkAccessManager::SchedulerCancelScope::PendingAndRunning);
    QVERIFY(cancelled.isSuccess());
    QCOMPARE(cancelled.cancelledRequests(), 2);
    QTRY_VERIFY(first->isFinished() && second->isFinished());
}

void tst_QCNetworkScheduler::testUnknownLaneFailsClosed()
{
    SchedulerHarness h;
    QCNetworkLaneKey missing;
    QVERIFY(QCNetworkLaneKey::fromName(QStringLiteral("Missing"), &missing));
    QSignalSpy queued(&h.manager(), &QCNetworkAccessManager::schedulerRequestQueued);
    auto *reply = h.get(QUrl(QStringLiteral("http://unknown.test/")),
                        QCNetworkRequestPriority::Normal,
                        missing);
    QTRY_VERIFY(reply->isFinished());
    QCOMPARE(reply->error(), NetworkError::InvalidRequest);
    QCOMPARE(queued.count(), 0);
    QCOMPARE(h.mock().capturedRequests().size(), 0);
    QCOMPARE(h.manager().schedulerStatistics().pendingRequests(), 0);
}

void tst_QCNetworkScheduler::testInvalidSchedulerPriorityFailsClosed()
{
    SchedulerHarness h;
    auto *reply = h.get(QUrl(QStringLiteral("http://invalid.test/")),
                        static_cast<QCNetworkRequestPriority>(99));
    QTRY_VERIFY(reply->isFinished());
    QCOMPARE(reply->error(), NetworkError::InvalidRequest);
    QCOMPARE(h.mock().capturedRequests().size(), 0);
    QCOMPARE(h.manager().schedulerStatistics().runningRequests(), 0);
}

void tst_QCNetworkScheduler::testStatistics()
{
    SchedulerHarness h;
    QVERIFY(h.configure());
    h.get(QUrl(QStringLiteral("http://stats.test/one")));
    h.get(QUrl(QStringLiteral("http://stats.test/two")));
    QTRY_COMPARE(h.manager().schedulerStatistics().completedRequests(), 2);
    const auto stats = h.manager().schedulerStatistics();
    QCOMPARE(stats.runningRequests(), 0);
    QCOMPARE(stats.pendingRequests(), 0);
    QCOMPARE(stats.cancelledRequests(), 0);
    QCOMPARE(stats.totalBytesReceived(), qint64(4));
    QVERIFY(stats.avgResponseTime() >= 0);
    auto *cancelled = h.get(QUrl(QStringLiteral("http://stats.test/cancelled")));
    QCOMPARE(h.manager().cancelScheduledRequest(cancelled), SchedulerCommandResult::Applied);
    QCOMPARE(h.manager().schedulerStatistics().cancelledRequests(), 1);
    QCOMPARE(stats.cancelledRequests(), 0);
}

QTEST_GUILESS_MAIN(tst_QCNetworkScheduler)
