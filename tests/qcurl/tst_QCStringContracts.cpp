#include "QCBlockingNetworkClient.h"
#include "QCNetworkAccessManager.h"
#include "QCNetworkContentEncoding.h"
#include "QCNetworkHttpHeaders.h"
#include "QCNetworkProxyConfig.h"
#include "QCNetworkReply.h"
#include "QCNetworkRequest.h"
#include "QCNetworkTimeoutConfig.h"
#include "private/QCCurlOptionAdapter_p.h"
#include "private/QCNetworkProtocolPolicy_p.h"
#include "private/QCNetworkRequestPolicy_p.h"
#include "qcurl_http_script_server.h"

#include <QScopeGuard>
#include <QtTest>

#include <curl/curl.h>
#include <future>
#include <limits>
#include <type_traits>

using namespace QCurl;

static_assert(!std::is_invocable_v<decltype(&QCNetworkRequest::setAllowedProtocols),
                                   QCNetworkRequest &,
                                   QStringList>);
static_assert(!std::is_invocable_v<decltype(&QCNetworkTransferConfig::setAllowedRedirectProtocols),
                                   QCNetworkTransferConfig &,
                                   QStringList>);

namespace {

QByteArray headerValue(const QByteArray &wire, const QByteArray &name)
{
    for (const auto &line : wire.left(wire.indexOf("\r\n\r\n")).split('\n')) {
        const auto colon = line.indexOf(':');
        if (colon > 0 && line.left(colon).compare(name, Qt::CaseInsensitive) == 0) {
            return line.mid(colon + 1).trimmed();
        }
    }
    return {};
}

} // namespace

class TestQCStringContracts : public QObject
{
    Q_OBJECT

public:
    TestQCStringContracts() = default;

private Q_SLOTS:
    void namedVocabulary();
    void optionIdentityAndFaultInjection();
    void protocolValues_data();
    void protocolValues();
    void invalidProtocols_data();
    void invalidProtocols();
    void initialProtocolRestriction_data();
    void initialProtocolRestriction();
    void redirectProtocolRestriction_data();
    void redirectProtocolRestriction();
    void encodingStates_data();
    void encodingStates();
    void policyValidation_data();
    void policyValidation();
    void sensitiveRedirectAuthorization_data();
    void sensitiveRedirectAuthorization();

private:
    Q_DISABLE_COPY_MOVE(TestQCStringContracts)
};

void TestQCStringContracts::namedVocabulary()
{
    QCNetworkRequest request;
    request.setRawHeader(httpheaders::kContentType, "application/json");
    request.setRawHeader("CONTENT-TYPE", "text/plain");
    request.setRawHeader("X-Extension", "kept");
    QCOMPARE(request.rawHeaderList().size(), 2);
    QCOMPARE(request.rawHeader("content-type"), QByteArray("text/plain"));
    QCOMPARE(request.rawHeader("x-extension"), QByteArray("kept"));
    QCOMPARE(httpheaders::kContentType, QByteArray("Content-Type"));
    QCOMPARE(httpheaders::kAcceptEncoding, QByteArray("Accept-Encoding"));
    request.setAcceptedEncodings({contentencoding::kGzip,
                                  contentencoding::kDeflate,
                                  contentencoding::kBrotli,
                                  contentencoding::kZstd,
                                  contentencoding::kIdentity,
                                  QStringLiteral("x-custom")});
    QCOMPARE(request.acceptedEncodings(),
             QStringList({QStringLiteral("gzip"),
                          QStringLiteral("deflate"),
                          QStringLiteral("br"),
                          QStringLiteral("zstd"),
                          QStringLiteral("identity"),
                          QStringLiteral("x-custom")}));
    QVERIFY(request.autoDecompressionEnabled());
    request.setAcceptedEncodings({});
    QVERIFY(request.acceptedEncodings().isEmpty());
    QVERIFY(!request.autoDecompressionEnabled());
    request.setAutoDecompressionEnabled(true);
    QVERIFY(request.autoDecompressionEnabled());
    QVERIFY(request.acceptedEncodings().isEmpty());
}

