/**
 * @file
 * @brief QCNetworkLaneKey 与 QCNetworkSchedulerPolicy public contract 测试。
 */

#include "QCNetworkLaneCancelResult.h"
#include "QCNetworkLaneKey.h"
#include "QCNetworkSchedulerPolicy.h"

#include <QLatin1StringView>
#include <QtTest/QtTest>

#include <type_traits>

using namespace QCurl;

class tst_QCNetworkSchedulerPolicy : public QObject
{
    Q_OBJECT

public:
    tst_QCNetworkSchedulerPolicy() = default;

private:
    Q_DISABLE_COPY_MOVE(tst_QCNetworkSchedulerPolicy)

private Q_SLOTS:
    void testLaneKeyDefaultsAndBuiltins();
    void testLaneKeyFromNameReportsErrorsWithoutModifyingOutput();
    void testLaneCancelResultFactoriesEnforceInvariants();
    void testPolicyRegistersCustomLaneAndValidatesDefaultLane();
    void testPolicyValidationRejectsInvalidLimitsAndLaneConfig();
    void testPolicySetLaneConfigRejectsInvalidInputWithoutSideEffects();
    void testPolicyIsSharedDataValueType();
};

void tst_QCNetworkSchedulerPolicy::testLaneKeyDefaultsAndBuiltins()
{
    QCOMPARE(QCNetworkLaneKey::defaultLane().name(), QString());
    QCOMPARE(QCNetworkLaneKey::control().name(), QStringLiteral("Control"));
    QCOMPARE(QCNetworkLaneKey::transfer().name(), QStringLiteral("Transfer"));
    QCOMPARE(QCNetworkLaneKey::background().name(), QStringLiteral("Background"));
    QVERIFY(QCNetworkLaneKey::control().isValid());
    QVERIFY(QCNetworkLaneKey::control() != QCNetworkLaneKey::transfer());
}

void tst_QCNetworkSchedulerPolicy::testLaneKeyFromNameReportsErrorsWithoutModifyingOutput()
{
    QCNetworkLaneKey lane = QCNetworkLaneKey::control();
    QString error;

    QVERIFY(QCNetworkLaneKey::fromName(QStringLiteral("  ImagePrefetch  "), &lane, &error));
    QCOMPARE(lane.name(), QStringLiteral("ImagePrefetch"));
    QCOMPARE(error, QString());

    QVERIFY(QCNetworkLaneKey::fromName(QLatin1StringView("  LatinLane  "), &lane, &error));
    QCOMPARE(lane.name(), QStringLiteral("LatinLane"));
    QCOMPARE(error, QString());

    const QCNetworkLaneKey previousLane = lane;
    QVERIFY(!QCNetworkLaneKey::fromName(QStringLiteral("   "), &lane, &error));
    QCOMPARE(lane, previousLane);
    QVERIFY(error.contains(QStringLiteral("lane name")));

    QVERIFY(!QCNetworkLaneKey::fromName(QStringLiteral("ImagePrefetch"), nullptr, &error));
    QVERIFY(error.contains(QStringLiteral("output")));
}

void tst_QCNetworkSchedulerPolicy::testLaneCancelResultFactoriesEnforceInvariants()
{
    static_assert(!std::is_convertible_v<QCNetworkLaneCancelResult::Status,
                                         QCNetworkLaneCancelResult::FailureReason>);

    const QCNetworkLaneCancelResult success = QCNetworkLaneCancelResult::success(3);
    QVERIFY(success.isSuccess());
    QCOMPARE(success.status(), QCNetworkLaneCancelResult::Status::Success);
    QCOMPARE(success.cancelledRequests(), 3);
    QCOMPARE(success.error(), QString());

    const QCNetworkLaneCancelResult failure
        = QCNetworkLaneCancelResult::failure(QCNetworkLaneCancelResult::FailureReason::InvalidLane,
                                             QString());
    QVERIFY(!failure.isSuccess());
    QCOMPARE(failure.status(), QCNetworkLaneCancelResult::Status::InvalidLane);
    QCOMPARE(failure.cancelledRequests(), 0);
    QVERIFY(!failure.error().isEmpty());
}

void tst_QCNetworkSchedulerPolicy::testPolicyRegistersCustomLaneAndValidatesDefaultLane()
{
    QCNetworkSchedulerPolicy policy = QCNetworkSchedulerPolicy::defaultPolicy();
    QCNetworkLaneKey imagePrefetch;
    QVERIFY(QCNetworkLaneKey::fromName(QStringLiteral("ImagePrefetch"), &imagePrefetch));

    QVERIFY(policy.isLaneRegistered(QCNetworkLaneKey::defaultLane()));
    QVERIFY(policy.isLaneRegistered(QCNetworkLaneKey::control()));
    QVERIFY(!policy.isLaneRegistered(imagePrefetch));
    QVERIFY(policy.validate());

    QCNetworkSchedulerPolicy::LaneConfig config;
    config.setWeight(2);
    QVERIFY(policy.setLaneConfig(imagePrefetch, config));

    QVERIFY(policy.isLaneRegistered(imagePrefetch));
    QCNetworkSchedulerPolicy::LaneConfig readConfig;
    QVERIFY(policy.laneConfig(imagePrefetch, &readConfig));
    QCOMPARE(readConfig.weight(), 2);
    QVERIFY(policy.validate());

    QCNetworkLaneKey missingLane;
    QVERIFY(QCNetworkLaneKey::fromName(QStringLiteral("MissingLane"), &missingLane));
    policy = QCNetworkSchedulerPolicy();
    QVERIFY(policy.setLaneConfig(missingLane, config));
    QString error;
    QVERIFY(!policy.validate(&error));
    QVERIFY(error.contains(QStringLiteral("default lane")));
}

