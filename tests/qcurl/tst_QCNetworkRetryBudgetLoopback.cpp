#include "QCNetworkAccessManager.h"
#include "QCNetworkDownloadToDeviceJob.h"
#include "QCNetworkReply.h"
#include "QCNetworkRequest.h"
#include "QCNetworkResumableDownloadJob.h"
#include "QCNetworkSchedulerPolicy.h"
#include "private/QCNetworkRetryPolicy_p.h"
#include "qcnetwork_retry_policy_test_helper.h"
#include "qcurl_http_script_server.h"

#include <QBuffer>
#include <QFile>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

#include <curl/curl.h>

using namespace QCurl;
using namespace std::chrono_literals;

class TestQCNetworkRetryBudgetLoopback final : public QObject
{
    Q_OBJECT

public:
    TestQCNetworkRetryBudgetLoopback() = default;

private Q_SLOTS:
    void init() { Internal::setRetryJitterFractionForTest(0.0); }
    void cleanup() { Internal::setRetryJitterFractionForTest(std::nullopt); }
    void rollbackFailureDoesNotSend();
    void rejectedBudgetPreservesFile();
    void cumulativeBudget_data();
    void cumulativeBudget();
    void elapsedIncludesTransferAndDoesNotRestart();
    void queueTimeIsExcluded();
    void cancelAcceptedWaitingRetry();
    void rejectedBudgetPreservesOutputDevice();
    void destroyingReplyFromRetrySignalDoesNotSendAgain();

private:
    Q_DISABLE_COPY_MOVE(TestQCNetworkRetryBudgetLoopback)
};

void TestQCNetworkRetryBudgetLoopback::rollbackFailureDoesNotSend()
{
    const auto old     = qgetenv("QCURL_TEST_FORCE_DOWNLOAD_RESTORE_ERROR");
    const auto restore = qScopeGuard([old]() {
        if (old.isNull()) {
            qunsetenv("QCURL_TEST_FORCE_DOWNLOAD_RESTORE_ERROR");
        } else {
            qputenv("QCURL_TEST_FORCE_DOWNLOAD_RESTORE_ERROR", old);
        }
    });
    qputenv("QCURL_TEST_FORCE_DOWNLOAD_RESTORE_ERROR", "1");
    HttpScriptServer server({HttpScriptServer::response(503, QByteArrayLiteral("file-body"))});
    QVERIFY(server.start());
    QCNetworkAccessManager manager;
    QCNetworkRequest request(server.url());
    request.setRetryPolicy(TestSupport::makeRetryPolicyOrFail(2, 10ms));
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QCNetworkResumableDownloadJob job(&manager, request, dir.filePath(QStringLiteral("download")));
    QSignalSpy done(&job, &QCNetworkTransferJob::finished);
    job.start();
    QVERIFY(done.wait(3000));
    QCOMPARE(job.reply()->error(), NetworkError::OutputDeviceError);
    QVERIFY(job.reply()->errorString().contains(QStringLiteral("restore failure")));
    QCOMPARE(job.reply()->retryAttemptCount(), 0);
    QCOMPARE(job.reply()->cumulativeRetryDelay(), 0ms);
    QTest::qWait(30);
    QCOMPARE(server.requests().size(), 1);
    job.reply()->deleteLater();
}

void TestQCNetworkRetryBudgetLoopback::rejectedBudgetPreservesFile()
{
    auto partial = HttpScriptServer::response(200, QByteArrayLiteral("file-body"));
    partial.headers.replace("Content-Length: 9", "Content-Length: 64");
    HttpScriptServer server({partial});
    QVERIFY(server.start());
    QCNetworkAccessManager manager;
    auto policy = TestSupport::makeRetryPolicyOrFail(2, 10ms);
    QCOMPARE(policy.setMaxCumulativeDelay(0ms), QCNetworkRetryPolicy::UpdateResult::Applied);
    policy.setRetryableErrors({fromCurlCode(CURLE_PARTIAL_FILE)});
    QCNetworkRequest request(server.url());
    request.setRetryPolicy(policy);
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const auto path = dir.filePath(QStringLiteral("download"));
    QCNetworkResumableDownloadJob job(&manager, request, path);
    QSignalSpy done(&job, &QCNetworkTransferJob::finished);
    job.start();
    QVERIFY(done.wait(3000));
    QCOMPARE(job.reply()->error(), fromCurlCode(CURLE_PARTIAL_FILE));
    QCOMPARE(job.reply()->retryAttemptCount(), 0);
    QFile output(path);
    QVERIFY(output.open(QIODevice::ReadOnly));
    QCOMPARE(output.readAll(), QByteArrayLiteral("file-body"));
    QCOMPARE(server.requests().size(), 1);
    job.reply()->deleteLater();
}

