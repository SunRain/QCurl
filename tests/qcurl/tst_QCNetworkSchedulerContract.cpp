// SPDX-License-Identifier: MIT
// Copyright (c) 2025 QCurl Project

#include "QCNetworkAccessManager.h"
#include "QCNetworkMockHandler.h"
#include "QCNetworkReply.h"
#include "QCNetworkRequest.h"
#include "qcnetwork_mock_test_support.h"

#include <QSignalSpy>
#include <QtTest>

#include <limits>

using namespace QCurl;

class tst_QCNetworkSchedulerContract : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void busyLaneRemoval_data();
    void busyLaneRemoval();
    void policyReentryKeepsLatestCommit();
    void cancellationCommitsOnce();
    void maximumWeightStartsRequest();
    void managerSynchronousCancellationPreventsExecute();

private:
    Q_DISABLE_COPY_MOVE(tst_QCNetworkSchedulerContract)

public:
    tst_QCNetworkSchedulerContract() = default;
};

void tst_QCNetworkSchedulerContract::busyLaneRemoval_data()
{
    QTest::addColumn<int>("state");
    QTest::newRow("running") << 0;
    QTest::newRow("pending") << 1;
    QTest::newRow("deferred") << 2;
}

void tst_QCNetworkSchedulerContract::busyLaneRemoval()
{
    QFETCH(int, state);
    QCNetworkAccessManager manager;
    manager.enableRequestScheduler(true);
    auto policy = QCNetworkSchedulerPolicy::defaultPolicy();
    policy.setMaxConcurrentRequests(1);
    QVERIFY(manager.setSchedulerPolicy(policy));
    if (state != 0) {
        manager.get(QCNetworkRequest(QUrl(QStringLiteral("http://blocker.test/"))));
    }
    QCNetworkRequest request(QUrl(QStringLiteral("http://control.test/")));
    request.setLane(QCNetworkLaneKey::control());
    auto *reply = manager.get(request);
    if (state == 2) {
        QCOMPARE(manager.deferScheduledRequest(reply), SchedulerCommandResult::Applied);
    }
    QCNetworkSchedulerPolicy replacement;
    QVERIFY(replacement.setLaneConfig(QCNetworkLaneKey::defaultLane(), {}));
    const auto before = manager.schedulerStatistics();
    QString error;
    QVERIFY(!manager.setSchedulerPolicy(replacement, &error));
    QVERIFY(!error.isEmpty());
    QVERIFY(manager.schedulerPolicy().isLaneRegistered(QCNetworkLaneKey::control()));
    QCOMPARE(manager.schedulerStatistics().pendingRequests(), before.pendingRequests());
    QCOMPARE(manager.schedulerStatistics().runningRequests(), before.runningRequests());
    const auto cancelled
        = manager.cancelLaneRequests(QCNetworkLaneKey::control(),
                                     QCNetworkAccessManager::SchedulerCancelScope::PendingAndRunning);
    QVERIFY(cancelled.isSuccess());
    QCOMPARE(cancelled.cancelledRequests(), 1);
    QVERIFY(manager.setSchedulerPolicy(replacement));
    QVERIFY(!manager.schedulerPolicy().isLaneRegistered(QCNetworkLaneKey::control()));
}

