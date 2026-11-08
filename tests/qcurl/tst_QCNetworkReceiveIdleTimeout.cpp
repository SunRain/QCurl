#include "QCBlockingNetworkClient.h"
#include "QCNetworkAccessManager.h"
#include "QCNetworkMockHandler.h"
#include "QCNetworkReply.h"
#include "QCNetworkRequest.h"
#include "QCNetworkRetryPolicy.h"
#include "QCNetworkSchedulerPolicy.h"
#include "QCNetworkSslConfig.h"
#include "QCNetworkTimeoutConfig.h"
#include "qcnetwork_mock_test_support.h"
#include "qcnetwork_receive_idle_server.h"
#include "qcnetwork_receive_idle_test.h"

#include <QElapsedTimer>
#include <QSignalSpy>
#include <QtTest>

#include <curl/curl.h>
#include <limits>

using namespace QCurl;
using namespace std::chrono_literals;

static QCNetworkRequest idleRequest(const QUrl &url, std::chrono::milliseconds timeout = 400ms)
{
    QCNetworkRequest request(url);
    QCNetworkTimeoutConfig config;
    config.setConnectTimeout(timeout);
    config.setReceiveIdleTimeout(timeout);
    request.setTimeoutConfig(config);
    auto ssl = request.sslConfig();
    ssl.setVerifyPeer(false);
    ssl.setVerifyHost(false);
    request.setSslConfig(ssl);
    return request;
}

void TestQCNetworkReceiveIdleTimeout::configuration()
{
    QCNetworkTimeoutConfig config;
    QVERIFY(!config.receiveIdleTimeout());
    config.setConnectTimeout(100ms);
    config.setTotalTimeout(2s);
    config.setLowSpeedTime(1s);
    config.setLowSpeedLimit(8);
    config.setReceiveIdleTimeout(750ms);
    auto copy = config;
    copy.setReceiveIdleTimeout(900ms);
    QCOMPARE(config.receiveIdleTimeout(), std::optional(750ms));
    QCOMPARE(copy.receiveIdleTimeout(), std::optional(900ms));
    QCNetworkRequest request(QUrl(QStringLiteral("http://127.0.0.1/")));
    request.setTimeoutConfig(config);
    auto requestCopy = request;
    requestCopy.setTimeoutConfig(copy);
    QCOMPARE(request.timeoutConfig().receiveIdleTimeout(), std::optional(750ms));
    copy.setReceiveIdleTimeout(std::nullopt);
    QVERIFY(!copy.receiveIdleTimeout());
    QCOMPARE(copy.connectTimeout(), std::optional(100ms));
    QCOMPARE(copy.totalTimeout(), std::optional(2000ms));
    QCOMPARE(copy.lowSpeedTime(), std::optional(1s));
    QCOMPARE(copy.lowSpeedLimit(), std::optional<long>(8));
    QCBlockingNetworkClient::Options options;
    options.setApplicationThreadPolicy(
        QCBlockingNetworkClient::ApplicationThreadPolicy::AllowForCliOrTests);
    QCBlockingNetworkClient client(options);
    QCOMPARE(client.get(request).error(), NetworkError::UnsupportedCapability);
}

void TestQCNetworkReceiveIdleTimeout::invalidConfiguration_data()
{
    QTest::addColumn<qint64>("milliseconds");
    QTest::newRow("negative") << qint64(-1);
    QTest::newRow("overflow") << qint64(std::numeric_limits<int>::max()) + 1;
    QTest::newRow("max-duration") << std::numeric_limits<qint64>::max();
}

void TestQCNetworkReceiveIdleTimeout::invalidConfiguration()
{
    QFETCH(qint64, milliseconds);
    IdleServer server;
    QVERIFY(server.started());
    QCNetworkAccessManager manager;
    auto request = idleRequest(server.url());
    auto config  = request.timeoutConfig();
    config.setReceiveIdleTimeout(std::chrono::milliseconds(milliseconds));
    request.setTimeoutConfig(config);
    auto *reply = manager.get(request);
    QSignalSpy done(reply, &QCNetworkReply::finished);
    QVERIFY(done.wait(2000));
    QCOMPARE(reply->error(), NetworkError::InvalidRequest);
    QCOMPARE(server.connections(), 0);
    QCOMPARE(done.size(), 1);
}