void TestQCNetworkRetryBudgetLoopback::cumulativeBudget_data()
{
    QTest::addColumn<int>("budget");
    QTest::addColumn<int>("retries");
    QTest::newRow("unlimited") << -1 << 2;
    QTest::newRow("zero") << 0 << 0;
    QTest::newRow("before-first") << 4 << 0;
    QTest::newRow("exact-first") << 5 << 1;
    QTest::newRow("before-second") << 9 << 1;
    QTest::newRow("exact-second") << 10 << 2;
}

void TestQCNetworkRetryBudgetLoopback::cumulativeBudget()
{
    QFETCH(int, budget);
    QFETCH(int, retries);
    HttpScriptServer server({HttpScriptServer::response(503, QByteArrayLiteral("failure-body"))});
    QVERIFY(server.start());
    QCNetworkAccessManager manager;
    auto policy = TestSupport::makeRetryPolicyOrFail(2, 10ms, 1.0, 10ms);
    if (budget >= 0) {
        QCOMPARE(policy.setMaxCumulativeDelay(std::chrono::milliseconds(budget)),
                 QCNetworkRetryPolicy::UpdateResult::Applied);
    }
    QCNetworkRequest request(server.url());
    request.setRetryPolicy(policy);
    auto *reply = manager.get(request);
    QCOMPARE(reply->retryAttemptCount(), 0);
    QCOMPARE(reply->cumulativeRetryDelay(), 0ms);
    QSignalSpy done(reply, &QCNetworkReply::finished);
    QVERIFY(done.wait(3000));
    QCOMPARE(server.requests().size(), retries + 1);
    QCOMPARE(reply->retryAttemptCount(), retries);
    QCOMPARE(reply->cumulativeRetryDelay(), retries * 5ms);
    QCOMPARE(reply->error(), NetworkError::HttpServiceUnavailable);
    QCOMPARE(reply->readAll().value(), QByteArrayLiteral("failure-body"));
    reply->deleteLater();
}

void TestQCNetworkRetryBudgetLoopback::elapsedIncludesTransferAndDoesNotRestart()
{
    auto response    = HttpScriptServer::response(503, QByteArrayLiteral("slow-failure"));
    response.delayMs = 80;
    HttpScriptServer server({response});
    QVERIFY(server.start());
    QCNetworkAccessManager manager;
    auto policy = TestSupport::makeRetryPolicyOrFail(4, 0ms);
    QCOMPARE(policy.setMaxElapsedTime(120ms), QCNetworkRetryPolicy::UpdateResult::Applied);
    QCNetworkRequest request(server.url());
    request.setRetryPolicy(policy);
    auto *reply = manager.get(request);
    QSignalSpy done(reply, &QCNetworkReply::finished);
    QVERIFY(done.wait(3000));
    QCOMPARE(server.requests().size(), 2);
    QCOMPARE(reply->retryAttemptCount(), 1);
    QVERIFY(reply->durationMs() >= 160);
    QCOMPARE(reply->readAll().value(), QByteArrayLiteral("slow-failure"));
    reply->deleteLater();
}

