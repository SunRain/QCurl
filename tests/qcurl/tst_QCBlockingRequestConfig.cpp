/**
 * @file
 * @brief Blocking Extras 请求配置合同测试。
 */

#include "QCBlockingNetworkClient.h"
#include "QCNetworkAccessManager.h"
#include "QCNetworkError.h"
#include "QCNetworkProxyConfig.h"
#include "QCNetworkReply.h"
#include "QCNetworkRequest.h"
#include "qcblocking_upload_echo_server.h"
#include "qcurl_http_script_server.h"

#include <QCoreApplication>
#include <QtTest/QtTest>

#include <curl/curl.h>
#include <future>

using namespace QCurl;

namespace {

class ScopedEnvVar final
{
public:
    ScopedEnvVar(const char *name, QByteArray value)
        : m_name(name)
        , m_hadValue(qEnvironmentVariableIsSet(name))
        , m_oldValue(qgetenv(name))
    {
        qputenv(m_name, value);
    }

    ~ScopedEnvVar()
    {
        if (m_hadValue) {
            qputenv(m_name, m_oldValue);
        } else {
            qunsetenv(m_name);
        }
    }

private:
    const char *m_name = nullptr;
    bool m_hadValue    = false;
    QByteArray m_oldValue;
};

QCBlockingNetworkClient makeClient()
{
    QCBlockingNetworkClient::Options options;
    options.setApplicationThreadPolicy(
        QCBlockingNetworkClient::ApplicationThreadPolicy::AllowForCliOrTests);
    return QCBlockingNetworkClient(options);
}

QCNetworkRequest makeRequest(const QUrl &url)
{
    QCNetworkRequest request(url);
    request.setRawHeader(QByteArrayLiteral("Expect"), QByteArray());
    return request;
}

QByteArray headerValue(const UploadEchoServer::RequestRecord &request, const QByteArray &name)
{
    for (const auto &header : request.headers) {
        if (header.first.compare(name, Qt::CaseInsensitive) == 0) {
            return header.second;
        }
    }
    return {};
}

} // namespace

class tst_QCBlockingRequestConfig : public QObject
{
    Q_OBJECT

public:
    tst_QCBlockingRequestConfig() = default;

private Q_SLOTS:
    void appliesResolveOverride_data();
    void appliesResolveOverride();
    void appliesTransferRequestHeaders();
    void preservesExplicitHeadersOverRequestConfig();
    void rejectsUnsupportedAllowedProtocolsByDefault();
    void rejectsNonHttpCoreUrls();
    void rejectsHttpRedirectToFtp();
    void rejectsSlistAppendFailure();
    void rejectsHeaderAppendFailure();
    void rejectsRequiredSetoptFailure();
    void commonNativePolicy_data();
    void commonNativePolicy();

private:
    Q_DISABLE_COPY_MOVE(tst_QCBlockingRequestConfig)
};

void tst_QCBlockingRequestConfig::appliesResolveOverride_data()
{
    QTest::addColumn<QString>("host");
    QTest::newRow("ascii") << QStringLiteral("original.invalid");
    QTest::newRow("internal") << QStringLiteral("internal_name");
    QTest::newRow("unicode") << QStringLiteral("bücher.invalid");
    QTest::newRow("unicode-normalization") << QStringLiteral("bu\u0308cher.invalid");
    QTest::newRow("ace") << QStringLiteral("xn--bcher-kva.invalid");
    QTest::newRow("uppercase") << QStringLiteral("BÜCHER.INVALID");
}

void tst_QCBlockingRequestConfig::appliesResolveOverride()
{
    QFETCH(QString, host);
    UploadEchoServer::ResponsePlan plan;
    plan.body = QByteArrayLiteral("resolved");
    UploadEchoServer server(plan);
    QVERIFY(server.start());
    auto url = server.url(QStringLiteral("/resolve"));
    url.setHost(host);
    QCNetworkRequest request = makeRequest(url);
    QCOMPARE(request.setResolveOverride(
                 {QStringLiteral("%1:%2:127.0.0.1").arg(host).arg(url.port())}),
             QCNetworkConfigUpdateResult::Applied);
    const QByteArray aceHost = QUrl::toAce(url.host());
    QCOMPARE(request.resolveOverride().value(),
             QStringList{QString::fromLatin1(aceHost)
                         + QStringLiteral(":%1:127.0.0.1").arg(url.port())});
    const auto result = makeClient().get(request);
    QVERIFY2(result.isSuccess(), qPrintable(result.errorMessage()));
    QCOMPARE(result.body(), QByteArrayLiteral("resolved"));
    QCOMPARE(headerValue(server.lastRequest(), QByteArrayLiteral("Host")),
             aceHost + ':' + QByteArray::number(url.port()));
}

