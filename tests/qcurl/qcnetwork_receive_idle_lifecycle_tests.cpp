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

#include <QScopeGuard>
#include <QSignalSpy>
#include <QtTest>

using namespace QCurl;
using namespace std::chrono_literals;

static QCNetworkRequest makeIdleRequest(const QUrl &url, std::chrono::milliseconds idle = 400ms)
{
    QCNetworkRequest request(url);
    QCNetworkTimeoutConfig config;
    config.setConnectTimeout(idle);
    config.setReceiveIdleTimeout(idle);
    request.setTimeoutConfig(config);
    auto ssl = request.sslConfig();
    ssl.setVerifyPeer(false);
    ssl.setVerifyHost(false);
    request.setSslConfig(ssl);
    return request;
}

void TestQCNetworkReceiveIdleTimeout::schedulerQueueAndCancel()
{
    IdleResponse stream;
    stream.chunks       = {"a", "b", "c", "d", "e", "f"};
    stream.chunkDelayMs = 150;
    IdleServer server({stream, IdleResponse{}});
    QVERIFY(server.started());
    QCNetworkAccessManager manager;
    auto policy = manager.schedulerPolicy();
    policy.setMaxConcurrentRequests(1);
    policy.setMaxRequestsPerHost(1);
    QVERIFY(manager.setSchedulerPolicy(policy));
    manager.enableRequestScheduler(true);
    auto *active = manager.get(makeIdleRequest(server.url()));
    QTRY_COMPARE(server.requests(), 1);
    auto *queued    = manager.get(makeIdleRequest(server.url()));
    auto *cancelled = manager.get(makeIdleRequest(server.url()));
    QSignalSpy activeDone(active, &QCNetworkReply::finished);
    QSignalSpy queuedDone(queued, &QCNetworkReply::finished);
    QSignalSpy cancelledDone(cancelled, &QCNetworkReply::finished);
    cancelled->cancel();
    QCOMPARE(cancelledDone.size(), 1);
    QVERIFY(queuedDone.wait(4000));
    QCOMPARE(activeDone.size(), 1);
    QCOMPARE(queuedDone.size(), 1);
    QCOMPARE(active->error(), NetworkError::NoError);
    QCOMPARE(queued->error(), NetworkError::NoError);
    QCOMPARE(cancelled->error(), NetworkError::OperationCancelled);
    QCOMPARE(server.requests(), 2);
}

void TestQCNetworkReceiveIdleTimeout::receiveHeaderSlowStream()
{
    IdleResponse response;
    // libcurl 为折行预读保留末行；两次间隔仍须明显小于接收时限。
    response.headerLineDelayMs = 250;
    IdleServer server({response});
    QVERIFY(server.started());
    QCNetworkAccessManager manager;
    auto *reply = manager.get(makeIdleRequest(server.url(), 650ms));
    QSignalSpy done(reply, &QCNetworkReply::finished);
    QVERIFY(done.wait(3000));
    QCOMPARE(reply->error(), NetworkError::NoError);
    QCOMPARE(reply->httpStatusCode(), 200);
    QCOMPARE(reply->readAll().value(), QByteArrayLiteral("ok"));
}

void TestQCNetworkReceiveIdleTimeout::unsetTimeoutPreservesWaiting()
{
    IdleResponse response;
    response.headerDelayMs = 800;
    IdleServer server({response});
    QVERIFY(server.started());
    QCNetworkAccessManager manager;
    auto request = makeIdleRequest(server.url());
    auto config  = request.timeoutConfig();
    config.setReceiveIdleTimeout(std::nullopt);
    request.setTimeoutConfig(config);
    auto *reply = manager.get(request);
    QSignalSpy done(reply, &QCNetworkReply::finished);
    QVERIFY(done.wait(3000));
    QCOMPARE(reply->error(), NetworkError::NoError);
    QCOMPARE(reply->readAll().value(), QByteArrayLiteral("ok"));
    QCOMPARE(server.requests(), 1);
}

void TestQCNetworkReceiveIdleTimeout::staleReuseReconnect()
{
    IdleResponse first;
    first.keepAlive    = true;
    first.closeDelayMs = 200;
    IdleResponse second;
    second.handshakeDelayMs = 850;
    second.headerDelayMs    = 850;
    IdleServer server({first, second}, true);
    QVERIFY(server.started());
    QCNetworkAccessManager manager;
    auto request  = makeIdleRequest(server.url(), 1300ms);
    auto *initial = manager.get(request);
    QSignalSpy firstDone(initial, &QCNetworkReply::finished);
    QVERIFY(firstDone.wait(3000));
    QCOMPARE(initial->error(), NetworkError::NoError);
    // 第二请求已经复用第一连接等待响应；其断开迫使 libcurl 隐式重连。
    auto *reply = manager.get(request);
    QSignalSpy done(reply, &QCNetworkReply::finished);
    QVERIFY(done.wait(5000));
    QCOMPARE(reply->error(), NetworkError::NoError);
    QCOMPARE(reply->retryAttemptCount(), 0);
    QCOMPARE(server.connections(), 2);
    QCOMPARE(server.requests(), 3);
    QCOMPARE(reply->readAll().value(), QByteArrayLiteral("ok"));
}

