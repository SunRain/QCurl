/**
 * @file tst_QCNetworkRequestWarningsOffline.cpp
 * @brief 请求配置冲突告警（capabilityWarnings）纯离线门禁（MockHandler）
 *
 * 说明：
 * - 不启动任何本地服务端，不绑定端口。
 * - 验证回放冲突的发布次数，以及真实配置失败前的混合诊断顺序。
 */

#include "QCNetworkAccessManager.h"
#include "QCNetworkHttpVersion.h"
#include "QCNetworkMockHandler.h"
#include "QCNetworkProxyConfig.h"
#include "QCNetworkReply.h"
#include "QCNetworkRequest.h"
#include "QCNetworkRequestConfig.h"
#include "qcnetwork_managed_reply_wait_helper.h"
#include "qcnetwork_mock_test_support.h"

#include <QScopeGuard>
#include <QScopedPointer>
#include <QSignalSpy>
#include <QtTest/QtTest>

using namespace QCurl;

namespace {
QStringList s_messages;

void captureConfigurationWarning(QtMsgType type, const QMessageLogContext &, const QString &message)
{
    if (type == QtWarningMsg) {
        s_messages.append(message);
    }
}

void restoreEnvironment(const char *name, const QByteArray &value)
{
    if (value.isNull()) {
        qunsetenv(name);
    } else {
        qputenv(name, value);
    }
}

QCNetworkRequest mixedWarningRequest()
{
    QCNetworkRequest request(QUrl(QStringLiteral("http://example.invalid/?query-marker")));
    request.setFollowLocation(true);
    request.setHttpVersion(QCNetworkHttpVersion::Http1_1);
    QCNetworkHttpAuthConfig auth;
    auth.setUserName(QStringLiteral("user-marker"));
    auth.setPassword(QStringLiteral("credential-marker"));
    auth.setAllowUnrestrictedAuth(true);
    request.setHttpAuth(auth);
    QCNetworkProxyConfig proxy;
    proxy.setType(QCNetworkProxyConfig::ProxyType::Http);
    request.setProxyConfig(proxy);
    return request;
}
} // namespace

class TestQCNetworkRequestWarningsOffline final : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void testAcceptEncodingConflictWarnings();
    void mixedConfigurationWarningsPrecedeFirstFatal_data();
    void mixedConfigurationWarningsPrecedeFirstFatal();
};

void TestQCNetworkRequestWarningsOffline::testAcceptEncodingConflictWarnings()
{
    QCNetworkAccessManager manager;
    QCNetworkMockHandler mock;
    QCurl::TestSupport::setMockHandler(manager, &mock);

    const QUrl url(QStringLiteral("https://example.com/offline/warnings_accept_encoding"));
    mock.mockResponse(HttpMethod::Get, url, QByteArrayLiteral("ok"), 200);

    QCNetworkRequest request(url);
    request.setRawHeader("Accept-Encoding", "gzip");
    request.setAutoDecompressionEnabled(true);

    QScopedPointer<QCNetworkReply> reply(TestSupport::sendWaitedAsyncTestReply(manager, request));
    QVERIFY(reply);
    QCOMPARE(reply->error(), NetworkError::NoError);

    const QString conflict = QStringLiteral(
        "请求配置冲突：已显式设置 Accept-Encoding header，将忽略 "
        "autoDecompression/acceptedEncodings（不会自动解压）");
    // setup 与 mock dispatch 各发布一次；迁移不能去重或再增加一次发布。
    QCOMPARE(reply->capabilityWarnings(), QStringList({conflict, conflict}));
}

void TestQCNetworkRequestWarningsOffline::mixedConfigurationWarningsPrecedeFirstFatal_data()
{
    QTest::addColumn<QByteArray>("forcedOptions");
    QTest::addColumn<QString>("firstFatal");
    QTest::newRow("proxy-before-http-version")
        << QByteArray("CURLOPT_PROXY,CURLOPT_HTTP_VERSION") << QStringLiteral("CURLOPT_PROXY");
    QTest::newRow("http-version") << QByteArray("CURLOPT_HTTP_VERSION")
                                  << QStringLiteral("CURLOPT_HTTP_VERSION");
}

void TestQCNetworkRequestWarningsOffline::mixedConfigurationWarningsPrecedeFirstFatal()
{
    QFETCH(QByteArray, forcedOptions);
    QFETCH(QString, firstFatal);
    const auto oldCapability = qgetenv("QCURL_TEST_FORCE_CAPABILITY_ERROR");
    const auto oldSetopt     = qgetenv("QCURL_TEST_FORCE_SETOPT_ERROR");
    const auto previous      = qInstallMessageHandler(captureConfigurationWarning);
    const auto restore       = qScopeGuard([=]() {
        qInstallMessageHandler(previous);
        restoreEnvironment("QCURL_TEST_FORCE_CAPABILITY_ERROR", oldCapability);
        restoreEnvironment("QCURL_TEST_FORCE_SETOPT_ERROR", oldSetopt);
    });
    s_messages.clear();
    qputenv("QCURL_TEST_FORCE_CAPABILITY_ERROR", "CURLOPT_UNRESTRICTED_AUTH");
    qputenv("QCURL_TEST_FORCE_SETOPT_ERROR", forcedOptions);
    QCNetworkAccessManager manager;
    auto *reply = manager.get(mixedWarningRequest());
    QSignalSpy errors(reply, qOverload<NetworkError>(&QCNetworkReply::error));
    QSignalSpy finished(reply, &QCNetworkReply::finished);
    QStringList warningsAtError;
    connect(reply, qOverload<NetworkError>(&QCNetworkReply::error), this, [&]() {
        warningsAtError = reply->capabilityWarnings();
        s_messages.append(QStringLiteral("terminal-error"));
    });
    QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 1000);
    QCOMPARE(errors.count(), 1);
    QCOMPARE(reply->state(), ReplyState::Error);
    QCOMPARE(reply->error(), NetworkError::InvalidRequest);
    QVERIFY(reply->errorString().contains(firstFatal));
    QCOMPARE(reply->capabilityWarnings().size(), 2);
    QCOMPARE(warningsAtError, reply->capabilityWarnings());
    QCOMPARE(s_messages.size(), 5);
    QVERIFY(s_messages[0].startsWith(QStringLiteral("QCNetworkReply: Basic authentication")));
    QVERIFY(s_messages[1].contains(QStringLiteral("安全风险")));
    QVERIFY(s_messages[2].contains(QStringLiteral("libcurl 不支持 CURLOPT_UNRESTRICTED_AUTH")));
    QCOMPARE(s_messages[3], QStringLiteral("QCNetworkReply: invalid proxy configuration ignored"));
    QCOMPARE(s_messages[4], QStringLiteral("terminal-error"));
    const QString diagnostics = s_messages.join(QLatin1Char('\n')) + reply->errorString();
    for (const auto &secret : {"user-marker", "credential-marker", "query-marker"}) {
        QVERIFY2(!diagnostics.contains(QString::fromLatin1(secret)), qPrintable(diagnostics));
    }
}

QTEST_MAIN(TestQCNetworkRequestWarningsOffline)
#include "tst_QCNetworkRequestWarningsOffline.moc"