void tst_QCBlockingRequestConfig::appliesTransferRequestHeaders()
{
    UploadEchoServer server;
    QVERIFY(server.start());

    QCNetworkRequest request = makeRequest(server.url(QStringLiteral("/headers")));
    request.setReferer(QStringLiteral("https://origin.example/source"));
    request.setAcceptedEncodings({QStringLiteral("gzip"), QStringLiteral("br")});
    QCOMPARE(request.setMaxDownloadBytesPerSec(1024 * 1024), QCNetworkConfigUpdateResult::Applied);

    const auto result = makeClient().get(request);
    QVERIFY2(result.isSuccess(), qPrintable(result.errorMessage()));

    const auto captured = server.lastRequest();
    QCOMPARE(headerValue(captured, QByteArrayLiteral("Referer")),
             QByteArrayLiteral("https://origin.example/source"));
    QCOMPARE(headerValue(captured, QByteArrayLiteral("Accept-Encoding")),
             QByteArrayLiteral("gzip,br"));
}

void tst_QCBlockingRequestConfig::preservesExplicitHeadersOverRequestConfig()
{
    UploadEchoServer server;
    QVERIFY(server.start());

    QCNetworkRequest request = makeRequest(server.url(QStringLiteral("/explicit")));
    request.setRawHeader(QByteArrayLiteral("Referer"),
                         QByteArrayLiteral("https://header.example/source"));
    request.setRawHeader(QByteArrayLiteral("Accept-Encoding"), QByteArrayLiteral("identity"));
    request.setReferer(QStringLiteral("https://config.example/source"));
    request.setAcceptedEncodings({QStringLiteral("gzip")});

    const auto result = makeClient().get(request);
    QVERIFY2(result.isSuccess(), qPrintable(result.errorMessage()));

    const auto captured = server.lastRequest();
    QCOMPARE(headerValue(captured, QByteArrayLiteral("Referer")),
             QByteArrayLiteral("https://header.example/source"));
    QCOMPARE(headerValue(captured, QByteArrayLiteral("Accept-Encoding")),
             QByteArrayLiteral("identity"));
}

void tst_QCBlockingRequestConfig::rejectsUnsupportedAllowedProtocolsByDefault()
{
    ScopedEnvVar forcedCapabilityError("QCURL_TEST_FORCE_CAPABILITY_ERROR",
                                       QByteArrayLiteral("CURLOPT_PROTOCOLS_STR"));

    QCNetworkRequest request(QUrl(QStringLiteral("http://127.0.0.1:1/blocked")));
    request.setAllowedProtocols(QCurl::QCNetworkProtocol::Http);

    const auto result = makeClient().get(request);
    QVERIFY(!result.isSuccess());
    QCOMPARE(result.error(), NetworkError::UnsupportedCapability);
    QVERIFY2(result.errorMessage().contains(QStringLiteral("CURLOPT_PROTOCOLS_STR")),
             qPrintable(result.errorMessage()));
}

void tst_QCBlockingRequestConfig::rejectsNonHttpCoreUrls()
{
    const QList<QUrl> urls{
        QUrl(QStringLiteral("file:///tmp/qcurl-core-protocol-test")),
        QUrl(QStringLiteral("ftp://127.0.0.1:1/resource")),
        QUrl(QStringLiteral("ftps://127.0.0.1:1/resource")),
    };

    for (const QUrl &url : urls) {
        const auto result = makeClient().get(makeRequest(url));
        QVERIFY2(!result.isSuccess(), qPrintable(url.toString()));
        QCOMPARE(result.error(), NetworkError::InvalidRequest);
        QVERIFY2(result.errorMessage().contains(QStringLiteral("HTTP/HTTPS")),
                 qPrintable(result.errorMessage()));
    }
}

void tst_QCBlockingRequestConfig::rejectsHttpRedirectToFtp()
{
    UploadEchoServer::ResponsePlan plan;
    plan.statusLine   = QByteArrayLiteral("HTTP/1.1 302 Found");
    plan.extraHeaders = {QByteArrayLiteral("Location: ftp://127.0.0.1:1/resource")};
    UploadEchoServer server(std::move(plan));
    QVERIFY(server.start());

    const auto result = makeClient().get(makeRequest(server.url(QStringLiteral("/redirect"))));
    QVERIFY(!result.isSuccess());
    QCOMPARE(result.diagnosticCurlCode(), static_cast<int>(CURLE_UNSUPPORTED_PROTOCOL));
}

