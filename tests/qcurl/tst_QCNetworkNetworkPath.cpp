// SPDX-License-Identifier: MIT
// Copyright (c) 2025 QCurl Project

#include "CurlFeatureProbe.h"
#include "QCNetworkAccessManager.h"
#include "QCNetworkReply.h"
#include "QCNetworkReply_p.h"
#include "QCNetworkRequest.h"
#include "QCNetworkSslConfig.h"

#include <QScopeGuard>
#include <QtTest/QtTest>

using namespace QCurl;

class TestQCNetworkNetworkPath : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void testDefaults();
    void testSettersAndGetters();
    void testInvalidInputs();
    void testConfigureCurlOptionsSmoke();
    void testSlistBuildFailureIsTerminal();
    void testHeaderAppendFailureIsTerminal();
    void testRequiredSetoptFailureIsTerminal();
    void testMinimumRuntimeGate();
    void testProtocolAllowlistCapabilityPolicy();
    void testCoreProtocolOptionsAreMandatory();
    void testCoreEntryRejectsNonHttpScheme();
    void testExplicitProtocolCannotExpandCore();
};

void TestQCNetworkNetworkPath::testDefaults()
{
    QCNetworkRequest request(QUrl(QStringLiteral("https://example.com/")));

    QVERIFY(!request.ipResolve().has_value());
    QVERIFY(!request.happyEyeballsTimeout().has_value());
    QVERIFY(!request.networkInterface().has_value());
    QVERIFY(!request.localPort().has_value());
    QVERIFY(!request.localPortRange().has_value());
    QVERIFY(!request.resolveOverride().has_value());
    QVERIFY(!request.connectTo().has_value());
    QVERIFY(!request.dnsServers().has_value());
    QVERIFY(!request.dohUrl().has_value());

    QVERIFY(!request.allowedProtocols().has_value());
    QVERIFY(!request.allowedRedirectProtocols().has_value());
    QCOMPARE(request.unsupportedSecurityOptionPolicy(), QCUnsupportedSecurityOptionPolicy::Fail);
}

void TestQCNetworkNetworkPath::testSettersAndGetters()
{
    QCNetworkRequest request(QUrl(QStringLiteral("https://example.com/")));

    request.setIpResolve(QCNetworkIpResolve::Ipv4);
    QVERIFY(request.ipResolve().has_value());
    QCOMPARE(request.ipResolve().value(), QCNetworkIpResolve::Ipv4);

    request.setHappyEyeballsTimeout(std::chrono::milliseconds(123));
    QVERIFY(request.happyEyeballsTimeout().has_value());
    QCOMPARE(request.happyEyeballsTimeout()->count(), 123);

    request.setNetworkInterface(QStringLiteral("lo"));
    QVERIFY(request.networkInterface().has_value());
    QCOMPARE(request.networkInterface().value(), QStringLiteral("lo"));

    request.setLocalPortRange(12345, 10);
    QVERIFY(request.localPort().has_value());
    QVERIFY(request.localPortRange().has_value());
    QCOMPARE(request.localPort().value(), 12345);
    QCOMPARE(request.localPortRange().value(), 10);

    request.setResolveOverride(QStringList{
        QStringLiteral("example.com:443:127.0.0.1"),
    });
    QVERIFY(request.resolveOverride().has_value());
    QCOMPARE(request.resolveOverride()->size(), 1);

    request.setConnectTo(QStringList{
        QStringLiteral("example.com:443:127.0.0.1:443"),
    });
    QVERIFY(request.connectTo().has_value());
    QCOMPARE(request.connectTo()->size(), 1);

    request.setDnsServers(QStringList{
        QStringLiteral("8.8.8.8"),
        QStringLiteral("1.1.1.1"),
    });
    QVERIFY(request.dnsServers().has_value());
    QCOMPARE(request.dnsServers()->size(), 2);

    request.setDohUrl(QUrl(QStringLiteral("https://doh.example/dns-query")));
    QVERIFY(request.dohUrl().has_value());
    QCOMPARE(request.dohUrl()->toString(), QStringLiteral("https://doh.example/dns-query"));

    request.setAllowedProtocols(QCurl::QCNetworkProtocol::Http | QCurl::QCNetworkProtocol::Https);
    QVERIFY(request.allowedProtocols().has_value());
    QCOMPARE(request.allowedProtocols().value(), QCNetworkProtocol::Http | QCNetworkProtocol::Https);

    request.setAllowedRedirectProtocols(QCurl::QCNetworkProtocol::Https);
    QVERIFY(request.allowedRedirectProtocols().has_value());
    QCOMPARE(request.allowedRedirectProtocols().value(),
             QCNetworkProtocols(QCNetworkProtocol::Https));

    request.setUnsupportedSecurityOptionPolicy(QCUnsupportedSecurityOptionPolicy::Warn);
    QCOMPARE(request.unsupportedSecurityOptionPolicy(), QCUnsupportedSecurityOptionPolicy::Warn);
}

