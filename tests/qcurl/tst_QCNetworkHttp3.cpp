/**
 * @file tst_QCNetworkHttp3.cpp
 * @brief HTTP/3 运行时语义门禁（离线）
 *
 * 说明：
 * - 不依赖外网服务；真实 h3 成功路径由 `tests/libcurl_consistency/`（curl testenv + nghttpx-h3）覆盖。
 * - 这里仅固化 QCurl 在不同运行时能力下的行为：Http3Only 的失败语义、Http3 的降级语义。
 */

#include "CurlFeatureProbe.h"
#include "QCBlockingNetworkClient.h"
#include "QCNetworkAccessManager.h"
#include "QCNetworkHttpVersion.h"
#include "QCNetworkMockHandler.h"
#include "QCNetworkReply.h"
#include "QCNetworkRequest.h"
#include "private/QCNetworkHttpVersion_p.h"
#include "qcnetwork_managed_reply_wait_helper.h"
#include "qcnetwork_mock_test_support.h"

#include <QScopeGuard>
#include <QScopedPointer>
#include <QtTest/QtTest>

#include <curl/curl.h>

using namespace QCurl;

namespace {

bool runtimeSupportsHttp3()
{
#ifdef CURL_VERSION_HTTP3
    auto *ver = curl_version_info(CURLVERSION_NOW);
    return ver && ((ver->features & CURL_VERSION_HTTP3) != 0);
#else
    return false;
#endif
}

} // namespace

class TestQCNetworkHttp3 : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void testHttp3EnumConversion();
    void testHttp3OnlyRuntimeGating();
    void testHttp3DowngradeWhenUnsupported();
    void testRequireHttp3EnvGate();
    void testRuntimeCapabilityMatrix_data();
    void testRuntimeCapabilityMatrix();
    void testMissingHttp3RejectsAllExecutors();
};

void TestQCNetworkHttp3::testHttp3EnumConversion()
{
    QVERIFY(detail::toCurlHttpVersion(QCNetworkHttpVersion::Http1_0) == CURL_HTTP_VERSION_1_0);
    QVERIFY(detail::toCurlHttpVersion(QCNetworkHttpVersion::Http1_1) == CURL_HTTP_VERSION_1_1);
    QVERIFY(detail::toCurlHttpVersion(QCNetworkHttpVersion::Http2) == CURL_HTTP_VERSION_2_0);
    QVERIFY(detail::toCurlHttpVersion(QCNetworkHttpVersion::Http2TLS) == CURL_HTTP_VERSION_2TLS);
    QVERIFY(detail::toCurlHttpVersion(QCNetworkHttpVersion::HttpAny) == CURL_HTTP_VERSION_NONE);
    QCOMPARE(detail::toCurlHttpVersion(QCNetworkHttpVersion::Http3), long(CURL_HTTP_VERSION_3));
#if LIBCURL_VERSION_NUM >= 0x075800
    QCOMPARE(detail::toCurlHttpVersion(QCNetworkHttpVersion::Http3Only),
             long(CURL_HTTP_VERSION_3ONLY));
#else
    QCOMPARE(detail::toCurlHttpVersion(QCNetworkHttpVersion::Http3Only), -1L);
#endif
}

void TestQCNetworkHttp3::testHttp3OnlyRuntimeGating()
{
    QCNetworkAccessManager manager;
    QCNetworkMockHandler mock;
    QCurl::TestSupport::setMockHandler(manager, &mock);

    const QUrl url("https://example.com/offline/http3_only");
    mock.enqueueResponse(HttpMethod::Get, url, QByteArray("ok"), 200);

    QCNetworkRequest req(url);
    req.setHttpVersion(QCNetworkHttpVersion::Http3Only);

    QScopedPointer<QCNetworkReply> reply(TestSupport::sendWaitedAsyncTestReply(manager, req));
    QVERIFY(reply);

    if (runtimeSupportsHttp3() && detail::toCurlHttpVersion(QCNetworkHttpVersion::Http3Only) >= 0
        && CurlFeatureProbe::instance().runtimeVersionNum() >= 0x075800) {
        QCOMPARE(reply->error(), NetworkError::NoError);
    } else {
        QCOMPARE(reply->error(), NetworkError::InvalidRequest);
    }
}

