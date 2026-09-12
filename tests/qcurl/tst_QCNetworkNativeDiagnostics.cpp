#include "tst_QCNetworkNativeDiagnostics.h"

#include "QCBlockingNetworkClient.h"
#include "QCNetworkAccessManager.h"
#include "QCNetworkDownloadToDeviceJob.h"
#include "QCNetworkReply.h"
#include "QCNetworkRequest.h"
#include "QCNetworkTimeoutConfig.h"
#include "qcurl_http_script_server.h"
#include "qcurl_tls_test_server.h"
#include "test_source_paths.h"

#include <QSignalSpy>
#include <QtTest>

#include <curl/curl.h>
#include <future>
#include <memory>
#include <type_traits>
#include <utility>

using namespace QCurl;
using namespace std::chrono_literals;

static_assert(
    std::is_same_v<decltype(std::declval<const QCNetworkReply &>().diagnosticCurlCode()), int>);
static_assert(noexcept(std::declval<const QCNetworkReply &>().diagnosticCurlCode()));

namespace {

class FailingIoDevice final : public QIODevice
{
    Q_OBJECT

public:
    FailingIoDevice() = default;
    bool isSequential() const override { return true; }

protected:
    qint64 readData(char *, qint64) override
    {
        setErrorString(QStringLiteral("测试输入设备读取失败"));
        return -1;
    }
    qint64 writeData(const char *, qint64) override
    {
        setErrorString(QStringLiteral("测试输出设备写入失败"));
        return -1;
    }

private:
    Q_DISABLE_COPY_MOVE(FailingIoDevice)
};

int directCurlCode(const QUrl &url)
{
    std::unique_ptr<CURL, decltype(&curl_easy_cleanup)> handle(curl_easy_init(), curl_easy_cleanup);
    if (!handle) {
        return -1;
    }
    const QByteArray encoded = url.toEncoded();
    if (curl_easy_setopt(handle.get(), CURLOPT_URL, encoded.constData()) != CURLE_OK
        || curl_easy_setopt(handle.get(), CURLOPT_NOPROXY, "*") != CURLE_OK
        || curl_easy_setopt(handle.get(), CURLOPT_TIMEOUT_MS, 3000L) != CURLE_OK
        || curl_easy_setopt(handle.get(), CURLOPT_NOSIGNAL, 1L) != CURLE_OK) {
        return -1;
    }
    return static_cast<int>(curl_easy_perform(handle.get()));
}

} // namespace

void tst_QCNetworkNativeDiagnostics::transferResults_data()
{
    QTest::addColumn<int>("scenario");
    QTest::addColumn<bool>("scheduled");
    QTest::addColumn<int>("status");
    QTest::addColumn<NetworkError>("error");
    QTest::addColumn<int>("code");
    for (const bool scheduled : {false, true}) {
        const QByteArray prefix = scheduled ? "scheduled-" : "direct-";
        QTest::newRow((prefix + "200").constData())
            << 200 << scheduled << 200 << NetworkError::NoError << int(CURLE_OK);
        QTest::newRow((prefix + "404").constData())
            << 404 << scheduled << 404 << NetworkError::HttpNotFound << int(CURLE_OK);
        QTest::newRow((prefix + "refused").constData())
            << 0 << scheduled << 0 << NetworkError::ConnectionRefused << int(CURLE_COULDNT_CONNECT);
        QTest::newRow((prefix + "timeout").constData())
            << 1 << scheduled << 0 << NetworkError::ConnectionTimeout
            << int(CURLE_OPERATION_TIMEDOUT);
    }
}