void tst_QCBlockingRequestConfig::rejectsSlistAppendFailure()
{
    ScopedEnvVar forcedSlistError("QCURL_TEST_FORCE_SLIST_APPEND_ERROR",
                                  QByteArrayLiteral("CURLOPT_RESOLVE:2"));

    QCNetworkRequest request(QUrl(QStringLiteral("http://127.0.0.1:1/blocked")));
    QCOMPARE(request.setResolveOverride(QStringList{
                 QStringLiteral("example.com:80:127.0.0.1"),
                 QStringLiteral("example.net:80:127.0.0.1"),
             }),
             QCNetworkConfigUpdateResult::Applied);

    const auto result = makeClient().get(request);
    QVERIFY(!result.isSuccess());
    QCOMPARE(result.error(), NetworkError::InvalidRequest);
    QVERIFY2(result.errorMessage().contains(QStringLiteral("CURLOPT_RESOLVE")),
             qPrintable(result.errorMessage()));
}

void tst_QCBlockingRequestConfig::rejectsHeaderAppendFailure()
{
    ScopedEnvVar forcedSlistError("QCURL_TEST_FORCE_SLIST_APPEND_ERROR",
                                  QByteArrayLiteral("CURLOPT_HTTPHEADER"));

    QCNetworkRequest request(QUrl(QStringLiteral("http://127.0.0.1:1/blocked")));
    request.setRawHeader(QByteArrayLiteral("Authorization"), QByteArrayLiteral("redacted"));

    const auto result = makeClient().get(request);
    QVERIFY(!result.isSuccess());
    QCOMPARE(result.error(), NetworkError::InvalidRequest);
    QVERIFY2(result.errorMessage().contains(QStringLiteral("CURLOPT_HTTPHEADER")),
             qPrintable(result.errorMessage()));
}

void tst_QCBlockingRequestConfig::rejectsRequiredSetoptFailure()
{
    ScopedEnvVar forcedSetoptError("QCURL_TEST_FORCE_SETOPT_ERROR",
                                   QByteArrayLiteral("CURLOPT_WRITEFUNCTION"));

    QCNetworkRequest request(QUrl(QStringLiteral("http://127.0.0.1:1/blocked")));
    const auto result = makeClient().get(request);

    QVERIFY(!result.isSuccess());
    QCOMPARE(result.error(), NetworkError::InvalidRequest);
    QVERIFY2(result.errorMessage().contains(QStringLiteral("CURLOPT_WRITEFUNCTION")),
             qPrintable(result.errorMessage()));
}

namespace {
QCNetworkRequest nativePolicyRequest(const QUrl &url, const QByteArray &option, bool effective)
{
    auto request = makeRequest(url);
    if (!effective) {
        request.setFollowLocation(false);
        request.setTimeout(std::chrono::milliseconds(0));
        request.setConnectTimeout(std::chrono::milliseconds(0));
        request.setReferer(QStringLiteral("private-marker"));
        request.setRawHeader("Referer", {});
        request.setAcceptedEncodings({QStringLiteral("gzip")});
        request.setRawHeader("Accept-Encoding", {});
        QCNetworkHttpAuthConfig auth;
        auth.setUserName(QStringLiteral("private-marker"));
        request.setHttpAuth(auth);
        request.setRawHeader("Authorization", {});
        return request;
    }
    request.setReferer(QStringLiteral("private-marker"));
    request.setAutoDecompressionEnabled(true);
    Q_UNUSED(request.setMaxRedirects(3));
    request.setPostRedirectPolicy(QCNetworkPostRedirectPolicy::KeepPostAll);
    request.setAutoRefererEnabled(true);
    request.setTimeout(std::chrono::seconds(3));
    request.setConnectTimeout(std::chrono::seconds(1));
    Q_UNUSED(request.setMaxDownloadBytesPerSec(1024));
    Q_UNUSED(request.setMaxUploadBytesPerSec(1024));
    request.setRange(1, 3);
    request.setRawHeader("Authorization", "private-marker");
    request.setAllowUnrestrictedSensitiveHeadersOnRedirect(true);
    if (option == "CURLOPT_NOPROXY") {
        QCNetworkProxyConfig proxy;
        proxy.setType(QCNetworkProxyConfig::ProxyType::Http);
        proxy.setHostName(QStringLiteral("127.0.0.1"));
        proxy.setPort(quint16(url.port()));
        request.setProxyConfig(proxy);
    }
    if (option == "CURLOPT_CAINFO") {
        auto tls = request.sslConfig();
        tls.setCaCertPath(QStringLiteral("private-marker"));
        request.setSslConfig(tls);
    }
    return request;
}
} // namespace