void TestQCNetworkReceiveIdleTimeout::phases_data()
{
    QTest::addColumn<int>("handshake");
    QTest::addColumn<int>("headers");
    QTest::addColumn<int>("bodyGap");
    QTest::addColumn<bool>("success");
    QTest::newRow("independent-tls-first-byte-budgets") << 850 << 850 << 0 << true;
    QTest::newRow("connect-stall") << 1900 << 0 << 0 << false;
    QTest::newRow("first-byte-stall") << 0 << 1900 << 0 << false;
    QTest::newRow("body-stall-after-200") << 0 << 0 << 1900 << false;
}

void TestQCNetworkReceiveIdleTimeout::phases()
{
    QFETCH(int, handshake);
    QFETCH(int, headers);
    QFETCH(int, bodyGap);
    QFETCH(bool, success);
    IdleResponse response;
    response.handshakeDelayMs = handshake;
    response.headerDelayMs    = headers;
    response.chunkDelayMs     = bodyGap;
    response.chunks           = {"a", "b"};
    IdleServer server({response}, true);
    QVERIFY(server.started());
    QCNetworkAccessManager manager;
    auto *reply = manager.get(idleRequest(server.url(), 1300ms));
    QSignalSpy done(reply, &QCNetworkReply::finished);
    QElapsedTimer elapsed;
    elapsed.start();
    QVERIFY(done.wait(7000));
    QCOMPARE(reply->error(), success ? NetworkError::NoError : NetworkError::ConnectionTimeout);
    QCOMPARE(done.size(), 1);
    QCOMPARE(reply->isReceiveIdleTimeout(), !success && handshake == 0);
    if (success) {
        QCOMPARE(reply->readAll().value(), QByteArrayLiteral("ab"));
        QVERIFY(elapsed.elapsed() > 1300);
    } else if (bodyGap) {
        QCOMPARE(reply->httpStatusCode(), 200);
        QCOMPARE(reply->readAll().value(), QByteArrayLiteral("a"));
    } else if (handshake) {
        QCOMPARE(server.requests(), 0);
    }
}

void TestQCNetworkReceiveIdleTimeout::slowStreamAndExplicitTotal()
{
    IdleResponse response;
    response.chunkDelayMs = 150;
    response.chunks       = {"a", "b", "c", "d", "e", "f", "g", "h", "i"};
    IdleServer server({response});
    QVERIFY(server.started());
    QCNetworkAccessManager manager;
    auto request = idleRequest(server.url());
    auto *reply  = manager.get(request);
    QSignalSpy done(reply, &QCNetworkReply::finished);
    QVERIFY(done.wait(4000));
    QCOMPARE(reply->error(), NetworkError::NoError);
    QCOMPARE(reply->readAll().value(), QByteArrayLiteral("abcdefghi"));
    for (const bool lowSpeed : {false, true}) {
        QCNetworkTimeoutConfig timeout;
        timeout.setReceiveIdleTimeout(400ms);
        if (lowSpeed) {
            timeout.setLowSpeedTime(1s);
            timeout.setLowSpeedLimit(1000);
        } else {
            timeout.setTotalTimeout(400ms);
        }
        request.setTimeoutConfig(timeout);
        auto *limited = manager.get(request);
        QSignalSpy limitedDone(limited, &QCNetworkReply::finished);
        QVERIFY(limitedDone.wait(3000));
        QCOMPARE(limited->error(), NetworkError::ConnectionTimeout);
        QCOMPARE(limited->diagnosticCurlCode(), int(CURLE_OPERATION_TIMEDOUT));
        QVERIFY(!limited->isReceiveIdleTimeout());
        QVERIFY(limited->readAll()->size() < 9);
    }
}

