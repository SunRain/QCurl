// SPDX-License-Identifier: MIT
// Copyright (c) 2025 QCurl Project

#include "private/QCNetworkRequestSchedulerTestAccess_p.h"
#include "qcnetwork_scheduler_test.h"

#include <QScopeGuard>
#include <QTimer>

#include <thread>

using namespace QCurl;

void tst_QCNetworkScheduler::testSchedulerConstructionDoesNotStartThrottleTimer()
{
    QCNetworkAccessManager manager;
    auto *timer = QCNetworkRequestSchedulerTestAccess::admissionTimer(manager);
    QVERIFY(!timer->isActive());
    auto policy = manager.schedulerPolicy();
    policy.setAdmissionByteBudget(100);
    QVERIFY(manager.setSchedulerPolicy(policy));
    QVERIFY(!timer->isActive());
    QVERIFY(timer->isSingleShot());
}

void tst_QCNetworkScheduler::testBandwidthWindowUsesProgressDeltas()
{
    const QByteArray previous = qgetenv("QCURL_TEST_MOCK_CHAOS");
    const auto restore        = qScopeGuard([previous]() {
        if (previous.isNull()) {
            qunsetenv("QCURL_TEST_MOCK_CHAOS");
        } else {
            qputenv("QCURL_TEST_MOCK_CHAOS", previous);
        }
    });
    qputenv("QCURL_TEST_MOCK_CHAOS",
            QByteArrayLiteral("seed=17;max_chunk_bytes=12;chunk_delay_ms=20"));
    SchedulerHarness h;
    QVERIFY(h.configure());
    auto policy = h.manager().schedulerPolicy();
    policy.setAdmissionByteBudget(40);
    QVERIFY(h.manager().setSchedulerPolicy(policy));
    const QByteArray payload("ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789");
    QSignalSpy started(&h.manager(), &QCNetworkAccessManager::schedulerRequestStarted);
    QSignalSpy wakeups(QCNetworkRequestSchedulerTestAccess::admissionTimer(h.manager()),
                       &QTimer::timeout);
    h.get(QUrl(QStringLiteral("http://delta.test/one")),
          QCNetworkRequestPriority::Normal,
          QCNetworkLaneKey::defaultLane(),
          payload);
    h.get(QUrl(QStringLiteral("http://delta.test/two")),
          QCNetworkRequestPriority::Normal,
          QCNetworkLaneKey::defaultLane(),
          payload);
    QTRY_COMPARE_WITH_TIMEOUT(started.count(), 2, 700);
    QCOMPARE(wakeups.count(), 0);
    QTRY_COMPARE(h.manager().schedulerStatistics().completedRequests(), 2);
    QCOMPARE(h.manager().schedulerStatistics().totalBytesReceived(), qint64(payload.size() * 2));
}

void tst_QCNetworkScheduler::testProgressTrackingAutoConnectionAndTeardown()
{
    SchedulerHarness h;
    h.mock().setGlobalDelay(2000);
    auto *reply = h.get(QUrl(QStringLiteral("http://progress.test/")));
    Q_EMIT reply->downloadProgress(10, 100);
    Q_EMIT reply->downloadProgress(15, 100);
    Q_EMIT reply->uploadProgress(4, 100);
    QCOMPARE(QCNetworkRequestSchedulerTestAccess::progressWindow(h.manager()), qint64(19));
    QCNetworkRequestSchedulerTestAccess::rebindProgress(h.manager(), reply);
    Q_EMIT reply->downloadProgress(20, 100);
    QCOMPARE(QCNetworkRequestSchedulerTestAccess::progressWindow(h.manager()), qint64(24));
    std::thread emitter([reply]() { Q_EMIT reply->downloadProgress(29, 100); });
    emitter.join();
    QCOMPARE(QCNetworkRequestSchedulerTestAccess::progressWindow(h.manager()), qint64(24));
    QTRY_COMPARE(QCNetworkRequestSchedulerTestAccess::progressWindow(h.manager()), qint64(33));
    std::thread staleEmitter([reply]() { Q_EMIT reply->downloadProgress(35, 100); });
    staleEmitter.join();
    QCOMPARE(h.manager().cancelScheduledRequest(reply), SchedulerCommandResult::Applied);
    QCoreApplication::sendPostedEvents();
    Q_EMIT reply->downloadProgress(45, 100);
    QCOMPARE(QCNetworkRequestSchedulerTestAccess::progressWindow(h.manager()), qint64(33));
}