void TestQCNetworkNetworkPath::testInvalidInputs()
{
    QCNetworkRequest request(QUrl(QStringLiteral("https://example.com/")));

    request.setIpResolve(QCNetworkIpResolve::Any);
    QVERIFY(!request.ipResolve().has_value());

    request.setHappyEyeballsTimeout(std::chrono::milliseconds(-1));
    QVERIFY(!request.happyEyeballsTimeout().has_value());

    request.setNetworkInterface(QString());
    QVERIFY(!request.networkInterface().has_value());

    request.setLocalPortRange(0, 0);
    QVERIFY(!request.localPort().has_value());
    QVERIFY(!request.localPortRange().has_value());

    request.setLocalPortRange(70000, 0);
    QVERIFY(!request.localPort().has_value());
    QVERIFY(!request.localPortRange().has_value());

    request.setResolveOverride(QStringList{QString(), QStringLiteral("   ")});
    QVERIFY(!request.resolveOverride().has_value());

    request.setConnectTo(QStringList{QString(), QStringLiteral("   ")});
    QVERIFY(!request.connectTo().has_value());

    request.setDnsServers(QStringList{QString(), QStringLiteral("   ")});
    QVERIFY(!request.dnsServers().has_value());

    request.setDohUrl(QUrl());
    QVERIFY(!request.dohUrl().has_value());

    request.setAllowedProtocols({});
    QVERIFY(!request.allowedProtocols().has_value());

    request.setAllowedRedirectProtocols({});
    QVERIFY(!request.allowedRedirectProtocols().has_value());
}

void TestQCNetworkNetworkPath::testConfigureCurlOptionsSmoke()
{
    QCNetworkRequest request(QUrl(QStringLiteral("https://example.com/")));
    request.setUnsupportedSecurityOptionPolicy(QCUnsupportedSecurityOptionPolicy::Warn);

    request.setIpResolve(QCNetworkIpResolve::Ipv4);
    request.setHappyEyeballsTimeout(std::chrono::milliseconds(200));
    request.setNetworkInterface(QStringLiteral("lo"));
    request.setLocalPortRange(12345, 1);
    request.setResolveOverride(QStringList{QStringLiteral("example.com:443:127.0.0.1")});
    request.setConnectTo(QStringList{QStringLiteral("example.com:443:127.0.0.1:443")});
    request.setDnsServers(QStringList{QStringLiteral("8.8.8.8")});
    request.setDohUrl(QUrl(QStringLiteral("https://doh.example/dns-query")));
    request.setAllowedProtocols(QCurl::QCNetworkProtocol::Http | QCurl::QCNetworkProtocol::Https);
    request.setAllowedRedirectProtocols(QCurl::QCNetworkProtocol::Https);

    // 触发 configureCurlOptions（不发起网络请求）
    QCNetworkReplyPrivate replyPrivate(nullptr,
                                       request,
                                       HttpMethod::Get,
                                       Internal::makeEmptyRequestBody(),
                                       QByteArray());
    QVERIFY(replyPrivate.configureCurlOptions());
    QCOMPARE(replyPrivate.errorCode, NetworkError::NoError);

    // 不应包含敏感明文
    const QStringList warnings = replyPrivate.capabilityWarnings;
    for (const QString &w : warnings) {
        QVERIFY2(!w.contains(QStringLiteral("Authorization"), Qt::CaseInsensitive),
                 qPrintable(QStringLiteral("capability warning contains sensitive key: %1").arg(w)));
        QVERIFY2(!w.contains(QStringLiteral("Cookie"), Qt::CaseInsensitive),
                 qPrintable(QStringLiteral("capability warning contains sensitive key: %1").arg(w)));
    }
}

