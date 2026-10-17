#include "CurlFeatureProbe.h"

#include <QScopeGuard>
#include <QtTest/QtTest>

#include <curl/curlver.h>

using namespace QCurl;

class tst_CurlFeatureProbe : public QObject
{
    Q_OBJECT

private:
    Q_DISABLE_COPY_MOVE(tst_CurlFeatureProbe)

public:
    tst_CurlFeatureProbe() = default;

private Q_SLOTS:
    void test_detectVersion();
    void test_minimumRuntimeAvailability_data();
    void test_minimumRuntimeAvailability();
    void test_easyOptionAvailability();
    void test_multiOptionAvailability();
};

void tst_CurlFeatureProbe::test_detectVersion()
{
    auto &probe = CurlFeatureProbe::instance();

    QCOMPARE(probe.compiledVersionNum(), LIBCURL_VERSION_NUM);
    const auto *runtime = curl_version_info(CURLVERSION_NOW);
    QVERIFY(runtime);
    QCOMPARE(probe.runtimeVersionNum(), static_cast<int>(runtime->version_num));
    QVERIFY(probe.runtimeVersionNum() >= 0x080e01);
    QVERIFY(!probe.runtimeVersionString().isEmpty());
}

void tst_CurlFeatureProbe::test_minimumRuntimeAvailability_data()
{
    QTest::addColumn<QByteArray>("versionNum");
    QTest::addColumn<QString>("versionString");
    QTest::addColumn<bool>("supported");

    QTest::newRow("previous-minimum")
        << QByteArray("0x075500") << QStringLiteral("7.85.0") << false;
    QTest::newRow("previous-target") << QByteArray("0x080c00") << QStringLiteral("8.12.0") << false;
    QTest::newRow("below-minimum") << QByteArray("0x080d00") << QStringLiteral("8.13.0") << false;
    QTest::newRow("unfixed-websocket")
        << QByteArray("0x080e00") << QStringLiteral("8.14.0") << false;
    QTest::newRow("exact-minimum") << QByteArray("0x080e01") << QStringLiteral("8.14.1") << true;
    QTest::newRow("above-minimum") << QByteArray("0x080e02") << QStringLiteral("8.14.2") << true;
}

void tst_CurlFeatureProbe::test_minimumRuntimeAvailability()
{
    QFETCH(QByteArray, versionNum);
    QFETCH(QString, versionString);
    QFETCH(bool, supported);

    auto &probe               = CurlFeatureProbe::instance();
    const QByteArray oldEnv   = qgetenv("QCURL_TEST_FORCE_RUNTIME_LIBCURL_VERSION_NUM");
    const auto restoreRuntime = qScopeGuard([&probe, oldEnv]() {
        if (oldEnv.isEmpty()) {
            qunsetenv("QCURL_TEST_FORCE_RUNTIME_LIBCURL_VERSION_NUM");
        } else {
            qputenv("QCURL_TEST_FORCE_RUNTIME_LIBCURL_VERSION_NUM", oldEnv);
        }
        probe.refreshForTesting();
    });

    qputenv("QCURL_TEST_FORCE_RUNTIME_LIBCURL_VERSION_NUM", versionNum);
    probe.refreshForTesting();

    const auto availability = probe.minimumRuntimeAvailability();
    QCOMPARE(availability.supported, supported);
    if (supported) {
        QVERIFY(availability.reason.isEmpty());
    } else {
        QVERIFY(availability.reason.contains(QStringLiteral("8.14.1")));
        QVERIFY(availability.reason.contains(versionString));
        QVERIFY(availability.reason.contains(QStringLiteral("请升级运行时 libcurl")));
    }
}

void tst_CurlFeatureProbe::test_easyOptionAvailability()
{
    auto &probe = CurlFeatureProbe::instance();

    const auto availability = probe.easyOptionAvailability(CURLOPT_MAXLIFETIME_CONN);
    QVERIFY(availability.supported);
    QVERIFY(availability.reason.isEmpty());
}

void tst_CurlFeatureProbe::test_multiOptionAvailability()
{
    auto &probe = CurlFeatureProbe::instance();

    const auto availability = probe.multiOptionAvailability(CURLMOPT_MAX_TOTAL_CONNECTIONS);
    QVERIFY(availability.supported);
    QVERIFY(availability.reason.isEmpty());
}

QTEST_MAIN(tst_CurlFeatureProbe)

#include "tst_CurlFeatureProbe.moc"
