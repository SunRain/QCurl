/**
 * @file tst_QCNetworkHttp3.cpp
 * @brief HTTP/3 运行时语义门禁（离线）
 *
 * 说明：
 * - 不依赖外网服务；真实 h3 成功路径由 `tests/libcurl_consistency/`（curl testenv + nghttpx-h3）覆盖。
 * - 这里仅固化 QCurl 在不同运行时能力下的行为：Http3Only 的失败语义、Http3 的降级语义。
 */

#include "QCNetworkAccessManager.h"
#include "QCNetworkHttpVersion.h"
#include "QCNetworkMockHandler.h"
#include "QCNetworkReply.h"
#include "QCNetworkRequest.h"

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

private slots:
    void testHttp3EnumConversion();
    void testHttp3OnlyRuntimeGating();
    void testHttp3DowngradeWhenUnsupported();
    void testRequireHttp3EnvGate();
};

void TestQCNetworkHttp3::testHttp3EnumConversion()
{
    QVERIFY(toCurlHttpVersion(QCNetworkHttpVersion::Http1_0) == CURL_HTTP_VERSION_1_0);
    QVERIFY(toCurlHttpVersion(QCNetworkHttpVersion::Http1_1) == CURL_HTTP_VERSION_1_1);
    QVERIFY(toCurlHttpVersion(QCNetworkHttpVersion::Http2) == CURL_HTTP_VERSION_2_0);
    QVERIFY(toCurlHttpVersion(QCNetworkHttpVersion::Http2TLS) == CURL_HTTP_VERSION_2TLS);
    QVERIFY(toCurlHttpVersion(QCNetworkHttpVersion::HttpAny) == CURL_HTTP_VERSION_NONE);
}

void TestQCNetworkHttp3::testHttp3OnlyRuntimeGating()
{
    QCNetworkAccessManager manager;
    QCNetworkMockHandler mock;
    manager.setMockHandler(&mock);

    const QUrl url("https://example.com/offline/http3_only");
    mock.enqueueResponse(HttpMethod::Get, url, QByteArray("ok"), 200);

    QCNetworkRequest req(url);
    req.setHttpVersion(QCNetworkHttpVersion::Http3Only);

    QScopedPointer<QCNetworkReply> reply(manager.sendGetSync(req));
    QVERIFY(reply);

    if (runtimeSupportsHttp3()) {
        QCOMPARE(reply->error(), NetworkError::NoError);
#if !defined(CURL_HTTP_VERSION_3ONLY)
        QVERIFY(!reply->capabilityWarnings().isEmpty());
#endif
    } else {
        QCOMPARE(reply->error(), NetworkError::InvalidRequest);
    }
}

void TestQCNetworkHttp3::testHttp3DowngradeWhenUnsupported()
{
    QCNetworkAccessManager manager;
    QCNetworkMockHandler mock;
    manager.setMockHandler(&mock);

    const QUrl url("https://example.com/offline/http3_try");
    mock.enqueueResponse(HttpMethod::Get, url, QByteArray("ok"), 200);

    QCNetworkRequest req(url);
    req.setHttpVersion(QCNetworkHttpVersion::Http3);

    QScopedPointer<QCNetworkReply> reply(manager.sendGetSync(req));
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
    manager.setMockHandler(&mock);

    const QUrl url("https://example.com/offline/http3_require_env");
    mock.enqueueResponse(HttpMethod::Get, url, QByteArray("ok"), 200);

    QCNetworkRequest req(url);
    req.setHttpVersion(QCNetworkHttpVersion::Http3);

    QScopedPointer<QCNetworkReply> reply(manager.sendGetSync(req));
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