void TestQCNetworkRetryBudgetLoopback::queueTimeIsExcluded()
{
    auto hold    = HttpScriptServer::response(200, QByteArrayLiteral("hold"));
    hold.delayMs = 250;
    HttpScriptServer server({hold,
                             HttpScriptServer::response(503, QByteArrayLiteral("retry")),
                             HttpScriptServer::response(200, QByteArrayLiteral("ok"))});
    QVERIFY(server.start());
    QCNetworkAccessManager manager;
    auto scheduler = manager.schedulerPolicy();
    scheduler.setMaxConcurrentRequests(1);
    QVERIFY(manager.setSchedulerPolicy(scheduler));
    auto *first = manager.get(QCNetworkRequest(server.url()));
    QTRY_COMPARE_WITH_TIMEOUT(server.requests().size(), 1, 1000);
    auto policy = TestSupport::makeRetryPolicyOrFail(1, 0ms);
    QCOMPARE(policy.setMaxElapsedTime(100ms), QCNetworkRetryPolicy::UpdateResult::Applied);
    QCNetworkRequest request(server.url());
    request.setRetryPolicy(policy);
    auto *second = manager.get(request);
    QSignalSpy done(second, &QCNetworkReply::finished);
    QVERIFY(done.wait(3000));
    QCOMPARE(server.requests().size(), 3);
    QCOMPARE(second->retryAttemptCount(), 1);
    QCOMPARE(second->error(), NetworkError::NoError);
    QCOMPARE(second->readAll().value(), QByteArrayLiteral("ok"));
    first->deleteLater();
    second->deleteLater();
}

void TestQCNetworkRetryBudgetLoopback::cancelAcceptedWaitingRetry()
{
    HttpScriptServer server({HttpScriptServer::response(503, QByteArrayLiteral("fail"))});
    QVERIFY(server.start());
    QCNetworkAccessManager manager;
    auto policy = TestSupport::makeRetryPolicyOrFail(2, 200ms, 1.0, 200ms);
    QCNetworkRequest request(server.url());
    request.setRetryPolicy(policy);
    auto *reply = manager.get(request);
    connect(reply, &QCNetworkReply::retryAttempt, reply, [reply]() {
        QTimer::singleShot(0, reply, [reply]() { reply->cancel(); });
    });
    QSignalSpy done(reply, &QCNetworkReply::finished);
    QVERIFY(done.wait(3000));
    QCOMPARE(reply->state(), ReplyState::Cancelled);
    QCOMPARE(reply->retryAttemptCount(), 1);
    QCOMPARE(reply->cumulativeRetryDelay(), 100ms);
    QTest::qWait(150);
    QCOMPARE(server.requests().size(), 1);
    reply->deleteLater();
}

void TestQCNetworkRetryBudgetLoopback::rejectedBudgetPreservesOutputDevice()
{
    HttpScriptServer server({HttpScriptServer::response(503, QByteArrayLiteral("body"))});
    QVERIFY(server.start());
    QCNetworkAccessManager manager;
    auto policy = TestSupport::makeRetryPolicyOrFail(2, 10ms);
    QCOMPARE(policy.setMaxCumulativeDelay(0ms), QCNetworkRetryPolicy::UpdateResult::Applied);
    QCNetworkRequest request(server.url());
    request.setRetryPolicy(policy);
    QBuffer output;
    QVERIFY(output.open(QIODevice::ReadWrite));
    output.write("prefix-");
    QCNetworkDownloadToDeviceJob job(&manager, request, &output);
    QSignalSpy done(&job, &QCNetworkTransferJob::finished);
    job.start();
    QVERIFY(done.wait(3000));
    QCOMPARE(server.requests().size(), 1);
    QCOMPARE(output.data(), QByteArrayLiteral("prefix-body"));
    QCOMPARE(job.reply()->retryAttemptCount(), 0);
    QCOMPARE(job.reply()->cumulativeRetryDelay(), 0ms);
    QCOMPARE(job.reply()->error(), NetworkError::HttpServiceUnavailable);
    job.reply()->deleteLater();
}

void TestQCNetworkRetryBudgetLoopback::destroyingReplyFromRetrySignalDoesNotSendAgain()
{
    HttpScriptServer server({HttpScriptServer::response(503, QByteArrayLiteral("fail"))});
    QVERIFY(server.start());
    QCNetworkAccessManager manager;
    QCNetworkRequest request(server.url());
    request.setRetryPolicy(TestSupport::makeRetryPolicyOrFail(2, 10ms));
    QPointer<QCNetworkReply> reply = manager.get(request);
    connect(reply, &QCNetworkReply::retryAttempt, &manager, [reply]() { delete reply.data(); });
    QTRY_VERIFY_WITH_TIMEOUT(!reply, 3000);
    QTest::qWait(30);
    QCOMPARE(server.requests().size(), 1);
}

QTEST_GUILESS_MAIN(TestQCNetworkRetryBudgetLoopback)
#include "tst_QCNetworkRetryBudgetLoopback.moc"