void TestQCNetworkNetworkPath::testSlistBuildFailureIsTerminal()
{
    const QByteArray oldEnv = qgetenv("QCURL_TEST_FORCE_SLIST_APPEND_ERROR");
    const auto restoreEnv   = qScopeGuard([oldEnv]() {
        if (oldEnv.isEmpty()) {
            qunsetenv("QCURL_TEST_FORCE_SLIST_APPEND_ERROR");
        } else {
            qputenv("QCURL_TEST_FORCE_SLIST_APPEND_ERROR", oldEnv);
        }
    });

    qputenv("QCURL_TEST_FORCE_SLIST_APPEND_ERROR", QByteArrayLiteral("CURLOPT_RESOLVE:2"));

    QCNetworkRequest request(QUrl(QStringLiteral("https://example.com/")));
    request.setResolveOverride(QStringList{
        QStringLiteral("example.com:443:127.0.0.1"),
        QStringLiteral("example.net:443:127.0.0.1"),
    });

    QCNetworkReplyPrivate replyPrivate(nullptr,
                                       request,
                                       HttpMethod::Get,
                                       Internal::makeEmptyRequestBody(),
                                       QByteArray());
    QVERIFY(!replyPrivate.configureCurlOptions());
    QCOMPARE(replyPrivate.errorCode, NetworkError::InvalidRequest);
    QVERIFY(replyPrivate.errorMessage.contains(QStringLiteral("CURLOPT_RESOLVE")));
    QVERIFY(replyPrivate.resolveSlist == nullptr);
}

void TestQCNetworkNetworkPath::testHeaderAppendFailureIsTerminal()
{
    const QByteArray oldEnv = qgetenv("QCURL_TEST_FORCE_SLIST_APPEND_ERROR");
    const auto restoreEnv   = qScopeGuard([oldEnv]() {
        if (oldEnv.isEmpty()) {
            qunsetenv("QCURL_TEST_FORCE_SLIST_APPEND_ERROR");
        } else {
            qputenv("QCURL_TEST_FORCE_SLIST_APPEND_ERROR", oldEnv);
        }
    });

    qputenv("QCURL_TEST_FORCE_SLIST_APPEND_ERROR", QByteArrayLiteral("CURLOPT_HTTPHEADER"));

    QCNetworkRequest request(QUrl(QStringLiteral("https://example.com/")));
    request.setRawHeader(QByteArrayLiteral("Authorization"), QByteArrayLiteral("redacted"));

    QCNetworkReplyPrivate replyPrivate(nullptr,
                                       request,
                                       HttpMethod::Get,
                                       Internal::makeEmptyRequestBody(),
                                       QByteArray());
    QVERIFY(!replyPrivate.configureCurlOptions());
    QCOMPARE(replyPrivate.errorCode, NetworkError::InvalidRequest);
    QVERIFY(replyPrivate.errorMessage.contains(QStringLiteral("CURLOPT_HTTPHEADER")));
    QVERIFY(replyPrivate.curlManager.headerList() == nullptr);
}

void TestQCNetworkNetworkPath::testRequiredSetoptFailureIsTerminal()
{
    const QByteArray oldEnv = qgetenv("QCURL_TEST_FORCE_SETOPT_ERROR");
    const auto restoreEnv   = qScopeGuard([oldEnv]() {
        if (oldEnv.isEmpty()) {
            qunsetenv("QCURL_TEST_FORCE_SETOPT_ERROR");
        } else {
            qputenv("QCURL_TEST_FORCE_SETOPT_ERROR", oldEnv);
        }
    });

    const QList<QByteArray> requiredOptions{
        QByteArrayLiteral("CURLOPT_URL"),
        QByteArrayLiteral("CURLOPT_PROXY"),
        QByteArrayLiteral("CURLOPT_WRITEFUNCTION"),
    };
    for (const QByteArray &optionName : requiredOptions) {
        qputenv("QCURL_TEST_FORCE_SETOPT_ERROR", optionName);

        QCNetworkRequest request(QUrl(QStringLiteral("https://example.com/")));
        QCNetworkReplyPrivate replyPrivate(nullptr,
                                           request,
                                           HttpMethod::Get,
                                           Internal::makeEmptyRequestBody(),
                                           QByteArray());
        QVERIFY2(!replyPrivate.configureCurlOptions(), optionName.constData());
        QCOMPARE(replyPrivate.errorCode, NetworkError::InvalidRequest);
        QVERIFY2(replyPrivate.errorMessage.contains(QString::fromUtf8(optionName)),
                 qPrintable(replyPrivate.errorMessage));
    }
}