void TestQCNetworkHttp3::testRuntimeCapabilityMatrix_data()
{
    QTest::addColumn<int>("version");
    QTest::addColumn<int>("features");
    QTest::addColumn<bool>("only");
    QTest::addColumn<bool>("required");
    QTest::addColumn<bool>("accepted");
    QTest::newRow("runtime-no-http3") << 0x080000 << 0 << false << false << true;
    QTest::newRow("only-runtime-no-http3") << 0x080000 << 0 << true << false << false;
    QTest::newRow("only-runtime-before-7.88")
        << 0x075500 << int(CURL_VERSION_HTTP3) << true << false << false;
    QTest::newRow("http3-runtime-7.85")
        << 0x075500 << int(CURL_VERSION_HTTP3) << false << false << true;
    QTest::newRow("only-runtime-7.88") << 0x075800 << int(CURL_VERSION_HTTP3) << true << false
                                       << (LIBCURL_VERSION_NUM >= 0x075800);
    QTest::newRow("require-runtime-no-http3") << 0x080000 << 0 << false << true << false;
}

void TestQCNetworkHttp3::testRuntimeCapabilityMatrix()
{
    QFETCH(int, version);
    QFETCH(int, features);
    QFETCH(bool, only);
    QFETCH(bool, required);
    QFETCH(bool, accepted);
    const QByteArray oldVersion  = qgetenv("QCURL_TEST_FORCE_RUNTIME_LIBCURL_VERSION_NUM");
    const QByteArray oldFeatures = qgetenv("QCURL_TEST_FORCE_RUNTIME_LIBCURL_FEATURES");
    const QByteArray oldRequired = qgetenv("QCURL_REQUIRE_HTTP3");
    const auto restore           = qScopeGuard([&]() {
        qputenv("QCURL_TEST_FORCE_RUNTIME_LIBCURL_VERSION_NUM", oldVersion);
        qputenv("QCURL_TEST_FORCE_RUNTIME_LIBCURL_FEATURES", oldFeatures);
        qputenv("QCURL_REQUIRE_HTTP3", oldRequired);
        CurlFeatureProbe::instance().refreshForTesting();
    });
    qputenv("QCURL_TEST_FORCE_RUNTIME_LIBCURL_VERSION_NUM", QByteArray::number(version));
    qputenv("QCURL_TEST_FORCE_RUNTIME_LIBCURL_FEATURES", QByteArray::number(features));
    qputenv("QCURL_REQUIRE_HTTP3", required ? "1" : "0");
    CurlFeatureProbe::instance().refreshForTesting();
    const auto requested = only ? QCNetworkHttpVersion::Http3Only : QCNetworkHttpVersion::Http3;
    QCNetworkHttpVersion effective = QCNetworkHttpVersion::HttpAny;
    QString error;
    QString warning;
    QCOMPARE(detail::resolveHttpVersion(requested, &effective, &error, &warning), accepted);
    QCOMPARE(error.isEmpty(), accepted);
    if (accepted) {
        QCOMPARE(effective, features ? requested : QCNetworkHttpVersion::Http2TLS);
        QCOMPARE(warning.isEmpty(), features != 0);
    }
}

