// SPDX-License-Identifier: MIT
// Copyright (c) 2025 QCurl Project

#include "private/QCNetworkAdmissionCore_p.h"

#include <QCoreApplication>
#include <QSet>
#include <QtTest>

#include <limits>

using namespace QCurl;
using namespace QCurl::Internal;

namespace {

ReplySnapshot snapshot(const QCNetworkLaneKey &lane,
                       int host                          = 0,
                       QCNetworkRequestPriority priority = QCNetworkRequestPriority::Normal)
{
    return {lane, QStringLiteral("http://host%1.test:80").arg(host), priority};
}

QCNetworkSchedulerPolicy weightedPolicy(int limit = 1)
{
    auto policy = QCNetworkSchedulerPolicy::defaultPolicy();
    policy.setMaxConcurrentRequests(limit);
    policy.setMaxRequestsPerHost(2);
    QCNetworkSchedulerPolicy::LaneConfig control;
    control.setWeight(3);
    const bool registered = policy.setLaneConfig(QCNetworkLaneKey::control(), control);
    Q_ASSERT(registered);
    return policy;
}

} // namespace

class tst_QCNetworkAdmissionCore : public QObject
{
    Q_OBJECT

public:
    tst_QCNetworkAdmissionCore() = default;

private Q_SLOTS:
    void noApplicationRequired();
    void weightedShareWithTwoHundredPending();
    void prioritiesAndHostRotation();
    void busyLaneRemoval_data();
    void busyLaneRemoval();
    void noOpPolicyPreservesRoundAndLowerLimitDoesNotPreempt();
    void commandTransitionsAndSnapshots();
    void terminalAccountingIsUnique();
    void weightBoundaries();
    void reservationAndReleaseSequences_data();
    void reservationAndReleaseSequences();

private:
    Q_DISABLE_COPY_MOVE(tst_QCNetworkAdmissionCore)
};

void tst_QCNetworkAdmissionCore::noApplicationRequired()
{
    QVERIFY(QCoreApplication::instance() == nullptr);
    AdmissionCore core;
    QCOMPARE(core.enqueue(1, snapshot(QCNetworkLaneKey::defaultLane())),
             SchedulerCommandResult::Applied);
    QVERIFY(core.takeNextPending());
    QVERIFY(core.finalize(1, FinalizeTrigger::FinishedSignal).wasTracked);
    QCOMPARE(core.statistics().completedRequests(), 1);
}

void tst_QCNetworkAdmissionCore::weightedShareWithTwoHundredPending()
{
    AdmissionCore core;
    QVERIFY(core.applyPolicy(weightedPolicy()));
    for (int index = 0; index < 200; ++index) {
        const auto lane = index < 150 ? QCNetworkLaneKey::control() : QCNetworkLaneKey::transfer();
        QCOMPARE(core.enqueue(quint64(index + 1),
                              snapshot(lane,
                                       index % 4,
                                       static_cast<QCNetworkRequestPriority>(index % 6))),
                 SchedulerCommandResult::Applied);
    }
    QCOMPARE(core.statistics().pendingRequests(), 200);
    QSet<quint64> started;
    for (int index = 0; index < 200; ++index) {
        // 每次重新应用相同快照也不能把 3:1 轮次重置到 Control。
        QVERIFY(core.applyPolicy(weightedPolicy()));
        const auto next = core.takeNextPending();
        QVERIFY(next);
        QCOMPARE(next->request.snapshot.lane,
                 index % 4 < 3 ? QCNetworkLaneKey::control() : QCNetworkLaneKey::transfer());
        QVERIFY(!started.contains(next->request.id));
        started.insert(next->request.id);
        QVERIFY(core.finalize(next->request.id, FinalizeTrigger::FinishedSignal).wasTracked);
    }
    QCOMPARE(started.size(), 200);
    QCOMPARE(core.statistics().completedRequests(), 200);
    QCOMPARE(core.statistics().runningRequests(), 0);
    QCOMPARE(core.statistics().pendingRequests(), 0);
    QVERIFY(!core.takeNextPending());
}

void tst_QCNetworkAdmissionCore::prioritiesAndHostRotation()
{
    AdmissionCore core;
    QVERIFY(core.applyPolicy(weightedPolicy()));
    for (int value = 0; value < 6; ++value) {
        for (int host = 0; host < 2; ++host) {
            QCOMPARE(core.enqueue(quint64(2 * value + host + 1),
                                  snapshot(QCNetworkLaneKey::control(),
                                           host,
                                           static_cast<QCNetworkRequestPriority>(value))),
                     SchedulerCommandResult::Applied);
        }
    }
    for (int value = 5; value >= 0; --value) {
        for (int host = 0; host < 2; ++host) {
            const auto next = core.takeNextPending();
            QVERIFY(next);
            QCOMPARE(int(next->request.snapshot.priority), value);
            QCOMPARE(next->request.snapshot.origin,
                     snapshot(QCNetworkLaneKey::control(), host).origin);
            QVERIFY(core.finalize(next->request.id, FinalizeTrigger::FinishedSignal).wasTracked);
        }
    }
}