void tst_QCNetworkScheduler::testAdmissionWakeupOnlyWhileGating()
{
    SchedulerHarness h;
    QVERIFY(h.configure());
    auto policy = h.manager().schedulerPolicy();
    policy.setAdmissionByteBudget(10);
    QVERIFY(h.manager().setSchedulerPolicy(policy));
    auto *timer = QCNetworkRequestSchedulerTestAccess::admissionTimer(h.manager());
    QSignalSpy wakeups(timer, &QTimer::timeout);
    auto *blocker = h.occupySlot();
    Q_EMIT blocker->downloadProgress(10, 10);
    QVERIFY(!timer->isActive());
    auto *reply = h.get(QUrl(QStringLiteral("http://gated.test/")));
    // 此刻只有全局槽位不足，尚没有可被字节阈值阻挡的 admission。
    QVERIFY(!timer->isActive());
    QCOMPARE(h.manager().cancelScheduledRequest(blocker), SchedulerCommandResult::Applied);
    QVERIFY(timer->isActive());
    QCOMPARE(h.manager().schedulerStatistics().runningRequests(), 0);
    QCOMPARE(h.manager().schedulerStatistics().pendingRequests(), 1);
    QTRY_VERIFY_WITH_TIMEOUT(reply->isRunning(), 1600);
    QCOMPARE(wakeups.count(), 1);
    QVERIFY(!timer->isActive());
    QTRY_VERIFY(reply->isFinished());
    QVERIFY(!timer->isActive());
}

void tst_QCNetworkScheduler::testExpiredStartTicketCannotExecute()
{
    SchedulerHarness h;
    auto *reply = h.get(QUrl(QStringLiteral("http://expired-ticket.test/")));
    QCNetworkRequestSchedulerTestAccess::invalidateStartTicket(h.manager(), reply);
    QSignalSpy about(&h.manager(), &QCNetworkAccessManager::schedulerRequestAboutToStart);
    QSignalSpy started(&h.manager(), &QCNetworkAccessManager::schedulerRequestStarted);
    QCoreApplication::sendPostedEvents();
    QCOMPARE(about.count(), 0);
    QCOMPARE(started.count(), 0);
    QCOMPARE(h.mock().capturedRequests().size(), 0);
    QCOMPARE(h.manager().cancelScheduledRequest(reply), SchedulerCommandResult::Applied);
    QCOMPARE(h.manager().schedulerStatistics().runningRequests(), 0);
}

void tst_QCNetworkScheduler::testNoEventDispatcherRejectsAdmission()
{
    bool finished      = false;
    NetworkError error = NetworkError::NoError;
    int queued         = -1;
    int running        = -1;
    bool timerActive   = true;
    std::thread thread([&]() {
        QCNetworkAccessManager manager;
        manager.enableRequestScheduler(true);
        QSignalSpy queueSpy(&manager, &QCNetworkAccessManager::schedulerRequestQueued);
        auto *reply = manager.get(
            QCNetworkRequest(QUrl(QStringLiteral("http://no-dispatcher.test/"))));
        finished    = reply && reply->isFinished();
        error       = reply->error();
        queued      = queueSpy.count();
        running     = manager.schedulerStatistics().runningRequests();
        timerActive = QCNetworkRequestSchedulerTestAccess::admissionTimer(manager)->isActive();
    });
    thread.join();
    QVERIFY(finished);
    QCOMPARE(error, NetworkError::InvalidRequest);
    QCOMPARE(queued, 0);
    QCOMPARE(running, 0);
    QVERIFY(!timerActive);
}
