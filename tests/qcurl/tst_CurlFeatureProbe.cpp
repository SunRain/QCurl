#include "CurlFeatureProbe.h"

#include <QtTest/QtTest>

#include <curl/curlver.h>

using namespace QCurl;

class tst_CurlFeatureProbe : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void test_detectVersion();
    void test_minimumRuntimeAvailability();
    void test_easyOptionAvailability();
    void test_multiOptionAvailability();
};

void tst_CurlFeatureProbe::test_detectVersion()
{
    auto &probe = CurlFeatureProbe::instance();

    QCOMPARE(probe.compiledVersionNum(), LIBCURL_VERSION_NUM);
    QVERIFY(probe.runtimeVersionNum() >= 0x075500);
    QVERIFY(!probe.runtimeVersionString().isEmpty());
}

void tst_CurlFeatureProbe::test_minimumRuntimeAvailability()
{
    auto &probe = CurlFeatureProbe::instance();
    const QByteArray oldEnv = qgetenv("QCURL_TEST_FORCE_RUNTIME_LIBCURL_VERSION_NUM");

    qputenv("QCURL_TEST_FORCE_RUNTIME_LIBCURL_VERSION_NUM", QByteArray("0x075400"));
    probe.refreshForTesting();

    const auto failingAvailability = probe.minimumRuntimeAvailability();
    QVERIFY(!failingAvailability.supported);
    QVERIFY(failingAvailability.reason.contains(QStringLiteral("7.85.0")));
    QVERIFY(failingAvailability.reason.contains(QStringLiteral("请升级运行时 libcurl")));

    qputenv("QCURL_TEST_FORCE_RUNTIME_LIBCURL_VERSION_NUM", QByteArray("0x075500"));
    probe.refreshForTesting();

    const auto supportedAvailability = probe.minimumRuntimeAvailability();
    QVERIFY(supportedAvailability.supported);
    QVERIFY(supportedAvailability.reason.isEmpty());

    if (oldEnv.isEmpty()) {
        qunsetenv("QCURL_TEST_FORCE_RUNTIME_LIBCURL_VERSION_NUM");
    } else {
        qputenv("QCURL_TEST_FORCE_RUNTIME_LIBCURL_VERSION_NUM", oldEnv);
    }
    probe.refreshForTesting();
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
