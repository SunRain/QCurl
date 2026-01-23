// SPDX-License-Identifier: MIT
// Copyright (c) 2025 QCurl Project

#include <QtTest/QtTest>

#include "QCNetworkRequest.h"
#include "QCNetworkReply.h"
#include "QCNetworkReply_p.h"
#include "QCNetworkSslConfig.h"

#include <curl/curlver.h>

using namespace QCurl;

class TestQCNetworkNetworkPath : public QObject
{
    Q_OBJECT

private slots:
    void testDefaults();
    void testSettersAndGetters();
    void testInvalidInputs();
    void testConfigureCurlOptionsSmoke();
    void testProtocolAllowlistCapabilityPolicy();
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

    request.setAllowedProtocols(QStringList{QStringLiteral("http"), QStringLiteral("https")});
    QVERIFY(request.allowedProtocols().has_value());
    QCOMPARE(request.allowedProtocols()->size(), 2);

    request.setAllowedRedirectProtocols(QStringList{QStringLiteral("https")});
    QVERIFY(request.allowedRedirectProtocols().has_value());
    QCOMPARE(request.allowedRedirectProtocols()->size(), 1);

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

    request.setAllowedProtocols(QStringList{QString(), QStringLiteral("   ")});
    QVERIFY(!request.allowedProtocols().has_value());

    request.setAllowedRedirectProtocols(QStringList{QString(), QStringLiteral("   ")});
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
    request.setAllowedProtocols(QStringList{QStringLiteral("http"), QStringLiteral("https")});
    request.setAllowedRedirectProtocols(QStringList{QStringLiteral("https")});

    // 仅构造，触发 configureCurlOptions（不发起网络请求）
    QCNetworkReply reply(request, HttpMethod::Get, ExecutionMode::Sync, QByteArray(), nullptr);
    QVERIFY(reply.state() != ReplyState::Error);

    // 不应包含敏感明文
    const QStringList warnings = reply.capabilityWarnings();
    for (const QString &w : warnings) {
        QVERIFY2(!w.contains(QStringLiteral("Authorization"), Qt::CaseInsensitive),
                 qPrintable(QStringLiteral("capability warning contains sensitive key: %1").arg(w)));
        QVERIFY2(!w.contains(QStringLiteral("Cookie"), Qt::CaseInsensitive),
                 qPrintable(QStringLiteral("capability warning contains sensitive key: %1").arg(w)));
    }
}

void TestQCNetworkNetworkPath::testProtocolAllowlistCapabilityPolicy()
{
    const QByteArray oldEnv = qgetenv("QCURL_TEST_FORCE_CAPABILITY_ERROR");

    {
        // STR 不可用时应 fallback 到 legacy bitmask（仍然 enforce，Fail 策略不应 hard fail）。
        qputenv("QCURL_TEST_FORCE_CAPABILITY_ERROR", QByteArray("CURLOPT_PROTOCOLS_STR"));

        QCNetworkRequest request(QUrl(QStringLiteral("https://example.com/")));
        request.setAllowedProtocols(QStringList{QStringLiteral("https")});
        request.setUnsupportedSecurityOptionPolicy(QCUnsupportedSecurityOptionPolicy::Fail);

        QCNetworkReplyPrivate replyPrivate(nullptr,
                                           request,
                                           HttpMethod::Get,
                                           ExecutionMode::Sync,
                                           QByteArray());
        QVERIFY(replyPrivate.configureCurlOptions());
        QCOMPARE(replyPrivate.errorCode, NetworkError::NoError);
        QVERIFY(replyPrivate.capabilityWarnings.join(QStringLiteral("\n"))
                    .contains(QStringLiteral("fallback to CURLOPT_PROTOCOLS")));
    }

    {
        // Warn 策略同样应走 fallback，并形成 capability warning（不影响成功）。
        qputenv("QCURL_TEST_FORCE_CAPABILITY_ERROR", QByteArray("CURLOPT_PROTOCOLS_STR"));

        QCNetworkRequest request(QUrl(QStringLiteral("https://example.com/")));
        request.setAllowedProtocols(QStringList{QStringLiteral("https")});
        request.setUnsupportedSecurityOptionPolicy(QCUnsupportedSecurityOptionPolicy::Warn);

        QCNetworkReplyPrivate replyPrivate(nullptr,
                                           request,
                                           HttpMethod::Get,
                                           ExecutionMode::Sync,
                                           QByteArray());
        QVERIFY(replyPrivate.configureCurlOptions());
        QCOMPARE(replyPrivate.errorCode, NetworkError::NoError);
        QVERIFY(replyPrivate.capabilityWarnings.join(QStringLiteral("\n")).contains(QStringLiteral("CURLOPT_PROTOCOLS_STR")));
    }

    {
        // STR 与 legacy 均不可用时：Fail 策略必须 hard fail（回归：禁止 silent pass）。
        qputenv("QCURL_TEST_FORCE_CAPABILITY_ERROR",
                QByteArray("CURLOPT_PROTOCOLS_STR,CURLOPT_PROTOCOLS"));

        QCNetworkRequest request(QUrl(QStringLiteral("https://example.com/")));
        request.setAllowedProtocols(QStringList{QStringLiteral("https")});
        request.setUnsupportedSecurityOptionPolicy(QCUnsupportedSecurityOptionPolicy::Fail);

        QCNetworkReplyPrivate replyPrivate(nullptr,
                                           request,
                                           HttpMethod::Get,
                                           ExecutionMode::Sync,
                                           QByteArray());
        QVERIFY(!replyPrivate.configureCurlOptions());
        QCOMPARE(replyPrivate.errorCode, NetworkError::InvalidRequest);
        QVERIFY(replyPrivate.errorMessage.contains(QStringLiteral("CURLOPT_PROTOCOLS")));
    }

    if (oldEnv.isEmpty()) {
        qunsetenv("QCURL_TEST_FORCE_CAPABILITY_ERROR");
    } else {
        qputenv("QCURL_TEST_FORCE_CAPABILITY_ERROR", oldEnv);
    }
}

QTEST_MAIN(TestQCNetworkNetworkPath)

#include "tst_QCNetworkNetworkPath.moc"