void tst_QCNetworkSchedulerPolicy::testPolicyValidationRejectsInvalidLimitsAndLaneConfig()
{
    QCNetworkSchedulerPolicy policy = QCNetworkSchedulerPolicy::defaultPolicy();
    QString error;

    policy.setMaxConcurrentRequests(0);
    QVERIFY(!policy.validate(&error));
    QVERIFY(error.contains(QStringLiteral("maxConcurrentRequests")));

    policy = QCNetworkSchedulerPolicy::defaultPolicy();
    policy.setMaxRequestsPerHost(0);
    QVERIFY(!policy.validate(&error));
    QVERIFY(error.contains(QStringLiteral("maxRequestsPerHost")));

    policy = QCNetworkSchedulerPolicy::defaultPolicy();
    QCNetworkSchedulerPolicy::LaneConfig invalidLane;
    invalidLane.setWeight(0);
    QVERIFY(!policy.setLaneConfig(QCNetworkLaneKey::control(), invalidLane, &error));
    QVERIFY(error.contains(QStringLiteral("weight")));
    QVERIFY(policy.validate());

    policy      = QCNetworkSchedulerPolicy::defaultPolicy();
    invalidLane = QCNetworkSchedulerPolicy::LaneConfig();
    invalidLane.setWeight(-1);
    QVERIFY(!policy.setLaneConfig(QCNetworkLaneKey::control(), invalidLane, &error));
    QVERIFY(error.contains(QStringLiteral("weight")));
    QVERIFY(policy.validate());

    policy      = QCNetworkSchedulerPolicy::defaultPolicy();
    invalidLane = QCNetworkSchedulerPolicy::LaneConfig();
    invalidLane.setReservedGlobal(-1);
    QVERIFY(!policy.setLaneConfig(QCNetworkLaneKey::control(), invalidLane, &error));
    QVERIFY(error.contains(QStringLiteral("reservedGlobal")));
    QVERIFY(policy.validate());

    policy      = QCNetworkSchedulerPolicy::defaultPolicy();
    invalidLane = QCNetworkSchedulerPolicy::LaneConfig();
    invalidLane.setReservedPerHost(-1);
    QVERIFY(!policy.setLaneConfig(QCNetworkLaneKey::control(), invalidLane, &error));
    QVERIFY(error.contains(QStringLiteral("reservedPerHost")));
    QVERIFY(policy.validate());

    policy = QCNetworkSchedulerPolicy::defaultPolicy();
    policy.setAdmissionByteBudget(-1);
    QVERIFY(!policy.validate(&error));
    QVERIFY(error.contains(QStringLiteral("admissionByteBudget")));
}

void tst_QCNetworkSchedulerPolicy::testPolicySetLaneConfigRejectsInvalidInputWithoutSideEffects()
{
    QCNetworkSchedulerPolicy policy = QCNetworkSchedulerPolicy::defaultPolicy();
    QCNetworkSchedulerPolicy::LaneConfig originalControl;
    QVERIFY(policy.laneConfig(QCNetworkLaneKey::control(), &originalControl));

    QString error;
    QCNetworkSchedulerPolicy::LaneConfig validConfig;
    validConfig.setWeight(4);
    QVERIFY(!policy.setLaneConfig(QCNetworkLaneKey(), validConfig, &error));
    QVERIFY(error.contains(QStringLiteral("invalid lane")));
    QCNetworkSchedulerPolicy::LaneConfig controlConfig;
    QVERIFY(policy.laneConfig(QCNetworkLaneKey::control(), &controlConfig));
    QCOMPARE(controlConfig.weight(), originalControl.weight());

    QCNetworkSchedulerPolicy::LaneConfig invalidConfig;
    invalidConfig.setWeight(0);
    QVERIFY(!policy.setLaneConfig(QCNetworkLaneKey::control(), invalidConfig, &error));
    QVERIFY(error.contains(QStringLiteral("weight")));
    QVERIFY(policy.laneConfig(QCNetworkLaneKey::control(), &controlConfig));
    QCOMPARE(controlConfig.weight(), originalControl.weight());

    QCNetworkLaneKey missingLane;
    QVERIFY(QCNetworkLaneKey::fromName(QStringLiteral("MissingLane"), &missingLane));
    QVERIFY(!policy.laneConfig(missingLane, &controlConfig, &error));
    QVERIFY(error.contains(QStringLiteral("registered")));
    QVERIFY(!policy.laneConfig(QCNetworkLaneKey(), &controlConfig, &error));
    QVERIFY(error.contains(QStringLiteral("invalid lane")));
    QVERIFY(!policy.laneConfig(QCNetworkLaneKey::control(), nullptr, &error));
    QVERIFY(error.contains(QStringLiteral("output")));
}

void tst_QCNetworkSchedulerPolicy::testPolicyIsSharedDataValueType()
{
    QCNetworkSchedulerPolicy original = QCNetworkSchedulerPolicy::defaultPolicy();
    QCNetworkSchedulerPolicy copy     = original;

    QCNetworkLaneKey custom;
    QVERIFY(QCNetworkLaneKey::fromName(QStringLiteral("Custom"), &custom));
    QVERIFY(copy.setLaneConfig(custom, QCNetworkSchedulerPolicy::LaneConfig{}));

    QVERIFY(!original.isLaneRegistered(custom));
    QVERIFY(copy.isLaneRegistered(custom));
    QVERIFY(original.validate());
    QVERIFY(copy.validate());
}

int main(int argc, char *argv[])
{
    tst_QCNetworkSchedulerPolicy test;
    return QTest::qExec(&test, argc, argv);
}
#include "tst_QCNetworkSchedulerPolicy.moc"