void tst_QCBlockingRequestConfig::commonNativePolicy_data()
{
    QTest::addColumn<QByteArray>("option");
    QTest::addColumn<bool>("effective");
    QTest::addColumn<bool>("capability");
    const QList<QByteArray> required = {"CURLOPT_PROXY",
                                        "CURLOPT_NOPROXY",
                                        "CURLOPT_PROTOCOLS_STR",
                                        "CURLOPT_REDIR_PROTOCOLS_STR",
                                        "CURLOPT_FOLLOWLOCATION",
                                        "CURLOPT_MAXREDIRS",
                                        "CURLOPT_POSTREDIR",
                                        "CURLOPT_AUTOREFERER",
                                        "CURLOPT_REFERER",
                                        "CURLOPT_ACCEPT_ENCODING",
                                        "CURLOPT_RANGE",
                                        "CURLOPT_MAX_RECV_SPEED_LARGE",
                                        "CURLOPT_MAX_SEND_SPEED_LARGE",
                                        "CURLOPT_UNRESTRICTED_AUTH",
                                        "CURLOPT_SSL_VERIFYPEER",
                                        "CURLOPT_SSL_VERIFYHOST",
                                        "CURLOPT_CAINFO",
                                        "CURLOPT_CONNECTTIMEOUT_MS",
                                        "CURLOPT_TIMEOUT_MS"};
    for (const auto &option : required) {
        for (bool capability : {false, true}) {
            QTest::newRow(option + (capability ? "-capability" : "-argument"))
                << option << true << capability;
        }
    }
    for (const auto &option : {"CURLOPT_HTTP_VERSION",
                               "CURLOPT_CAINFO",
                               "CURLOPT_CONNECTTIMEOUT_MS",
                               "CURLOPT_TIMEOUT_MS",
                               "CURLOPT_MAXREDIRS",
                               "CURLOPT_REDIR_PROTOCOLS_STR",
                               "CURLOPT_REFERER",
                               "CURLOPT_ACCEPT_ENCODING",
                               "CURLOPT_USERNAME",
                               "CURLOPT_HTTPAUTH",
                               "CURLOPT_UNRESTRICTED_AUTH"}) {
        QTest::newRow(QByteArray(option) + "-inactive") << QByteArray(option) << false << false;
    }
}

void tst_QCBlockingRequestConfig::commonNativePolicy()
{
    QFETCH(QByteArray, option);
    QFETCH(bool, effective);
    QFETCH(bool, capability);
    ScopedEnvVar forced(capability ? "QCURL_TEST_FORCE_CAPABILITY_ERROR"
                                   : "QCURL_TEST_FORCE_SETOPT_ERROR",
                        option);
    HttpScriptServer server({HttpScriptServer::response(200, "ok")});
    QVERIFY(server.start());
    const auto request = nativePolicyRequest(server.url(), option, effective);
    auto pending       = std::async(std::launch::async,
                                    [request]() { return QCBlockingNetworkClient().get(request); });
    QTRY_VERIFY(pending.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready);
    const auto result = pending.get();
    QCNetworkAccessManager manager;
    auto *reply = manager.get(request);
    QSignalSpy done(reply, &QCNetworkReply::finished);
    QVERIFY(done.wait());
    QCOMPARE(result.isSuccess(), !effective);
    QCOMPARE(reply->error() == NetworkError::NoError, !effective);
    if (effective) {
        const bool protocol = option == "CURLOPT_PROTOCOLS_STR"
                              || option == "CURLOPT_REDIR_PROTOCOLS_STR";
        QCOMPARE(result.error(),
                 capability && protocol ? NetworkError::UnsupportedCapability
                                        : NetworkError::InvalidRequest);
        QCOMPARE(reply->error(), NetworkError::InvalidRequest);
        QVERIFY2(result.errorMessage().contains(QString::fromLatin1(option)),
                 qPrintable(result.errorMessage()));
        QVERIFY2(reply->errorString().contains(QString::fromLatin1(option)),
                 qPrintable(reply->errorString()));
        QVERIFY(!result.errorMessage().contains(QStringLiteral("private-")));
        QVERIFY(!reply->errorString().contains(QStringLiteral("private-")));
        QCOMPARE(server.connectionCount(), 0);
    } else {
        QCOMPARE(server.requests().size(), 2);
        for (const auto &wire : server.requests()) {
            QVERIFY(!wire.toLower().contains("\r\nreferer:"));
            QVERIFY(!wire.toLower().contains("\r\naccept-encoding:"));
            QVERIFY(!wire.toLower().contains("\r\nauthorization:"));
        }
    }
}

QTEST_MAIN(tst_QCBlockingRequestConfig)
#include "tst_QCBlockingRequestConfig.moc"
