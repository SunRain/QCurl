// SPDX-License-Identifier: MIT
// Copyright (c) 2025 QCurl Project

#include "qcnetwork_scheduler_test.h"

#include <QPointer>

using namespace QCurl;

void tst_QCNetworkScheduler::testQueueEmptySignal()
{
    SchedulerHarness h;
    QVERIFY(h.configure());
    h.mock().setGlobalDelay(2000);
    h.occupySlot();
    QSignalSpy empty(&h.manager(), &QCNetworkAccessManager::schedulerPendingQueueEmpty);
    QVERIFY(h.manager().setSchedulerPolicy(h.manager().schedulerPolicy()));
    QCOMPARE(empty.count(), 0);
    auto *reply = h.get(QUrl(QStringLiteral("http://empty.test/")));
    QCOMPARE(h.manager().deferScheduledRequest(reply), SchedulerCommandResult::Applied);
    QCOMPARE(empty.count(), 1);
    QCOMPARE(h.manager().schedulerStatistics().runningRequests(), 1);
    QVERIFY(h.manager().setSchedulerPolicy(h.manager().schedulerPolicy()));
    QCOMPARE(empty.count(), 1);
    QCOMPARE(h.manager().undeferScheduledRequest(reply), SchedulerCommandResult::Applied);
    QCOMPARE(h.manager().cancelScheduledRequest(reply), SchedulerCommandResult::Applied);
    QCOMPARE(empty.count(), 2);
    QCOMPARE(h.manager().cancelScheduledRequest(reply), SchedulerCommandResult::NotTracked);
    QVERIFY(h.manager().setSchedulerPolicy(h.manager().schedulerPolicy()));
    QCOMPARE(empty.count(), 2);
}

void tst_QCNetworkScheduler::testQueuedObserverUsesCapturedGuardAndValueSnapshot()
{
    SchedulerHarness h;
    QVERIFY(h.configure());
    h.occupySlot();
    QObject observer;
    QPointer<QCNetworkReply> retained;
    QCNetworkLaneKey savedLane;
    QString savedOrigin;
    QCNetworkRequestPriority savedPriority = QCNetworkRequestPriority::VeryLow;
    bool delivered                         = false;
    connect(&h.manager(),
            &QCNetworkAccessManager::schedulerRequestQueued,
            &observer,
            [&](QCNetworkReply *reply,
                const QCNetworkLaneKey &lane,
                const QString &origin,
                QCNetworkRequestPriority priority) {
                // 在有效的 owner-thread 同步回调中建立 guard，不在 queued 收件时补救裸指针。
                retained = reply;
                QMetaObject::invokeMethod(
                    &observer,
                    [&, guard = QPointer<QCNetworkReply>(reply), lane, origin, priority]() {
                        QVERIFY(guard.isNull());
                        savedLane     = lane;
                        savedOrigin   = origin;
                        savedPriority = priority;
                        delivered     = true;
                    },
                    Qt::QueuedConnection);
            });
    auto *reply = h.get(QUrl(QStringLiteral("http://snapshot.test/")),
                        QCNetworkRequestPriority::Low,
                        QCNetworkLaneKey::control());
    QCOMPARE(h.manager().setScheduledRequestPriority(reply, QCNetworkRequestPriority::Critical),
             SchedulerCommandResult::Applied);
    delete reply;
    QVERIFY(retained.isNull());
    QTRY_VERIFY(delivered);
    QCOMPARE(savedLane, QCNetworkLaneKey::control());
    QCOMPARE(savedOrigin, QStringLiteral("http://snapshot.test:80"));
    QCOMPARE(int(savedPriority), int(QCNetworkRequestPriority::Low));
}

void tst_QCNetworkScheduler::testQueuedObserverCannotVetoStart()
{
    SchedulerHarness h;
    QSignalSpy started(&h.manager(), &QCNetworkAccessManager::schedulerRequestStarted);
    bool delivered = false;
    QPointer<QCNetworkReply> guard;
    connect(
        &h.manager(),
        &QCNetworkAccessManager::schedulerRequestAboutToStart,
        this,
        [&](QCNetworkReply *, const QCNetworkLaneKey &, const QString &, QCNetworkRequestPriority) {
            QVERIFY(guard);
            QCOMPARE(started.count(), 1);
            QCOMPARE(h.mock().capturedRequests().size(), 1);
            QCOMPARE(h.manager().cancelScheduledRequest(guard.data()),
                     SchedulerCommandResult::Applied);
            delivered = true;
        },
        Qt::QueuedConnection);
    guard = h.get(QUrl(QStringLiteral("http://queued-observer.test/")));
    QTRY_VERIFY(delivered);
    QCOMPARE(started.count(), 1);
}