void TestQCNetworkHttp3::testMissingHttp3RejectsAllExecutors()
{
    const QByteArray previous = qgetenv("QCURL_TEST_FORCE_RUNTIME_LIBCURL_FEATURES");
    const auto restore        = qScopeGuard([previous]() {
        qputenv("QCURL_TEST_FORCE_RUNTIME_LIBCURL_FEATURES", previous);
        CurlFeatureProbe::instance().refreshForTesting();
    });
    qputenv("QCURL_TEST_FORCE_RUNTIME_LIBCURL_FEATURES", "0");
    CurlFeatureProbe::instance().refreshForTesting();
    QCNetworkRequest request(QUrl("https://example.invalid/http3-only"));
    request.setHttpVersion(QCNetworkHttpVersion::Http3Only);
    request.setTimeout(std::chrono::seconds(2));
    QCNetworkAccessManager manager;
    QCNetworkMockHandler mock;
    mock.mockResponse(HttpMethod::Get, request.url(), "unexpected", 200);
    TestSupport::setMockHandler(manager, &mock);
    for (const bool scheduled : {false, true}) {
        manager.enableRequestScheduler(scheduled);
        auto *reply = manager.get(request);
        QVERIFY(!reply->isFinished());
        QSignalSpy finished(reply, &QCNetworkReply::finished);
        QVERIFY(finished.wait());
        QCOMPARE(reply->error(), NetworkError::InvalidRequest);
        QVERIFY(reply->errorString().contains(QStringLiteral("Http3Only")));
        QCOMPARE(finished.size(), 1);
    }
    QVERIFY(mock.takeCapturedRequests().isEmpty());
    QCBlockingNetworkClient::Options options;
    options.setApplicationThreadPolicy(
        QCBlockingNetworkClient::ApplicationThreadPolicy::AllowForCliOrTests);
    const auto result = QCBlockingNetworkClient(options).get(request);
    QCOMPARE(result.error(), NetworkError::UnsupportedCapability);
    QVERIFY(result.errorMessage().contains(QStringLiteral("Http3Only")));
    QCOMPARE(result.diagnosticCurlCode(), 0);
}

void TestQCNetworkHttp3::testHttp3DowngradeWhenUnsupported()
{
    QCNetworkAccessManager manager;
    QCNetworkMockHandler mock;
    QCurl::TestSupport::setMockHandler(manager, &mock);

    const QUrl url("https://example.com/offline/http3_try");
    mock.enqueueResponse(HttpMethod::Get, url, QByteArray("ok"), 200);

    QCNetworkRequest req(url);
    req.setHttpVersion(QCNetworkHttpVersion::Http3);

    QScopedPointer<QCNetworkReply> reply(TestSupport::sendWaitedAsyncTestReply(manager, req));
    QVERIFY(reply);
    QCOMPARE(reply->error(), NetworkError::NoError);

    if (!runtimeSupportsHttp3()) {
        const QStringList warnings = reply->capabilityWarnings();
        QVERIFY(!warnings.isEmpty());
    }
}

void TestQCNetworkHttp3::testRequireHttp3EnvGate()
{
    const QByteArray old = qgetenv("QCURL_REQUIRE_HTTP3");
    qputenv("QCURL_REQUIRE_HTTP3", "1");

    QCNetworkAccessManager manager;
    QCNetworkMockHandler mock;
    QCurl::TestSupport::setMockHandler(manager, &mock);

    const QUrl url("https://example.com/offline/http3_require_env");
    mock.enqueueResponse(HttpMethod::Get, url, QByteArray("ok"), 200);

    QCNetworkRequest req(url);
    req.setHttpVersion(QCNetworkHttpVersion::Http3);

    QScopedPointer<QCNetworkReply> reply(TestSupport::sendWaitedAsyncTestReply(manager, req));
    QVERIFY(reply);

    if (runtimeSupportsHttp3()) {
        QCOMPARE(reply->error(), NetworkError::NoError);
    } else {
        QCOMPARE(reply->error(), NetworkError::InvalidRequest);
    }

    if (old.isEmpty()) {
        qunsetenv("QCURL_REQUIRE_HTTP3");
    } else {
        qputenv("QCURL_REQUIRE_HTTP3", old);
    }
}

QTEST_MAIN(TestQCNetworkHttp3)

#include "tst_QCNetworkHttp3.moc"