void TestQCNetworkReceiveIdleTimeout::redirectAndReuse()
{
    IdleResponse delayed;
    delayed.handshakeDelayMs = 850;
    delayed.headerDelayMs    = 850;
    delayed.keepAlive        = true;
    IdleServer destination({delayed}, true);
    QVERIFY(destination.started());
    IdleResponse redirect;
    redirect.status       = 302;
    redirect.extraHeaders = "Location: " + destination.url().toEncoded() + "\r\n";
    IdleServer source({redirect});
    QVERIFY(source.started());
    QCNetworkAccessManager manager;
    auto *reply = manager.get(idleRequest(source.url(), 1300ms));
    QSignalSpy done(reply, &QCNetworkReply::finished);
    QVERIFY(done.wait(6000));
    QCOMPARE(reply->error(), NetworkError::NoError);
    QCOMPARE(reply->readAll().value(), QByteArrayLiteral("ok"));
    QCOMPARE(source.requests(), 1);
    QCOMPARE(destination.requests(), 1);
    QTest::qWait(1400);
    auto *reused = manager.get(idleRequest(destination.url(), 1300ms));
    QSignalSpy reusedDone(reused, &QCNetworkReply::finished);
    QVERIFY(reusedDone.wait(4000));
    QCOMPARE(reused->error(), NetworkError::NoError);
    QCOMPARE(destination.connections(), 1);
    QCOMPARE(destination.requests(), 2);
}

void TestQCNetworkReceiveIdleTimeout::retryAfterAndTimeoutRetry()
{
    for (bool stall : {false, true}) {
        IdleResponse first;
        first.status        = stall ? 200 : 429;
        first.headerDelayMs = stall ? 900 : 0;
        first.extraHeaders  = "Retry-After: 1\r\n";
        IdleServer server({first, IdleResponse{}});
        QVERIFY(server.started());
        QCNetworkAccessManager manager;
        auto request = idleRequest(server.url());
        QCNetworkRetryPolicy policy;
        QCOMPARE(policy.setMaxRetries(1), QCNetworkRetryPolicy::UpdateResult::Applied);
        QCOMPARE(policy.setInitialDelay(1200ms), QCNetworkRetryPolicy::UpdateResult::Applied);
        request.setRetryPolicy(policy);
        auto *reply = manager.get(request);
        QSignalSpy done(reply, &QCNetworkReply::finished);
        QVERIFY(done.wait(5000));
        QCOMPARE(reply->error(), NetworkError::NoError);
        QCOMPARE(reply->retryAttemptCount(), 1);
        QVERIFY(!reply->isReceiveIdleTimeout());
        QCOMPARE(reply->diagnosticCurlCode(), 0);
        QCOMPARE(reply->readAll().value(), QByteArrayLiteral("ok"));
        QCOMPARE(server.requests(), 2);
        QVERIFY(reply->cumulativeRetryDelay() >= (stall ? 600ms : 1000ms));
    }
}

void TestQCNetworkReceiveIdleTimeout::cancelAndDestroy()
{
    IdleResponse slow;
    slow.headerDelayMs = 2000;
    IdleServer server({slow});
    QVERIFY(server.started());
    QCNetworkAccessManager manager;
    auto *reply = manager.get(idleRequest(server.url()));
    QSignalSpy done(reply, &QCNetworkReply::finished);
    QTRY_COMPARE_WITH_TIMEOUT(server.requests(), 1, 2000);
    reply->cancel();
    QCOMPARE(reply->error(), NetworkError::OperationCancelled);
    QCOMPARE(done.size(), 1);
    QVERIFY(!reply->isReceiveIdleTimeout());
    QCOMPARE(reply->diagnosticCurlCode(), 0);
    QPointer<QCNetworkReply> doomed = manager.get(idleRequest(server.url()));
    QTRY_COMPARE_WITH_TIMEOUT(server.requests(), 2, 2000);
    doomed->deleteLater();
    QTRY_VERIFY(!doomed);
    QTest::qWait(700);
    QCOMPARE(done.size(), 1);
    QCOMPARE(server.requests(), 2);
}

void TestQCNetworkReceiveIdleTimeout::pauseAndBackpressure_data()
{
    QTest::addColumn<bool>("backpressure");
    QTest::newRow("explicit-recv-pause") << false;
    QTest::newRow("backpressure") << true;
}