void TestQCStringContracts::optionIdentityAndFaultInjection()
{
    constexpr auto kUrlOption     = QCURL_CURL_OPTION(CURLOPT_URL);
    constexpr auto kEncodingAlias = QCURL_CURL_OPTION(CURLOPT_ENCODING);
    static_assert(kUrlOption.id == CURLOPT_URL);
    static_assert(kEncodingAlias.id == CURLOPT_ACCEPT_ENCODING);
    QCOMPARE(QByteArray(kUrlOption.name), QByteArray("CURLOPT_URL"));
    QCOMPARE(QByteArray(kEncodingAlias.name), QByteArray("CURLOPT_ENCODING"));
    CURL *handle = curl_easy_init();
    QVERIFY(handle);
    const auto cleanup = qScopeGuard([handle]() { curl_easy_cleanup(handle); });
    QCOMPARE(Internal::CurlOptions::setWithTestHook(handle, kUrlOption, "http://127.0.0.1/test"),
             CURLE_OK);
    char *actual = nullptr;
    QCOMPARE(curl_easy_getinfo(handle, CURLINFO_EFFECTIVE_URL, &actual), CURLE_OK);
    QCOMPARE(QByteArray(actual), QByteArray("http://127.0.0.1/test"));
    const bool hadValue = qEnvironmentVariableIsSet("QCURL_TEST_FORCE_SETOPT_ERROR");
    const auto previous = qgetenv("QCURL_TEST_FORCE_SETOPT_ERROR");
    const auto restore  = qScopeGuard([hadValue, previous]() {
        if (hadValue) {
            qputenv("QCURL_TEST_FORCE_SETOPT_ERROR", previous);
        } else {
            qunsetenv("QCURL_TEST_FORCE_SETOPT_ERROR");
        }
    });
    qputenv("QCURL_TEST_FORCE_SETOPT_ERROR", "CURLOPT_URL");
    QCOMPARE(Internal::CurlOptions::setWithTestHook(handle, kUrlOption, "http://invalid.test/"),
             CURLE_BAD_FUNCTION_ARGUMENT);
    QCOMPARE(Internal::CurlOptions::setWithTestHook(handle, QCURL_CURL_OPTION(CURLOPT_NOBODY), 1L),
             CURLE_OK);
    qputenv("QCURL_TEST_FORCE_SETOPT_ERROR", "CURLOPT_URl");
    QCOMPARE(Internal::CurlOptions::setWithTestHook(handle, kUrlOption, "http://127.0.0.1/control"),
             CURLE_OK);
}

void TestQCStringContracts::protocolValues_data()
{
    QTest::addColumn<QCNetworkProtocols>("protocols");
    QTest::addColumn<QStringList>("expected");
    QTest::newRow("empty") << QCNetworkProtocols{}
                           << QStringList{QStringLiteral("http"), QStringLiteral("https")};
    QTest::newRow("http") << QCNetworkProtocols(QCNetworkProtocol::Http)
                          << QStringList{QStringLiteral("http")};
    QTest::newRow("https") << QCNetworkProtocols(QCNetworkProtocol::Https)
                           << QStringList{QStringLiteral("https")};
    QTest::newRow("both") << (QCNetworkProtocol::Http | QCNetworkProtocol::Https)
                          << QStringList{QStringLiteral("http"), QStringLiteral("https")};
}

void TestQCStringContracts::protocolValues()
{
    QFETCH(QCNetworkProtocols, protocols);
    QFETCH(QStringList, expected);
    QCNetworkTransferConfig config;
    QVERIFY(!config.allowedProtocols());
    QVERIFY(!config.allowedRedirectProtocols());
    config.setAllowedProtocols(protocols);
    config.setAllowedRedirectProtocols(protocols);
    QCNetworkRequest request;
    request.setTransferConfig(config);
    QCOMPARE(request.allowedProtocols().has_value(), bool(protocols));
    QCOMPARE(request.allowedRedirectProtocols().has_value(), bool(protocols));
    QStringList effective;
    QString error;
    QVERIFY(Internal::QCNetworkProtocolPolicy::resolveInitialProtocols(request.allowedProtocols(),
                                                                       &effective,
                                                                       &error));
    QCOMPARE(effective, expected);
    QVERIFY(Internal::QCNetworkProtocolPolicy::resolveRedirectProtocols(
        request.allowedRedirectProtocols(), &effective, &error));
    QCOMPARE(effective, expected);
    request.setAllowedProtocols({});
    request.setAllowedRedirectProtocols({});
    QVERIFY(!request.allowedProtocols());
    QVERIFY(!request.allowedRedirectProtocols());
}

