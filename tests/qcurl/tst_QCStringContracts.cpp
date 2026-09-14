#include "QCBlockingNetworkClient.h"
#include "QCNetworkAccessManager.h"
#include "QCNetworkContentEncoding.h"
#include "QCNetworkHttpHeaders.h"
#include "QCNetworkReply.h"
#include "QCNetworkRequest.h"
#include "private/QCNetworkProtocolPolicy_p.h"
#include "qcurl_http_script_server.h"

#include <QtTest>

#include <curl/curl.h>
#include <future>
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

QTEST_GUILESS_MAIN(TestQCStringContracts)
#include "tst_QCStringContracts.moc"
