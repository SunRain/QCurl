#include "CurlFeatureProbe.h"

#include <QtTest/QtTest>

#include <curl/curlver.h>

using namespace QCurl;

class tst_CurlFeatureProbe : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void test_detectVersion();
    void test_easyOptionAvailability();
    void test_multiOptionAvailability();
};

void tst_CurlFeatureProbe::test_detectVersion()
{
    auto &probe = CurlFeatureProbe::instance();

    QCOMPARE(probe.compiledVersionNum(), LIBCURL_VERSION_NUM);
    QVERIFY(probe.runtimeVersionNum() >= 0x080000);
    QVERIFY(!probe.runtimeVersionString().isEmpty());
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