void TestQCStringContracts::invalidProtocols_data()
{
    QTest::addColumn<int>("bits");
    QTest::addColumn<bool>("redirect");
    for (int bits : {4, 7, -1}) {
        QTest::newRow(qPrintable(QStringLiteral("initial-%1").arg(bits))) << bits << false;
        QTest::newRow(qPrintable(QStringLiteral("redirect-%1").arg(bits))) << bits << true;
    }
}

void TestQCStringContracts::invalidProtocols()
{
    QFETCH(int, bits);
    QFETCH(bool, redirect);
    HttpScriptServer server({HttpScriptServer::response(200, "control")});
    QVERIFY(server.start());
    QCNetworkRequest request(server.url());
    const auto protocols = QCNetworkProtocols::fromInt(bits);
    if (redirect) {
        request.setAllowedRedirectProtocols(protocols);
    } else {
        request.setAllowedProtocols(protocols);
    }
    QCNetworkAccessManager manager;
    auto *reply = manager.get(request);
    QTRY_VERIFY(reply->isFinished());
    QCOMPARE(reply->error(), NetworkError::InvalidRequest);
    QVERIFY(reply->errorString().contains(QStringLiteral("HTTP/HTTPS")));
    auto blocking = std::async(std::launch::async,
                               [request]() { return QCBlockingNetworkClient().get(request); });
    QTRY_VERIFY(blocking.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready);
    const auto result = blocking.get();
    QCOMPARE(result.error(), NetworkError::InvalidRequest);
    QVERIFY(result.errorMessage().contains(QStringLiteral("HTTP/HTTPS")));
    QCOMPARE(server.connectionCount(), 0);
    auto *control = manager.get(QCNetworkRequest(server.url()));
    QTRY_VERIFY(control->isFinished());
    QCOMPARE(control->error(), NetworkError::NoError);
    QCOMPARE(server.requests().size(), 1);
}

void TestQCStringContracts::initialProtocolRestriction_data()
{
    QTest::addColumn<bool>("allowHttp");
    QTest::newRow("http") << true;
    QTest::newRow("https-only") << false;
}

void TestQCStringContracts::initialProtocolRestriction()
{
    QFETCH(bool, allowHttp);
    HttpScriptServer server({HttpScriptServer::response(200, "ok")});
    QVERIFY(server.start());
    QCNetworkRequest request(server.url());
    request.setAllowedProtocols(allowHttp ? QCNetworkProtocol::Http : QCNetworkProtocol::Https);
    QCNetworkAccessManager manager;
    auto *reply = manager.get(request);
    QTRY_VERIFY(reply->isFinished());
    QCOMPARE(reply->error() == NetworkError::NoError, allowHttp);
    auto blocking = std::async(std::launch::async,
                               [request]() { return QCBlockingNetworkClient().get(request); });
    QTRY_VERIFY(blocking.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready);
    const auto result = blocking.get();
    QCOMPARE(result.isSuccess(), allowHttp);
    QCOMPARE(server.connectionCount(), allowHttp ? 2 : 0);
    if (!allowHttp) {
        QCOMPARE(reply->diagnosticCurlCode(), int(CURLE_UNSUPPORTED_PROTOCOL));
        QCOMPARE(result.diagnosticCurlCode(), int(CURLE_UNSUPPORTED_PROTOCOL));
    }
}

void TestQCStringContracts::redirectProtocolRestriction_data()
{
    initialProtocolRestriction_data();
}

void TestQCStringContracts::redirectProtocolRestriction()
{
    QFETCH(bool, allowHttp);
    HttpScriptServer target({HttpScriptServer::response(200, "ok")});
    QVERIFY(target.start());
    HttpScriptServer redirector(
        {HttpScriptServer::response(302, "", "Location: " + target.url().toEncoded() + "\r\n")});
    QVERIFY(redirector.start());
    QCNetworkRequest request(redirector.url());
    request.setAllowedRedirectProtocols(allowHttp ? QCNetworkProtocol::Http
                                                  : QCNetworkProtocol::Https);
    QCNetworkAccessManager manager;
    auto *reply = manager.get(request);
    QTRY_VERIFY(reply->isFinished());
    QCOMPARE(reply->error() == NetworkError::NoError, allowHttp);
    auto blocking = std::async(std::launch::async,
                               [request]() { return QCBlockingNetworkClient().get(request); });
    QTRY_VERIFY(blocking.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready);
    const auto result = blocking.get();
    QCOMPARE(result.isSuccess(), allowHttp);
    QCOMPARE(redirector.connectionCount(), 2);
    QCOMPARE(target.connectionCount(), allowHttp ? 2 : 0);
    if (!allowHttp) {
        QCOMPARE(reply->diagnosticCurlCode(), int(CURLE_UNSUPPORTED_PROTOCOL));
        QCOMPARE(result.diagnosticCurlCode(), int(CURLE_UNSUPPORTED_PROTOCOL));
    }
}