void tst_QCNetworkNativeDiagnostics::transferResults()
{
    QFETCH(int, scenario);
    QFETCH(bool, scheduled);
    QFETCH(int, status);
    QFETCH(NetworkError, error);
    QFETCH(int, code);
    auto response    = HttpScriptServer::response(scenario == 404 ? 404 : 200, "body");
    response.delayMs = scenario == 1 ? 1000 : 0;
    HttpScriptServer server({response});
    QVERIFY(server.start());
    QTcpServer closedPort;
    QVERIFY(closedPort.listen(QHostAddress::LocalHost, 0));
    const QUrl refused(QStringLiteral("http://127.0.0.1:%1/").arg(closedPort.serverPort()));
    closedPort.close();
    QCNetworkRequest request(scenario == 0 ? refused : server.url());
    QCNetworkTimeoutConfig timeout;
    timeout.setTotalTimeout(scenario == 1 ? 100ms : 3000ms);
    request.setTimeoutConfig(timeout);
    QCNetworkAccessManager manager;
    manager.enableRequestScheduler(scheduled);
    auto *reply = manager.get(request);
    QCOMPARE(reply->diagnosticCurlCode(), 0);
    QSignalSpy finished(reply, &QCNetworkReply::finished);
    int notificationCode = -1;
    connect(reply, &QCNetworkReply::stateChanged, this, [&](ReplyState) {
        if (reply->isFinished()) {
            notificationCode = reply->diagnosticCurlCode();
        }
    });
    QVERIFY(finished.wait(5000));
    QCOMPARE(reply->state(),
             error == NetworkError::NoError ? ReplyState::Finished : ReplyState::Error);
    QCOMPARE(reply->error(), error);
    QCOMPARE(reply->httpStatusCode(), status);
    QCOMPARE(reply->diagnosticCurlCode(), code);
    QCOMPARE(notificationCode, code);
    reply->cancel();
    reply->execute();
    QCOMPARE(reply->diagnosticCurlCode(), code);
    QCOMPARE(finished.size(), 1);
    auto blocking = std::async(std::launch::async,
                               [request]() { return QCBlockingNetworkClient().get(request); });
    QTRY_VERIFY_WITH_TIMEOUT(blocking.wait_for(0ms) == std::future_status::ready, 5000);
    const auto result = blocking.get();
    QCOMPARE(result.error(), error);
    QCOMPARE(result.statusCode(), status);
    QCOMPARE(result.diagnosticCurlCode(), code);
}

void tst_QCNetworkNativeDiagnostics::tlsFailures_data()
{
    QTest::addColumn<bool>("plaintext");
    QTest::addColumn<bool>("scheduled");
    QTest::addColumn<int>("code");
    for (const bool scheduled : {false, true}) {
        const QByteArray prefix = scheduled ? "scheduled-" : "direct-";
        QTest::newRow((prefix + "handshake").constData())
            << true << scheduled << int(CURLE_SSL_CONNECT_ERROR);
        QTest::newRow((prefix + "certificate").constData())
            << false << scheduled << int(CURLE_PEER_FAILED_VERIFICATION);
    }
}

void tst_QCNetworkNativeDiagnostics::tlsFailures()
{
    QFETCH(bool, plaintext);
    QFETCH(bool, scheduled);
    QFETCH(int, code);
    LocalTlsServer server(TestSourcePaths::sourcePath(
                              QStringLiteral("tests/qcurl/testdata/http2/localhost.crt")),
                          TestSourcePaths::sourcePath(
                              QStringLiteral("tests/qcurl/testdata/http2/localhost.key")));
    QVERIFY2(server.start(plaintext), qPrintable(server.errorString()));
    const QUrl url(QStringLiteral("https://localhost:%1/").arg(server.port()));
    QCNetworkRequest request(url);
    QCNetworkTimeoutConfig timeout;
    timeout.setTotalTimeout(3000ms);
    request.setTimeoutConfig(timeout);
    QCNetworkAccessManager manager;
    manager.enableRequestScheduler(scheduled);
    auto *reply = manager.get(request);
    QSignalSpy finished(reply, &QCNetworkReply::finished);
    QVERIFY(finished.wait(5000));
    QCOMPARE(reply->error(), NetworkError::SslHandshakeFailed);
    QCOMPARE(reply->httpStatusCode(), 0);
    QCOMPARE(reply->diagnosticCurlCode(), code);
    auto reference = std::async(std::launch::async, [url]() { return directCurlCode(url); });
    QTRY_VERIFY_WITH_TIMEOUT(reference.wait_for(0ms) == std::future_status::ready, 5000);
    QCOMPARE(reference.get(), code);
    auto blocking = std::async(std::launch::async,
                               [request]() { return QCBlockingNetworkClient().get(request); });
    QTRY_VERIFY_WITH_TIMEOUT(blocking.wait_for(0ms) == std::future_status::ready, 5000);
    const auto result = blocking.get();
    QCOMPARE(result.error(), NetworkError::SslHandshakeFailed);
    QCOMPARE(result.statusCode(), 0);
    QCOMPARE(result.diagnosticCurlCode(), code);
    QCOMPARE(server.connectionCount(), 3);
}

