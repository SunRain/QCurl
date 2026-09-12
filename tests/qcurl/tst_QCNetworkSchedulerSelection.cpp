// SPDX-License-Identifier: MIT
// Copyright (c) 2025 QCurl Project

#include "qcnetwork_scheduler_test.h"

using namespace QCurl;

void tst_QCNetworkScheduler::testPriorityQueueOrdering()
{
    SchedulerHarness h;
    QVERIFY(h.configure());
    QSignalSpy started(&h.manager(), &QCNetworkAccessManager::schedulerRequestStarted);
    auto *low      = h.get(QUrl(QStringLiteral("http://priority.test/low")),
                           QCNetworkRequestPriority::Low);
    auto *normal   = h.get(QUrl(QStringLiteral("http://priority.test/normal")));
    auto *critical = h.get(QUrl(QStringLiteral("http://priority.test/critical")),
                           QCNetworkRequestPriority::Critical);
    QCOMPARE(h.manager().schedulerStatistics().runningRequests(), 1);
    QTRY_COMPARE(started.count(), 3);
    QCOMPARE(qvariant_cast<QCNetworkReply *>(started.at(0).at(0)), low);
    QCOMPARE(qvariant_cast<QCNetworkReply *>(started.at(1).at(0)), critical);
    QCOMPARE(qvariant_cast<QCNetworkReply *>(started.at(2).at(0)), normal);
}

void tst_QCNetworkScheduler::testConcurrentRequestLimit()
{
    SchedulerHarness h;
    QVERIFY(h.configure(2, 2));
    for (int index = 0; index < 5; ++index) {
        h.get(QUrl(QStringLiteral("http://concurrent%1.test/").arg(index)));
    }
    QCOMPARE(h.manager().schedulerStatistics().runningRequests(), 2);
    QCOMPARE(h.manager().schedulerStatistics().pendingRequests(), 3);
}

void tst_QCNetworkScheduler::testPerHostLimit()
{
    SchedulerHarness h;
    QVERIFY(h.configure(5, 1));
    for (int index = 0; index < 3; ++index) {
        h.get(QUrl(QStringLiteral("http://same.test/%1").arg(index)));
    }
    QCOMPARE(h.manager().schedulerStatistics().runningRequests(), 1);
    QCOMPARE(h.manager().schedulerStatistics().pendingRequests(), 2);
}

void tst_QCNetworkScheduler::testLaneReservationGating()
{
    SchedulerHarness h;
    QVERIFY(h.configure());
    auto policy = h.manager().schedulerPolicy();
    QCNetworkSchedulerPolicy::LaneConfig lane;
    lane.setReservedGlobal(1);
    lane.setReservedPerHost(1);
    QVERIFY(policy.setLaneConfig(QCNetworkLaneKey::control(), lane));
    QVERIFY(h.manager().setSchedulerPolicy(policy));
    auto *blocker = h.occupySlot();
    h.get(QUrl(QStringLiteral("http://reservation.test/transfer")),
          QCNetworkRequestPriority::Critical,
          QCNetworkLaneKey::transfer());
    auto *control = h.get(QUrl(QStringLiteral("http://reservation.test/control")),
                          QCNetworkRequestPriority::Low,
                          QCNetworkLaneKey::control());
    QSignalSpy started(&h.manager(), &QCNetworkAccessManager::schedulerRequestStarted);
    QCOMPARE(h.manager().cancelScheduledRequest(blocker), SchedulerCommandResult::Applied);
    QTRY_VERIFY(started.count() > 0);
    QCOMPARE(qvariant_cast<QCNetworkReply *>(started.at(0).at(0)), control);
}