void TestQCNetworkNetworkPath::testProtocolAllowlistCapabilityPolicy()
{
    const QByteArray oldEnv = qgetenv("QCURL_TEST_FORCE_CAPABILITY_ERROR");

    {
        // Fail 策略：direct allowlist capability 缺失时必须 hard fail，且文案不能泄露白名单内容。
        qputenv("QCURL_TEST_FORCE_CAPABILITY_ERROR", QByteArray("CURLOPT_PROTOCOLS_STR"));

        QCNetworkRequest request(QUrl(QStringLiteral("https://example.com/")));
        request.setAllowedProtocols(QCurl::QCNetworkProtocol::Https);
        request.setUnsupportedSecurityOptionPolicy(QCUnsupportedSecurityOptionPolicy::Fail);

        QCNetworkReplyPrivate replyPrivate(nullptr,
                                           request,
                                           HttpMethod::Get,
                                           Internal::makeEmptyRequestBody(),
                                           QByteArray());
        QVERIFY(!replyPrivate.configureCurlOptions());
        QCOMPARE(replyPrivate.errorCode, NetworkError::InvalidRequest);
        QVERIFY(replyPrivate.errorMessage.contains(QStringLiteral("CURLOPT_PROTOCOLS_STR")));
    }

    {
        // Core 协议边界不可降级：即使调用方选择 Warn，也必须 fail-closed。
        qputenv("QCURL_TEST_FORCE_CAPABILITY_ERROR", QByteArray("CURLOPT_PROTOCOLS_STR"));

        QCNetworkRequest request(QUrl(QStringLiteral("https://example.com/")));
        request.setAllowedProtocols(QCurl::QCNetworkProtocol::Https);
        request.setUnsupportedSecurityOptionPolicy(QCUnsupportedSecurityOptionPolicy::Warn);

        QCNetworkReplyPrivate replyPrivate(nullptr,
                                           request,
                                           HttpMethod::Get,
                                           Internal::makeEmptyRequestBody(),
                                           QByteArray());
        QVERIFY(!replyPrivate.configureCurlOptions());
        QCOMPARE(replyPrivate.errorCode, NetworkError::InvalidRequest);
        QVERIFY(replyPrivate.errorMessage.contains(QStringLiteral("CURLOPT_PROTOCOLS_STR")));
    }

    {
        // Fail 策略：redirect allowlist capability 缺失时必须 hard fail。
        qputenv("QCURL_TEST_FORCE_CAPABILITY_ERROR",
                QByteArray("CURLOPT_REDIR_PROTOCOLS_STR"));

        QCNetworkRequest request(QUrl(QStringLiteral("https://example.com/")));
        request.setAllowedRedirectProtocols(QCurl::QCNetworkProtocol::Https);
        request.setUnsupportedSecurityOptionPolicy(QCUnsupportedSecurityOptionPolicy::Fail);

        QCNetworkReplyPrivate replyPrivate(nullptr,
                                           request,
                                           HttpMethod::Get,
                                           Internal::makeEmptyRequestBody(),
                                           QByteArray());
        QVERIFY(!replyPrivate.configureCurlOptions());
        QCOMPARE(replyPrivate.errorCode, NetworkError::InvalidRequest);
        QVERIFY(replyPrivate.errorMessage.contains(QStringLiteral("CURLOPT_REDIR_PROTOCOLS_STR")));
    }

    {
        // Core 重定向协议边界不可降级：Warn 也不能回退到 libcurl 默认值。
        qputenv("QCURL_TEST_FORCE_CAPABILITY_ERROR",
                QByteArray("CURLOPT_REDIR_PROTOCOLS_STR"));

        QCNetworkRequest request(QUrl(QStringLiteral("https://example.com/")));
        request.setAllowedRedirectProtocols(QCurl::QCNetworkProtocol::Https);
        request.setUnsupportedSecurityOptionPolicy(QCUnsupportedSecurityOptionPolicy::Warn);

        QCNetworkReplyPrivate replyPrivate(nullptr,
                                           request,
                                           HttpMethod::Get,
                                           Internal::makeEmptyRequestBody(),
                                           QByteArray());
        QVERIFY(!replyPrivate.configureCurlOptions());
        QCOMPARE(replyPrivate.errorCode, NetworkError::InvalidRequest);
        QVERIFY(replyPrivate.errorMessage.contains(QStringLiteral("CURLOPT_REDIR_PROTOCOLS_STR")));
    }

    if (oldEnv.isEmpty()) {
        qunsetenv("QCURL_TEST_FORCE_CAPABILITY_ERROR");
    } else {
        qputenv("QCURL_TEST_FORCE_CAPABILITY_ERROR", oldEnv);
    }
}