void tst_QCNetworkNativeDiagnostics::deviceReadFailure_data()
{
    QTest::addColumn<bool>("scheduled");
    QTest::newRow("direct") << false;
    QTest::newRow("scheduled") << true;
}

void tst_QCNetworkNativeDiagnostics::deviceReadFailure()
{
    QFETCH(bool, scheduled);
    auto response    = HttpScriptServer::response(200, "ok");
    response.delayMs = 1000;
    HttpScriptServer server({response});
    QVERIFY(server.start());
    QCNetworkRequest request(server.url());
    request.setRawHeader("Expect", "");
    QCNetworkTimeoutConfig timeout;
    timeout.setTotalTimeout(3000ms);
    request.setTimeoutConfig(timeout);
    QCNetworkAccessManager manager;
    manager.enableRequestScheduler(scheduled);
    FailingIoDevice input;
    QVERIFY(input.open(QIODevice::ReadOnly));
    auto *reply = manager.post(request, &input, 16);
    QSignalSpy finished(reply, &QCNetworkReply::finished);
    QVERIFY(finished.wait(5000));
    QCOMPARE(reply->error(), NetworkError::InvalidRequest);
    QCOMPARE(reply->httpStatusCode(), 0);
    QCOMPARE(reply->diagnosticCurlCode(), int(CURLE_ABORTED_BY_CALLBACK));
    auto blocking = std::async(std::launch::async, [request]() {
        FailingIoDevice device;
        if (!device.open(QIODevice::ReadOnly)) {
            return QCBlockingNetworkResult::failure(NetworkError::Unknown, device.errorString());
        }
        return QCBlockingNetworkClient().post(request, &device, 16);
    });
    QTRY_VERIFY_WITH_TIMEOUT(blocking.wait_for(0ms) == std::future_status::ready, 5000);
    const auto result = blocking.get();
    QCOMPARE(result.error(), NetworkError::InputDeviceError);
    QCOMPARE(result.statusCode(), 0);
    QCOMPARE(result.diagnosticCurlCode(), int(CURLE_ABORTED_BY_CALLBACK));
}

void tst_QCNetworkNativeDiagnostics::deviceWriteFailure_data()
{
    deviceReadFailure_data();
}

void tst_QCNetworkNativeDiagnostics::deviceWriteFailure()
{
    QFETCH(bool, scheduled);
    HttpScriptServer server({HttpScriptServer::response(200, "payload")});
    QVERIFY(server.start());
    QCNetworkAccessManager manager;
    manager.enableRequestScheduler(scheduled);
    FailingIoDevice output;
    QVERIFY(output.open(QIODevice::WriteOnly));
    QCNetworkDownloadToDeviceJob job(&manager, QCNetworkRequest(server.url()), &output);
    QSignalSpy finished(&job, &QCNetworkTransferJob::finished);
    job.start();
    QVERIFY(finished.wait(5000));
    QVERIFY(job.reply());
    // 异步设备由 readyRead 消费，写失败走本地中止，尚未取得原生传输返回码。
    QCOMPARE(job.error(), NetworkError::InvalidRequest);
    QCOMPARE(job.reply()->httpStatusCode(), 200);
    QCOMPARE(job.reply()->diagnosticCurlCode(), 0);
    QCOMPARE(finished.size(), 1);
}

QTEST_GUILESS_MAIN(tst_QCNetworkNativeDiagnostics)
#include "tst_QCNetworkNativeDiagnostics.moc"