void tst_QCNetworkScheduler::testWeightedFairnessByLane()
{
    SchedulerHarness h;
    h.mock().setGlobalDelay(5);
    QVERIFY(h.configure());
    auto policy = h.manager().schedulerPolicy();
    QCNetworkSchedulerPolicy::LaneConfig lane;
    lane.setWeight(3);
    QVERIFY(policy.setLaneConfig(QCNetworkLaneKey::control(), lane));
    QVERIFY(h.manager().setSchedulerPolicy(policy));
    auto *blocker = h.occupySlot();
    for (int index = 0; index < 8; ++index) {
        h.get(QUrl(QStringLiteral("http://fair.test/%1").arg(index)),
              QCNetworkRequestPriority::Normal,
              index < 6 ? QCNetworkLaneKey::control() : QCNetworkLaneKey::transfer());
    }
    QSignalSpy started(&h.manager(), &QCNetworkAccessManager::schedulerRequestStarted);
    QCOMPARE(h.manager().cancelScheduledRequest(blocker), SchedulerCommandResult::Applied);
    QTRY_COMPARE(started.count(), 8);
    for (int index = 0; index < 8; ++index) {
        QCOMPARE(qvariant_cast<QCNetworkLaneKey>(started.at(index).at(1)),
                 index % 4 < 3 ? QCNetworkLaneKey::control() : QCNetworkLaneKey::transfer());
    }
}

void tst_QCNetworkScheduler::testPerHostHeadOfLineAvoidance()
{
    SchedulerHarness h;
    QVERIFY(h.configure(2, 1));
    auto *first   = h.get(QUrl(QStringLiteral("http://busy.test/first")));
    auto *waiting = h.get(QUrl(QStringLiteral("http://busy.test/second")));
    auto *other   = h.get(QUrl(QStringLiteral("http://free.test/")));
    QSignalSpy started(&h.manager(), &QCNetworkAccessManager::schedulerRequestStarted);
    QTRY_VERIFY(started.count() >= 2);
    QCOMPARE(qvariant_cast<QCNetworkReply *>(started.at(0).at(0)), first);
    QCOMPARE(qvariant_cast<QCNetworkReply *>(started.at(1).at(0)), other);
    QVERIFY(!waiting->isFinished());
}

void tst_QCNetworkScheduler::testHostKeyUsesOrigin()
{
    SchedulerHarness h;
    QVERIFY(h.configure(7, 1));
    const QStringList urls = {
        QStringLiteral("http://origin.test/path"),
        QStringLiteral("https://origin.test/path"),
        QStringLiteral("http://origin.test:8080/path"),
        QStringLiteral("https://origin.test:8443/path"),
        QStringLiteral("http://[2001:db8::1]/path"),
        QStringLiteral("http://origin.test:81/path"),
        QStringLiteral("https://[2001:db8::1]:9443/path"),
    };
    QSignalSpy queued(&h.manager(), &QCNetworkAccessManager::schedulerRequestQueued);
    for (const auto &url : urls) {
        h.get(QUrl(url));
    }
    QCOMPARE(h.manager().schedulerStatistics().runningRequests(), 7);
    QSet<QString> origins;
    for (const auto &event : queued) {
        origins.insert(event.at(2).toString());
    }
    QCOMPARE(origins,
             QSet<QString>({QStringLiteral("http://origin.test:80"),
                            QStringLiteral("https://origin.test:443"),
                            QStringLiteral("http://origin.test:8080"),
                            QStringLiteral("https://origin.test:8443"),
                            QStringLiteral("http://[2001:db8::1]:80"),
                            QStringLiteral("http://origin.test:81"),
                            QStringLiteral("https://[2001:db8::1]:9443")}));
}

void tst_QCNetworkScheduler::testReservationFallbackProgress()
{
    SchedulerHarness h;
    QVERIFY(h.configure(2, 1));
    auto policy = h.manager().schedulerPolicy();
    QCNetworkSchedulerPolicy::LaneConfig control;
    control.setReservedGlobal(2);
    QVERIFY(policy.setLaneConfig(QCNetworkLaneKey::control(), control));
    QVERIFY(h.manager().setSchedulerPolicy(policy));
    h.get(QUrl(QStringLiteral("http://occupied.test/one")));
    h.get(QUrl(QStringLiteral("http://occupied.test/control")),
          QCNetworkRequestPriority::Critical,
          QCNetworkLaneKey::control());
    auto *transfer = h.get(QUrl(QStringLiteral("http://available.test/")),
                           QCNetworkRequestPriority::Low,
                           QCNetworkLaneKey::transfer());
    QSignalSpy started(&h.manager(), &QCNetworkAccessManager::schedulerRequestStarted);
    QTRY_VERIFY(started.count() >= 2);
    QCOMPARE(qvariant_cast<QCNetworkReply *>(started.at(1).at(0)), transfer);
}