void TestQCNetworkNetworkPath::testCoreProtocolOptionsAreMandatory()
{
    const QByteArray oldEnv = qgetenv("QCURL_TEST_FORCE_CAPABILITY_ERROR");
    const auto restoreEnv   = qScopeGuard([oldEnv]() {
        if (oldEnv.isEmpty()) {
            qunsetenv("QCURL_TEST_FORCE_CAPABILITY_ERROR");
        } else {
            qputenv("QCURL_TEST_FORCE_CAPABILITY_ERROR", oldEnv);
        }
    });

    qputenv("QCURL_TEST_FORCE_CAPABILITY_ERROR", QByteArrayLiteral("CURLOPT_PROTOCOLS_STR"));
    QCNetworkRequest request(QUrl(QStringLiteral("https://example.com/")));
    QCNetworkReplyPrivate replyPrivate(nullptr,
                                       request,
                                       HttpMethod::Get,
                                       Internal::makeEmptyRequestBody(),
                                       QByteArray());
    QVERIFY(!replyPrivate.configureCurlOptions());
    QCOMPARE(replyPrivate.errorCode, NetworkError::InvalidRequest);
    QVERIFY(replyPrivate.errorMessage.contains(QStringLiteral("CURLOPT_PROTOCOLS_STR")));

    qputenv("QCURL_TEST_FORCE_CAPABILITY_ERROR", QByteArrayLiteral("CURLOPT_REDIR_PROTOCOLS_STR"));
    QCNetworkReplyPrivate redirectReplyPrivate(nullptr,
                                               request,
                                               HttpMethod::Get,
                                               Internal::makeEmptyRequestBody(),
                                               QByteArray());
    QVERIFY(!redirectReplyPrivate.configureCurlOptions());
    QCOMPARE(redirectReplyPrivate.errorCode, NetworkError::InvalidRequest);
    QVERIFY(
        redirectReplyPrivate.errorMessage.contains(QStringLiteral("CURLOPT_REDIR_PROTOCOLS_STR")));
}

void TestQCNetworkNetworkPath::testCoreEntryRejectsNonHttpScheme()
{
    QCNetworkAccessManager manager;
    QCNetworkRequest request(QUrl(QStringLiteral("ftp://127.0.0.1:1/resource")));

    QCNetworkReply *reply = manager.get(request);
    QVERIFY(reply);
    QCOMPARE(reply->error(), NetworkError::InvalidRequest);
    QVERIFY(reply->errorString().contains(QStringLiteral("HTTP/HTTPS")));
    reply->deleteLater();
}

void TestQCNetworkNetworkPath::testExplicitProtocolCannotExpandCore()
{
    QCNetworkRequest request(QUrl(QStringLiteral("https://example.com/")));
    request.setAllowedProtocols(QCurl::QCNetworkProtocol::Https
                                | static_cast<QCurl::QCNetworkProtocol>(0x4));

    QCNetworkReplyPrivate replyPrivate(nullptr,
                                       request,
                                       HttpMethod::Get,
                                       Internal::makeEmptyRequestBody(),
                                       QByteArray());
    QVERIFY(!replyPrivate.configureCurlOptions());
    QCOMPARE(replyPrivate.errorCode, NetworkError::InvalidRequest);
    QVERIFY(replyPrivate.errorMessage.contains(QStringLiteral("HTTP/HTTPS")));
}

void TestQCNetworkNetworkPath::testMinimumRuntimeGate()
{
    auto &probe = CurlFeatureProbe::instance();
    const QByteArray oldEnv = qgetenv("QCURL_TEST_FORCE_RUNTIME_LIBCURL_VERSION_NUM");

    qputenv("QCURL_TEST_FORCE_RUNTIME_LIBCURL_VERSION_NUM", QByteArray("0x075400"));
    probe.refreshForTesting();

    QCNetworkRequest request(QUrl(QStringLiteral("https://example.com/")));
    QCNetworkReplyPrivate replyPrivate(nullptr,
                                       request,
                                       HttpMethod::Get,
                                       Internal::makeEmptyRequestBody(),
                                       QByteArray());
    QVERIFY(!replyPrivate.configureCurlOptions());
    QCOMPARE(replyPrivate.errorCode, NetworkError::InvalidRequest);
    QVERIFY(replyPrivate.errorMessage.contains(QStringLiteral("7.85.0")));
    QVERIFY(replyPrivate.errorMessage.contains(QStringLiteral("请升级运行时 libcurl")));

    if (oldEnv.isEmpty()) {
        qunsetenv("QCURL_TEST_FORCE_RUNTIME_LIBCURL_VERSION_NUM");
    } else {
        qputenv("QCURL_TEST_FORCE_RUNTIME_LIBCURL_VERSION_NUM", oldEnv);
    }
    probe.refreshForTesting();
}

QTEST_MAIN(TestQCNetworkNetworkPath)

#include "tst_QCNetworkNetworkPath.moc"
