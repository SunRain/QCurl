/**
 * @file
 * @brief QCNetworkLaneKey 与 QCNetworkSchedulerPolicy public contract 测试。
 */

#include "QCNetworkLaneKey.h"
#include "QCNetworkSchedulerPolicy.h"

#include <QtTest/QtTest>

using namespace QCurl;

class tst_QCNetworkSchedulerPolicy : public QObject
{
    Q_OBJECT

private slots:
    void testLaneKeyDefaultsAndBuiltins();
    void testLaneKeyFromNameReportsErrorsWithoutModifyingOutput();
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

    const QCNetworkLaneKey previousLane = lane;
    QVERIFY(!QCNetworkLaneKey::fromName(QStringLiteral("   "), &lane, &error));
    QCOMPARE(lane, previousLane);
    QVERIFY(error.contains(QStringLiteral("lane name")));

    QVERIFY(!QCNetworkLaneKey::fromName(QStringLiteral("ImagePrefetch"), nullptr, &error));
    QVERIFY(error.contains(QStringLiteral("output")));
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
    config.setQuantum(3);
    QVERIFY(policy.setLaneConfig(imagePrefetch, config));
    policy.setDefaultLane(imagePrefetch);

    QVERIFY(policy.isLaneRegistered(imagePrefetch));
    QCOMPARE(policy.defaultLane(), imagePrefetch);
    QCOMPARE(policy.laneConfig(imagePrefetch).weight(), 2);
    QCOMPARE(policy.laneConfig(imagePrefetch).quantum(), 3);
    QVERIFY(policy.validate());

    QCNetworkLaneKey missingLane;
    QVERIFY(QCNetworkLaneKey::fromName(QStringLiteral("MissingLane"), &missingLane));
    policy.setDefaultLane(missingLane);
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

    policy = QCNetworkSchedulerPolicy::defaultPolicy();
    invalidLane = QCNetworkSchedulerPolicy::LaneConfig();
    invalidLane.setQuantum(0);
    QVERIFY(!policy.setLaneConfig(QCNetworkLaneKey::control(), invalidLane, &error));
    QVERIFY(error.contains(QStringLiteral("quantum")));
    QVERIFY(policy.validate());

    policy = QCNetworkSchedulerPolicy::defaultPolicy();
    invalidLane = QCNetworkSchedulerPolicy::LaneConfig();
    invalidLane.setReservedGlobal(-1);
    QVERIFY(!policy.setLaneConfig(QCNetworkLaneKey::control(), invalidLane, &error));
    QVERIFY(error.contains(QStringLiteral("reservedGlobal")));
    QVERIFY(policy.validate());

    policy = QCNetworkSchedulerPolicy::defaultPolicy();
    invalidLane = QCNetworkSchedulerPolicy::LaneConfig();
    invalidLane.setReservedPerHost(-1);
    QVERIFY(!policy.setLaneConfig(QCNetworkLaneKey::control(), invalidLane, &error));
    QVERIFY(error.contains(QStringLiteral("reservedPerHost")));
    QVERIFY(policy.validate());

    policy = QCNetworkSchedulerPolicy::defaultPolicy();
    policy.setMaxBandwidthBytesPerSec(-1);
    QVERIFY(!policy.validate(&error));
    QVERIFY(error.contains(QStringLiteral("maxBandwidthBytesPerSec")));
}

void tst_QCNetworkSchedulerPolicy::testPolicySetLaneConfigRejectsInvalidInputWithoutSideEffects()
{
    QCNetworkSchedulerPolicy policy = QCNetworkSchedulerPolicy::defaultPolicy();
    const QCNetworkSchedulerPolicy::LaneConfig originalControl
        = policy.laneConfig(QCNetworkLaneKey::control());

    QString error;
    QCNetworkSchedulerPolicy::LaneConfig validConfig;
    validConfig.setWeight(4);
    QVERIFY(!policy.setLaneConfig(QCNetworkLaneKey(), validConfig, &error));
    QVERIFY(error.contains(QStringLiteral("invalid lane")));
    QCOMPARE(policy.laneConfig(QCNetworkLaneKey::control()).weight(), originalControl.weight());

    QCNetworkSchedulerPolicy::LaneConfig invalidConfig;
    invalidConfig.setWeight(0);
    QVERIFY(!policy.setLaneConfig(QCNetworkLaneKey::control(), invalidConfig, &error));
    QVERIFY(error.contains(QStringLiteral("weight")));
    QCOMPARE(policy.laneConfig(QCNetworkLaneKey::control()).weight(), originalControl.weight());
}

void tst_QCNetworkSchedulerPolicy::testPolicyIsSharedDataValueType()
{
    QCNetworkSchedulerPolicy original = QCNetworkSchedulerPolicy::defaultPolicy();
    QCNetworkSchedulerPolicy copy = original;

    QCNetworkLaneKey custom;
    QVERIFY(QCNetworkLaneKey::fromName(QStringLiteral("Custom"), &custom));
    QVERIFY(copy.setLaneConfig(custom, QCNetworkSchedulerPolicy::LaneConfig{}));

    QVERIFY(!original.isLaneRegistered(custom));
    QVERIFY(copy.isLaneRegistered(custom));
    QVERIFY(original.validate());
    QVERIFY(copy.validate());
}

QTEST_MAIN(tst_QCNetworkSchedulerPolicy)
#include "tst_QCNetworkSchedulerPolicy.moc"