void TestQCNetworkReceiveIdleTimeout::pauseAndBackpressure()
{
    QFETCH(bool, backpressure);
    IdleResponse stream;
    stream.chunks       = {"a", "b", "c", "d"};
    stream.chunkDelayMs = 180;
    if (backpressure) {
        stream.chunks[0] = QByteArray(16384, 'a');
    }
    const QByteArray expected = stream.chunks[0] + "bcd";
    IdleServer server({stream});
    QVERIFY(server.started());
    QCNetworkAccessManager manager;
    auto request = idleRequest(server.url());
    if (backpressure) {
        QCOMPARE(request.setBackpressureLimitBytes(16384), QCNetworkConfigUpdateResult::Applied);
    }
    auto *reply = manager.get(request);
    QSignalSpy done(reply, &QCNetworkReply::finished);
    QTRY_VERIFY(reply->bytesAvailable() > 0);
    if (backpressure) {
        QTRY_VERIFY(reply->isBackpressureActive());
    } else {
        reply->pauseTransport(PauseMode::Recv);
        QCOMPARE(reply->state(), ReplyState::Paused);
    }
    QTest::qWait(900);
    QVERIFY(!reply->isFinished());
    QByteArray body = reply->readAll().value();
    connect(reply, &QCNetworkReply::readyRead, reply, [&body, reply]() {
        body += reply->readAll().value_or(QByteArray{});
    });
    if (!backpressure) {
        reply->resumeTransport();
    }
    QTRY_VERIFY_WITH_TIMEOUT(reply->isFinished(), 3000);
    QCOMPARE(reply->error(), NetworkError::NoError);
    body += reply->readAll().value();
    QCOMPARE(body, expected);
    QCOMPARE(done.size(), 1);
}

void TestQCNetworkReceiveIdleTimeout::sendPauseDoesNotSuspendReceive()
{
    IdleResponse slow;
    slow.headerDelayMs = 1200;
    IdleServer server({slow});
    QVERIFY(server.started());
    QCNetworkAccessManager manager;
    auto *reply = manager.get(idleRequest(server.url()));
    QSignalSpy done(reply, &QCNetworkReply::finished);
    QTRY_COMPARE(server.requests(), 1);
    reply->pauseTransport(PauseMode::Send);
    QVERIFY(done.wait(2000));
    QCOMPARE(reply->error(), NetworkError::ConnectionTimeout);
}

void TestQCNetworkReceiveIdleTimeout::uploadDoesNotRefreshReceive()
{
    IdleResponse slow;
    slow.headerDelayMs = 1800;
    IdleServer server({slow});
    QVERIFY(server.started());
    QCNetworkAccessManager manager;
    auto request = idleRequest(server.url(), 650ms);
    request.setRawHeader("Expect", "");
    QCOMPARE(request.setMaxUploadBytesPerSec(16 * 1024), QCNetworkConfigUpdateResult::Applied);
    auto *reply = manager.post(request, QByteArray(128 * 1024, 'x'));
    QSignalSpy done(reply, &QCNetworkReply::finished);
    QVERIFY(done.wait(3000));
    QCOMPARE(reply->error(), NetworkError::ConnectionTimeout);
    QVERIFY(server.uploaded() > 0);
    QVERIFY(server.uploaded() < 128 * 1024);
}

void TestQCNetworkReceiveIdleTimeout::mockFirstResponseWait()
{
    QCNetworkAccessManager manager;
    QCNetworkMockHandler mock;
    TestSupport::setMockHandler(manager, &mock);
    const QUrl url(QStringLiteral("http://mock.invalid/idle"));
    mock.mockResponse(HttpMethod::Get, url, QByteArrayLiteral("ok"));
    mock.setGlobalDelay(300);
    auto *reply = manager.get(idleRequest(url, 50ms));
    QSignalSpy done(reply, &QCNetworkReply::finished);
    QVERIFY(done.wait(2000));
    QCOMPARE(reply->error(), NetworkError::ConnectionTimeout);
    QVERIFY(reply->isReceiveIdleTimeout());
    QCOMPARE(reply->diagnosticCurlCode(), 0);
    QTest::qWait(700);
    QCOMPARE(done.size(), 1);
    QCOMPARE(reply->readAll().value(), QByteArray{});
}

QTEST_GUILESS_MAIN(TestQCNetworkReceiveIdleTimeout)