void TestQCNetworkReceiveIdleTimeout::callbackCancelAndDestroy()
{
    IdleResponse stream;
    stream.chunks       = {"a", "b", "c"};
    stream.chunkDelayMs = 200;
    IdleServer server({stream});
    QVERIFY(server.started());
    QCNetworkAccessManager manager;
    auto *reply = manager.get(makeIdleRequest(server.url()));
    QSignalSpy done(reply, &QCNetworkReply::finished);
    connect(reply, &QCNetworkReply::readyRead, reply, [reply]() { reply->cancel(); });
    QVERIFY(done.wait(3000));
    QCOMPARE(reply->error(), NetworkError::OperationCancelled);
    QPointer<QCNetworkReply> doomed = manager.get(makeIdleRequest(server.url()));
    connect(doomed, &QCNetworkReply::readyRead, doomed, [doomed]() {
        doomed->deleteLater();
        // 允许 deferred delete 在 callback 的嵌套事件处理中发生，record 必须继续保活。
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    });
    QTRY_VERIFY_WITH_TIMEOUT(!doomed, 3000);
    QTest::qWait(700);
    QCOMPARE(done.size(), 1);
    QCOMPARE(server.requests(), 2);
}

void TestQCNetworkReceiveIdleTimeout::pauseResumeThenStall()
{
    IdleResponse stall;
    stall.chunks       = {"a", "b"};
    stall.chunkDelayMs = 2400;
    IdleServer server({stall});
    QVERIFY(server.started());
    QCNetworkAccessManager manager;
    auto *reply = manager.get(makeIdleRequest(server.url()));
    QSignalSpy done(reply, &QCNetworkReply::finished);
    QTRY_VERIFY(reply->bytesAvailable() == 1);
    reply->pauseTransport(PauseMode::Recv);
    QTest::qWait(800);
    QVERIFY(!reply->isFinished());
    reply->resumeTransport();
    QVERIFY(done.wait(1800));
    QCOMPARE(reply->error(), NetworkError::ConnectionTimeout);
    QCOMPARE(reply->readAll().value(), QByteArrayLiteral("a"));
    QCOMPARE(done.size(), 1);
}

void TestQCNetworkReceiveIdleTimeout::replyMovedToOwnerThread()
{
    IdleResponse stall;
    stall.headerDelayMs = 1200;
    IdleServer server({stall});
    QVERIFY(server.started());
    QThread worker;
    auto *manager = new QCNetworkAccessManager;
    manager->moveToThread(&worker);
    connect(&worker, &QThread::finished, manager, &QObject::deleteLater);
    worker.start();
    auto shutdown = qScopeGuard([&worker]() {
        worker.quit();
        worker.wait();
    });
    std::atomic<bool> completed{false};
    std::atomic<bool> correctThread{false};
    std::atomic<int> error{-1};
    QMetaObject::invokeMethod(
        manager,
        [manager, request = makeIdleRequest(server.url()), &completed, &correctThread, &error]() {
            auto *reply = manager->get(request);
            QObject::connect(reply,
                             &QCNetworkReply::finished,
                             reply,
                             [reply, &completed, &correctThread, &error]() {
                                 correctThread.store(QThread::currentThread() == reply->thread());
                                 error.store(static_cast<int>(reply->error()));
                                 completed.store(true);
                                 reply->deleteLater();
                             });
        },
        Qt::QueuedConnection);
    QTRY_VERIFY_WITH_TIMEOUT(completed.load(), 3000);
    QVERIFY(correctThread.load());
    QCOMPARE(error.load(), static_cast<int>(NetworkError::ConnectionTimeout));
}

void TestQCNetworkReceiveIdleTimeout::mockRetryDiscardsLateAttempt()
{
    QCNetworkAccessManager manager;
    QCNetworkMockHandler mock;
    TestSupport::setMockHandler(manager, &mock);
    const QUrl url(QStringLiteral("http://mock.invalid/late"));
    mock.mockResponse(HttpMethod::Get, url, QByteArrayLiteral("late"));
    mock.setGlobalDelay(600);
    mock.setCaptureEnabled(true);
    auto request = makeIdleRequest(url);
    QCNetworkRetryPolicy policy;
    QCOMPARE(policy.setMaxRetries(1), QCNetworkRetryPolicy::UpdateResult::Applied);
    QCOMPARE(policy.setInitialDelay(0ms), QCNetworkRetryPolicy::UpdateResult::Applied);
    request.setRetryPolicy(policy);
    auto *reply = manager.get(request);
    QSignalSpy done(reply, &QCNetworkReply::finished);
    QVERIFY(done.wait(2000));
    QCOMPARE(reply->error(), NetworkError::ConnectionTimeout);
    QCOMPARE(reply->retryAttemptCount(), 1);
    QCOMPARE(reply->readAll().value(), QByteArray{});
    const auto captured = mock.takeCapturedRequests();
    QCOMPARE(captured.size(), 2);
    QCOMPARE(captured.first().receiveIdleTimeoutMs(), std::optional<qint64>(400));
    QTest::qWait(700);
    QCOMPARE(done.size(), 1);
}