void tst_QCNetworkAdmissionCore::busyLaneRemoval_data()
{
    QTest::addColumn<int>("state");
    QTest::newRow("pending") << 0;
    QTest::newRow("deferred") << 1;
    QTest::newRow("running") << 2;
}

void tst_QCNetworkAdmissionCore::busyLaneRemoval()
{
    QFETCH(int, state);
    AdmissionCore core;
    QCOMPARE(core.enqueue(1, snapshot(QCNetworkLaneKey::control())),
             SchedulerCommandResult::Applied);
    if (state == 1) {
        QCOMPARE(core.defer(1).result, SchedulerCommandResult::Applied);
    } else if (state == 2) {
        QVERIFY(core.takeNextPending());
    }
    const auto original = core.policy();
    const auto request  = core.request(1);
    QCNetworkSchedulerPolicy replacement;
    QVERIFY(replacement.setLaneConfig(QCNetworkLaneKey::defaultLane(), {}));
    QString error;
    QVERIFY(!core.applyPolicy(replacement, &error));
    QVERIFY(core.policy() == original);
    QVERIFY(!error.isEmpty());
    QCOMPARE(core.request(1)->state, request->state);
    QVERIFY(core.finalize(1, FinalizeTrigger::ExplicitCancel).wasTracked);
    QVERIFY(core.applyPolicy(replacement));
    QVERIFY(!core.policy().isLaneRegistered(QCNetworkLaneKey::control()));
}

void tst_QCNetworkAdmissionCore::noOpPolicyPreservesRoundAndLowerLimitDoesNotPreempt()
{
    AdmissionCore core;
    auto policy = weightedPolicy(3);
    QVERIFY(core.applyPolicy(policy));
    for (quint64 id = 1; id <= 4; ++id) {
        QCOMPARE(core.enqueue(id, snapshot(QCNetworkLaneKey::control(), int(id))),
                 SchedulerCommandResult::Applied);
    }
    for (int count = 0; count < 3; ++count) {
        QVERIFY(core.takeNextPending());
    }
    policy.setMaxConcurrentRequests(1);
    QVERIFY(core.applyPolicy(policy));
    QCOMPARE(core.statistics().runningRequests(), 3);
    QVERIFY(!core.takeNextPending());
    for (quint64 id = 1; id <= 3; ++id) {
        QVERIFY(!core.takeNextPending());
        QVERIFY(core.finalize(id, FinalizeTrigger::FinishedSignal).wasTracked);
    }
    QCOMPARE(core.takeNextPending()->request.id, quint64(4));
}

void tst_QCNetworkAdmissionCore::commandTransitionsAndSnapshots()
{
    AdmissionCore core;
    QCOMPARE(core.enqueue(1, snapshot(QCNetworkLaneKey::control())),
             SchedulerCommandResult::Applied);
    const auto changed = core.changePriority(1, QCNetworkRequestPriority::High);
    QCOMPARE(changed.result, SchedulerCommandResult::Applied);
    QCOMPARE(core.changePriority(1, QCNetworkRequestPriority::High).result,
             SchedulerCommandResult::NoChange);
    QVERIFY(core.defer(1).pendingQueueEmptied);
    QCOMPARE(core.defer(1).result, SchedulerCommandResult::InvalidState);
    QCOMPARE(core.changePriority(1, QCNetworkRequestPriority::Low).result,
             SchedulerCommandResult::InvalidState);
    QCOMPARE(core.undefer(1).result, SchedulerCommandResult::Applied);
    QCOMPARE(core.undefer(1).result, SchedulerCommandResult::InvalidState);
    const auto next = core.takeNextPending();
    QVERIFY(next && next->pendingQueueEmptied);
    QCOMPARE(int(next->request.snapshot.priority), int(QCNetworkRequestPriority::High));
    QVERIFY(core.finalize(1, FinalizeTrigger::ExplicitCancel).wasTracked);
    QCOMPARE(int(changed.snapshot.priority), int(QCNetworkRequestPriority::High));
    QCOMPARE(core.defer(1).result, SchedulerCommandResult::NotTracked);
}

void tst_QCNetworkAdmissionCore::terminalAccountingIsUnique()
{
    AdmissionCore core;
    QCOMPARE(core.enqueue(1, snapshot(QCNetworkLaneKey::control())),
             SchedulerCommandResult::Applied);
    QVERIFY(core.takeNextPending());
    QVERIFY(core.finalize(1, FinalizeTrigger::ExplicitCancel).emitCancelled);
    QVERIFY(!core.finalize(1, FinalizeTrigger::ExplicitCancel).wasTracked);
    QVERIFY(!core.finalize(1, FinalizeTrigger::FinishedSignal).wasTracked);
    QVERIFY(!core.finalize(1, FinalizeTrigger::Destroyed).wasTracked);
    QCOMPARE(core.statistics().cancelledRequests(), 1);
    QCOMPARE(core.statistics().runningRequests(), 0);
    QCOMPARE(core.enqueue(2, snapshot(QCNetworkLaneKey::control())),
             SchedulerCommandResult::Applied);
    QVERIFY(core.takeNextPending());
    QVERIFY(core.finalize(2, FinalizeTrigger::FinishedSignal, {false, 37, 11, 120}).emitFinished);
    QVERIFY(!core.finalize(2, FinalizeTrigger::Destroyed).wasTracked);
    QCOMPARE(core.statistics().completedRequests(), 1);
    QCOMPARE(core.statistics().totalBytesReceived(), qint64(37));
    QCOMPARE(core.statistics().totalBytesSent(), qint64(11));
    QCOMPARE(core.statistics().avgResponseTime(), 120.0);
}

