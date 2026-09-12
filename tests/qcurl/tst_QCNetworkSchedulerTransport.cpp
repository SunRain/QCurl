// SPDX-License-Identifier: MIT
// Copyright (c) 2025 QCurl Project

#include "QCNetworkAccessManager.h"
#include "QCNetworkConnectionPoolManager.h"
#include "QCNetworkReply.h"
#include "QCNetworkRequest.h"
#include "qcurl_http_script_server.h"

#include <QElapsedTimer>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QtTest>

using namespace QCurl;

class tst_QCNetworkSchedulerTransport : public QObject
{
    Q_OBJECT

public:
    tst_QCNetworkSchedulerTransport() = default;

private Q_SLOTS:
    void admissionAndConnectionLimitsAreIndependent_data();
    void admissionAndConnectionLimitsAreIndependent();
    void requestRateLimitDoesNotThrottleOtherRunningRequests();

private:
    Q_DISABLE_COPY_MOVE(tst_QCNetworkSchedulerTransport)
};

void tst_QCNetworkSchedulerTransport::admissionAndConnectionLimitsAreIndependent_data()
{
    QTest::addColumn<int>("admission");
    QTest::addColumn<int>("connections");
    QTest::addColumn<int>("expectedPeak");
    QTest::newRow("admission-only") << 1 << 3 << 1;
    QTest::newRow("connection-only") << 3 << 1 << 1;
    QTest::newRow("both-two") << 2 << 2 << 2;
}

void tst_QCNetworkSchedulerTransport::admissionAndConnectionLimitsAreIndependent()
{
    QFETCH(int, admission);
    QFETCH(int, connections);
    QFETCH(int, expectedPeak);
    auto response    = HttpScriptServer::response(200, QByteArrayLiteral("OK"));
    response.delayMs = 100;
    HttpScriptServer server({response});
    QVERIFY(server.start());
    auto *pool          = QCNetworkConnectionPoolManager::instance();
    const auto original = pool->config();
    const auto restore  = qScopeGuard([&]() {
        QCOMPARE(pool->setConfig(original), QCNetworkConnectionPoolManager::UpdateResult::Applied);
    });
    QCNetworkConnectionPoolConfig config;
    config.setMultiMaxTotalConnections(connections);
    config.setMultiMaxHostConnections(connections);
    QCOMPARE(pool->setConfig(config), QCNetworkConnectionPoolManager::UpdateResult::Applied);
    QCNetworkAccessManager manager;
    manager.enableRequestScheduler(true);
    auto policy = manager.schedulerPolicy();
    policy.setMaxConcurrentRequests(admission);
    policy.setMaxRequestsPerHost(3);
    QVERIFY(manager.setSchedulerPolicy(policy));
    QList<QCNetworkReply *> replies;
    for (int index = 0; index < 3; ++index) {
        replies.append(manager.get(QCNetworkRequest(server.url())));
    }
    QCOMPARE(manager.schedulerStatistics().runningRequests(), admission);
    QCOMPARE(manager.schedulerStatistics().pendingRequests(), 3 - admission);
    for (auto *reply : replies) {
        QTRY_VERIFY_WITH_TIMEOUT(reply->isFinished(), 3000);
        QCOMPARE(reply->error(), NetworkError::NoError);
    }
    QCOMPARE(server.peakRequests(), expectedPeak);
    QTRY_COMPARE(manager.schedulerStatistics().completedRequests(), 3);
}

void tst_QCNetworkSchedulerTransport::requestRateLimitDoesNotThrottleOtherRunningRequests()
{
    const QByteArray payload(256 * 1024, 'x');
    HttpScriptServer server({HttpScriptServer::response(200, payload)});
    QVERIFY(server.start());
    QCNetworkAccessManager manager;
    manager.enableRequestScheduler(true);
    auto policy = manager.schedulerPolicy();
    policy.setMaxConcurrentRequests(2);
    policy.setMaxRequestsPerHost(2);
    QVERIFY(manager.setSchedulerPolicy(policy));
    QCNetworkRequest slowRequest(server.url());
    QCOMPARE(slowRequest.setMaxDownloadBytesPerSec(64 * 1024), QCNetworkConfigUpdateResult::Applied);
    auto *slow = manager.get(slowRequest);
    auto *fast = manager.get(QCNetworkRequest(server.url()));
    QSignalSpy started(&manager, &QCNetworkAccessManager::schedulerRequestStarted);
    QElapsedTimer elapsed;
    elapsed.start();
    QTRY_VERIFY_WITH_TIMEOUT(fast->isFinished(), 3000);
    const qint64 fastDuration = elapsed.elapsed();
    QVERIFY(!slow->isFinished());
    QCOMPARE(started.count(), 2);
    QCOMPARE(fast->error(), NetworkError::NoError);
    QTRY_VERIFY_WITH_TIMEOUT(slow->isFinished(), 8000);
    QVERIFY(elapsed.elapsed() > fastDuration + 1000);
    QCOMPARE(slow->error(), NetworkError::NoError);
    QCOMPARE(slow->readAll().value_or(QByteArray()), payload);
    QCOMPARE(fast->readAll().value_or(QByteArray()), payload);
}

QTEST_GUILESS_MAIN(tst_QCNetworkSchedulerTransport)
#include "tst_QCNetworkSchedulerTransport.moc"