void TestQCStringContracts::encodingStates_data()
{
    QTest::addColumn<int>("state");
    QTest::addColumn<bool>("decoded");
    QTest::addColumn<QByteArray>("expectedHeader");
    QTest::newRow("default") << 0 << false << QByteArray();
    QTest::newRow("automatic-all") << 1 << true << QByteArray("*");
    QTest::newRow("named-gzip") << 2 << true << QByteArray("gzip");
    QTest::newRow("cleared") << 3 << false << QByteArray();
    QTest::newRow("raw-only") << 4 << false << QByteArray("identity");
    QTest::newRow("raw-wins") << 5 << false << QByteArray("identity");
}

void TestQCStringContracts::encodingStates()
{
    QFETCH(int, state);
    QFETCH(bool, decoded);
    QFETCH(QByteArray, expectedHeader);
    const auto compressed = QByteArray::fromHex(
        "1f8b0800000000000203cb48cdc9c9070086a6103605000000");
    HttpScriptServer server(
        {HttpScriptServer::response(200, compressed, "Content-Encoding: gzip\r\n")});
    QVERIFY(server.start());
    QCNetworkRequest request(server.url());
    if (state == 1) {
        request.setAutoDecompressionEnabled(true);
    } else if (state == 2 || state == 3 || state == 5) {
        request.setAcceptedEncodings({contentencoding::kGzip});
    }
    if (state == 3) {
        request.setAcceptedEncodings({});
    }
    if (state == 4 || state == 5) {
        request.setRawHeader(httpheaders::kAcceptEncoding, "identity");
    }
    QCNetworkAccessManager manager;
    auto *reply = manager.get(request);
    QTRY_VERIFY(reply->isFinished());
    QCOMPARE(reply->error(), NetworkError::NoError);
    QCOMPARE(reply->readAll().value(), decoded ? QByteArray("hello") : compressed);
    auto blocking = std::async(std::launch::async,
                               [request]() { return QCBlockingNetworkClient().get(request); });
    QTRY_VERIFY(blocking.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready);
    const auto result = blocking.get();
    QVERIFY2(result.isSuccess(), qPrintable(result.errorMessage()));
    QCOMPARE(result.body(), decoded ? QByteArray("hello") : compressed);
    QCOMPARE(server.requests().size(), 2);
    for (const auto &wire : server.requests()) {
        const auto encoding = headerValue(wire, "Accept-Encoding");
        if (state == 1) {
            QVERIFY(encoding.contains("gzip"));
        } else {
            QCOMPARE(encoding, expectedHeader);
        }
    }
}

void TestQCStringContracts::policyValidation_data()
{
    QTest::addColumn<int>("scenario");
    QTest::addColumn<QString>("failure");
    QTest::newRow("url-first") << 0 << QStringLiteral("HTTP/HTTPS");
    QTest::newRow("protocol-first") << 1 << QStringLiteral("HTTP/HTTPS");
    QTest::newRow("proxy-first") << 2 << QStringLiteral("代理");
    QTest::newRow("connect-first") << 3 << QStringLiteral("connect timeout");
    QTest::newRow("total") << 4 << QStringLiteral("total timeout");
    QTest::newRow("inactive-redirect-and-none-proxy") << 5 << QString();
    QTest::newRow("zero-timeouts") << 6 << QString();
    QTest::newRow("native-boundaries") << 7 << QString();
    if constexpr (std::numeric_limits<long>::digits
                  < std::numeric_limits<std::chrono::milliseconds::rep>::digits) {
        QTest::newRow("connect-overflow") << 8 << QStringLiteral("connect timeout");
        QTest::newRow("total-overflow") << 9 << QStringLiteral("total timeout");
    }
}

