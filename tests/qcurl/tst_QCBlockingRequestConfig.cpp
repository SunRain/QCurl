/**
 * @file
 * @brief Blocking Extras 请求配置合同测试。
 */

#include "QCBlockingNetworkClient.h"
#include "QCNetworkError.h"
#include "QCNetworkRequest.h"
#include "qcblocking_upload_echo_server.h"

#include <QCoreApplication>
#include <QtTest/QtTest>

#include <curl/curl.h>

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
    bool m_hadValue = false;
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

private Q_SLOTS:
    void appliesTransferRequestHeaders();
    void preservesExplicitHeadersOverRequestConfig();
    void rejectsUnsupportedAllowedProtocolsByDefault();
    void rejectsNonHttpCoreUrls();
    void rejectsHttpRedirectToFtp();
    void rejectsSlistAppendFailure();
    void rejectsHeaderAppendFailure();
    void rejectsRequiredSetoptFailure();
};

void tst_QCBlockingRequestConfig::appliesTransferRequestHeaders()
{
    UploadEchoServer server;
    QVERIFY(server.start());

    QCNetworkRequest request = makeRequest(server.url(QStringLiteral("/headers")));
    request.setReferer(QStringLiteral("https://origin.example/source"));
    request.setAcceptedEncodings({QStringLiteral("gzip"), QStringLiteral("br")});
    QCOMPARE(request.setMaxDownloadBytesPerSec(1024 * 1024),
             QCNetworkConfigUpdateResult::Applied);

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
    request.setAllowedProtocols({QStringLiteral("http")});

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
    request.setResolveOverride(QStringList{
        QStringLiteral("example.com:80:127.0.0.1"),
        QStringLiteral("example.net:80:127.0.0.1"),
    });

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

QTEST_MAIN(tst_QCBlockingRequestConfig)
#include "tst_QCBlockingRequestConfig.moc"