void tst_QCNetworkAdmissionCore::weightBoundaries()
{
    AdmissionCore core;
    auto policy = weightedPolicy();
    QCNetworkSchedulerPolicy::LaneConfig lane;
    for (int weight : {0, -1, std::numeric_limits<int>::min()}) {
        lane.setWeight(weight);
        QVERIFY(!policy.setLaneConfig(QCNetworkLaneKey::control(), lane));
    }
    lane.setWeight(std::numeric_limits<int>::max());
    lane.setReservedGlobal(std::numeric_limits<int>::max());
    lane.setReservedPerHost(std::numeric_limits<int>::max());
    QVERIFY(policy.setLaneConfig(QCNetworkLaneKey::control(), lane));
    QVERIFY(core.applyPolicy(policy));
    for (quint64 id = 1; id <= 20; ++id) {
        QCOMPARE(core.enqueue(id, snapshot(QCNetworkLaneKey::control())),
                 SchedulerCommandResult::Applied);
    }
    for (int count = 0; count < 20; ++count) {
        const auto next = core.takeNextPending();
        QVERIFY(next);
        QVERIFY(core.finalize(next->request.id, FinalizeTrigger::FinishedSignal).wasTracked);
    }
    lane.setReservedGlobal(0);
    lane.setReservedPerHost(0);
    QVERIFY(policy.setLaneConfig(QCNetworkLaneKey::control(), lane));
    QVERIFY(core.applyPolicy(policy));
    QCOMPARE(core.enqueue(21, snapshot(QCNetworkLaneKey::control())),
             SchedulerCommandResult::Applied);
    QVERIFY(core.takeNextPending());
}

void tst_QCNetworkAdmissionCore::reservationAndReleaseSequences_data()
{
    QTest::addColumn<int>("seed");
    for (int seed = 0; seed < 64; ++seed) {
        QTest::newRow(qPrintable(QString::number(seed))) << seed;
    }
}

void tst_QCNetworkAdmissionCore::reservationAndReleaseSequences()
{
    QFETCH(int, seed);
    AdmissionCore core;
    auto policy = weightedPolicy(1 + seed % 7);
    policy.setMaxRequestsPerHost(1 + seed % 3);
    const auto lanes = policy.registeredLanes();
    for (int index = 0; index < lanes.size(); ++index) {
        QCNetworkSchedulerPolicy::LaneConfig lane;
        lane.setWeight(1 + (seed + index) % 4);
        lane.setReservedGlobal((seed + index) % 5);
        lane.setReservedPerHost((seed + index) % 4);
        QVERIFY(policy.setLaneConfig(lanes.at(index), lane));
    }
    QVERIFY(core.applyPolicy(policy));
    for (quint64 id = 1; id <= 200; ++id) {
        QCOMPARE(core.enqueue(id,
                              snapshot(lanes.at(int(id) % lanes.size()),
                                       int(id) % 5,
                                       static_cast<QCNetworkRequestPriority>(id % 6))),
                 SchedulerCommandResult::Applied);
    }
    QSet<quint64> terminal;
    QList<ScheduledRequest> running;
    while (terminal.size() < 200) {
        while (const auto next = core.takeNextPending()) {
            QVERIFY(!terminal.contains(next->request.id));
            running.append(next->request);
            QVERIFY(running.size() <= policy.maxConcurrentRequests());
            QHash<QString, int> hostCounts;
            for (const auto &request : std::as_const(running)) {
                QVERIFY(++hostCounts[request.snapshot.origin] <= policy.maxRequestsPerHost());
            }
        }
        QVERIFY2(!running.isEmpty(), "存在未终结请求却无法继续选路或释放");
        const auto finished = running.takeAt((terminal.size() + seed) % running.size());
        const auto trigger  = finished.id % 11 == 0 ? FinalizeTrigger::ExplicitCancel
                                                    : FinalizeTrigger::FinishedSignal;
        QVERIFY(core.finalize(finished.id, trigger).wasTracked);
        terminal.insert(finished.id);
    }
    QCOMPARE(core.statistics().completedRequests() + core.statistics().cancelledRequests(), 200);
    QCOMPARE(core.statistics().pendingRequests(), 0);
    QCOMPARE(core.statistics().runningRequests(), 0);
    for (quint64 id = 1; id <= 200; ++id) {
        QVERIFY(!core.request(id));
    }
}

// 无 application 的 QtTest 入口；直接 qExec 避免可变参数宏在 C++17 下的扩展警告。
int main(int argc, char *argv[])
{
    tst_QCNetworkAdmissionCore test;
    return QTest::qExec(&test, argc, argv);
}
#include "tst_QCNetworkAdmissionCore.moc"