void tst_QCNetworkSchedulerContract::policyReentryKeepsLatestCommit()
{
    QCNetworkAccessManager manager;
    manager.enableRequestScheduler(true);
    auto policy = QCNetworkSchedulerPolicy::defaultPolicy();
    policy.setMaxConcurrentRequests(1);
    policy.setMaxRequestsPerHost(3);
    QVERIFY(manager.setSchedulerPolicy(policy));
    manager.get(QCNetworkRequest(QUrl(QStringLiteral("http://policy.test/first"))));
    manager.get(QCNetworkRequest(QUrl(QStringLiteral("http://policy.test/second"))));
    bool reentered = false;
    connect(&manager, &QCNetworkAccessManager::schedulerPendingQueueEmpty, &manager, [&]() {
        if (reentered) {
            return;
        }
        reentered = true;
        QCOMPARE(manager.schedulerPolicy().maxConcurrentRequests(), 2);
        QVERIFY(manager.setSchedulerPolicy(policy));
        QCOMPARE(manager.schedulerPolicy().maxConcurrentRequests(), 1);
    });
    auto outer = policy;
    outer.setMaxConcurrentRequests(2);
    QVERIFY(manager.setSchedulerPolicy(outer));
    QVERIFY(reentered);
    QCOMPARE(manager.schedulerPolicy().maxConcurrentRequests(), 1);
    QCOMPARE(manager.schedulerStatistics().runningRequests(), 2);
    manager.get(QCNetworkRequest(QUrl(QStringLiteral("http://policy.test/third"))));
    QCOMPARE(manager.schedulerStatistics().pendingRequests(), 1);
}

void tst_QCNetworkSchedulerContract::cancellationCommitsOnce()
{
    QCNetworkAccessManager manager;
    manager.enableRequestScheduler(true);
    auto *reply = manager.get(QCNetworkRequest(QUrl(QStringLiteral("http://cancel.test/"))));
    QSignalSpy cancelled(&manager, &QCNetworkAccessManager::schedulerRequestCancelled);
    QCOMPARE(manager.cancelScheduledRequest(reply), SchedulerCommandResult::Applied);
    QCOMPARE(manager.cancelScheduledRequest(reply), SchedulerCommandResult::NotTracked);
    QCOMPARE(manager.schedulerStatistics().cancelledRequests(), 1);
    QCOMPARE(cancelled.count(), 1);
    QTRY_VERIFY(reply->isFinished());
    QCOMPARE(manager.schedulerStatistics().cancelledRequests(), 1);
    QCOMPARE(cancelled.count(), 1);
}

void tst_QCNetworkSchedulerContract::maximumWeightStartsRequest()
{
    QCNetworkAccessManager manager;
    manager.enableRequestScheduler(true);
    auto policy = QCNetworkSchedulerPolicy::defaultPolicy();
    QCNetworkSchedulerPolicy::LaneConfig lane;
    lane.setWeight(std::numeric_limits<int>::max());
    QVERIFY(policy.setLaneConfig(QCNetworkLaneKey::control(), lane));
    QVERIFY(manager.setSchedulerPolicy(policy));
    QCNetworkRequest request(QUrl(QStringLiteral("http://weight.test/")));
    request.setLane(QCNetworkLaneKey::control());
    manager.get(request);
    QCOMPARE(manager.schedulerStatistics().runningRequests(), 1);
    QCOMPARE(manager.schedulerStatistics().pendingRequests(), 0);
}

void tst_QCNetworkSchedulerContract::managerSynchronousCancellationPreventsExecute()
{
    QCNetworkMockHandler mock;
    QCNetworkAccessManager manager;
    TestSupport::setMockHandler(manager, &mock);
    manager.enableRequestScheduler(true);
    const QUrl url(QStringLiteral("http://start.test/"));
    mock.setCaptureEnabled(true);
    mock.mockResponse(HttpMethod::Get, url, QByteArrayLiteral("OK"));
    QSignalSpy about(&manager, &QCNetworkAccessManager::schedulerRequestAboutToStart);
    QSignalSpy started(&manager, &QCNetworkAccessManager::schedulerRequestStarted);
    connect(&manager,
            &QCNetworkAccessManager::schedulerRequestAboutToStart,
            &manager,
            [&](QCNetworkReply *reply) {
                QCOMPARE(manager.cancelScheduledRequest(reply), SchedulerCommandResult::Applied);
            });
    auto *reply = manager.get(QCNetworkRequest(url));
    QTRY_VERIFY(reply->isFinished());
    QCOMPARE(about.count(), 1);
    QCOMPARE(started.count(), 0);
    QCOMPARE(mock.capturedRequests().size(), 0);
}

QTEST_GUILESS_MAIN(tst_QCNetworkSchedulerContract)
#include "tst_QCNetworkSchedulerContract.moc"