void TestQCStringContracts::policyValidation()
{
    QFETCH(int, scenario);
    QFETCH(QString, failure);
    QCNetworkRequest request(QUrl(QStringLiteral("https://example.test/?private-query")));
    QCNetworkProxyConfig proxy;
    proxy.setType(QCNetworkProxyConfig::ProxyType::Http);
    proxy.setUserName(QStringLiteral("private-user"));
    if (scenario <= 2) {
        request.setProxyConfig(proxy);
    }
    if (scenario == 0) {
        request.setUrl(QUrl(QStringLiteral("file:///private-path")));
    }
    if (scenario == 1 || scenario == 5) {
        request.setAllowedRedirectProtocols(QCNetworkProtocols::fromInt(4));
    }
    if (scenario <= 3) {
        request.setConnectTimeout(std::chrono::milliseconds(-1));
    }
    if (scenario <= 4) {
        request.setTimeout(std::chrono::milliseconds(-1));
    }
    if (scenario == 5) {
        proxy.setType(QCNetworkProxyConfig::ProxyType::None);
        request.setProxyConfig(proxy);
        request.setFollowLocation(false);
    }
    if (scenario == 6) {
        request.setConnectTimeout(std::chrono::milliseconds(0));
        request.setTimeout(std::chrono::milliseconds(0));
    }
    if (scenario == 7) {
        request.setConnectTimeout(std::chrono::milliseconds(std::numeric_limits<long>::max()));
        request.setTimeout(std::chrono::milliseconds(std::numeric_limits<long>::max()));
        QCOMPARE(request.setMaxDownloadBytesPerSec(std::numeric_limits<qint64>::max()),
                 QCNetworkConfigUpdateResult::Applied);
    }
    if (scenario >= 8) {
        const auto value = std::chrono::milliseconds::max();
        if (scenario == 8) {
            request.setConnectTimeout(value);
        } else {
            request.setTimeout(value);
        }
    }
    const auto result = Internal::compileRequestPolicy(request);
    QCOMPARE(result.policy.has_value(), failure.isEmpty());
    QVERIFY2(result.failure.contains(failure), qPrintable(result.failure));
    QVERIFY(!result.failure.contains(QStringLiteral("private-")));
    if (scenario == 6) {
        QVERIFY(!result.policy->timeout.connectMilliseconds);
        QVERIFY(!result.policy->timeout.totalMilliseconds);
    }
    if (scenario == 7) {
        QCOMPARE(result.policy->timeout.totalMilliseconds.value(), std::numeric_limits<long>::max());
        QCOMPARE(result.policy->transfer.downloadBytesPerSecond.value(),
                 static_cast<curl_off_t>(std::numeric_limits<qint64>::max()));
    }
}

void TestQCStringContracts::sensitiveRedirectAuthorization_data()
{
    QTest::addColumn<QByteArray>("header");
    QTest::addColumn<bool>("follow");
    QTest::addColumn<bool>("requestConsent");
    QTest::addColumn<bool>("authConsent");
    QTest::addColumn<bool>("unrestricted");
    QTest::newRow("no-sensitive") << QByteArray() << true << true << false << false;
    QTest::newRow("effective-auth") << QByteArray() << true << false << true << true;
    QTest::newRow("raw-auth-overrides-consent")
        << QByteArray("Authorization") << true << false << true << false;
    for (const auto &name : {"Authorization", "Proxy-Authorization", "Cookie", "Set-Cookie"}) {
        QTest::newRow(name) << QByteArray(name) << true << true << false << true;
    }
    QTest::newRow("redirect-disabled") << QByteArray("Cookie") << false << true << false << false;
    QTest::newRow("consent-missing") << QByteArray("Cookie") << true << false << false << false;
}

void TestQCStringContracts::sensitiveRedirectAuthorization()
{
    QFETCH(QByteArray, header);
    QFETCH(bool, follow);
    QFETCH(bool, requestConsent);
    QFETCH(bool, authConsent);
    QFETCH(bool, unrestricted);
    QCNetworkRequest request(QUrl(QStringLiteral("https://example.test/")));
    request.setFollowLocation(follow);
    request.setAllowUnrestrictedSensitiveHeadersOnRedirect(requestConsent);
    if (!header.isEmpty()) {
        request.setRawHeader(header, {});
    }
    if (authConsent) {
        QCNetworkHttpAuthConfig auth;
        auth.setUserName(QStringLiteral("user"));
        auth.setAllowUnrestrictedAuth(true);
        request.setHttpAuth(auth);
    }
    const auto result = Internal::compileRequestPolicy(request);
    QVERIFY(result.policy);
    QCOMPARE(result.policy->headers.unrestrictedAuth, unrestricted);
    QCOMPARE(result.policy->headers.credentials.has_value(), authConsent && header.isEmpty());
}

QTEST_GUILESS_MAIN(TestQCStringContracts)
#include "tst_QCStringContracts.moc"
