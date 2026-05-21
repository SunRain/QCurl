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

private slots:
    void appliesTransferRequestHeaders();
    void preservesExplicitHeadersOverRequestConfig();
    void rejectsUnsupportedAllowedProtocolsByDefault();
};

void tst_QCBlockingRequestConfig::appliesTransferRequestHeaders()
{
    UploadEchoServer server;
    QVERIFY(server.start());

    QCNetworkRequest request = makeRequest(server.url(QStringLiteral("/headers")));
    request.setReferer(QStringLiteral("https://origin.example/source"));
    request.setAcceptedEncodings({QStringLiteral("gzip"), QStringLiteral("br")});
    request.setMaxDownloadBytesPerSec(1024 * 1024);

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

QTEST_MAIN(tst_QCBlockingRequestConfig)
#include "tst_QCBlockingRequestConfig.moc"
