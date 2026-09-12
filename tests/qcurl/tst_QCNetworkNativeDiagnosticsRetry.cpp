#include "QCNetworkAccessManager.h"
#include "QCNetworkMockHandler.h"
#include "QCNetworkReply.h"
#include "QCNetworkRequest.h"
#include "QCNetworkResumableDownloadJob.h"
#include "QCNetworkRetryPolicy.h"
#include "qcnetwork_mock_test_support.h"
#include "qcurl_http_script_server.h"
#include "tst_QCNetworkNativeDiagnostics.h"

#include <QFile>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

#include <curl/curl.h>

using namespace QCurl;

QCNetworkRequest tst_QCNetworkNativeDiagnostics::retryRequest(const QUrl &url, int delayMs)
{
    QCNetworkRetryPolicy policy;
    if (policy.setMaxRetries(1) != QCNetworkRetryPolicy::UpdateResult::Applied
        || policy.setInitialDelay(std::chrono::milliseconds(delayMs))
               != QCNetworkRetryPolicy::UpdateResult::Applied) {
        qFatal("诊断测试的重试参数无效");
    }
    policy.setRetryableErrors({fromCurlCode(CURLE_GOT_NOTHING), fromCurlCode(CURLE_PARTIAL_FILE)});
    QCNetworkRequest request(url);
    request.setRetryPolicy(policy);
    return request;
}

void tst_QCNetworkNativeDiagnostics::retryResult_data()
{
    QTest::addColumn<bool>("success");
    QTest::addColumn<bool>("scheduled");
    QTest::newRow("direct-success") << true << false;
    QTest::newRow("direct-exhausted") << false << false;
    QTest::newRow("scheduled-success") << true << true;
    QTest::newRow("scheduled-exhausted") << false << true;
}

void tst_QCNetworkNativeDiagnostics::retryResult()
{
    QFETCH(bool, success);
    QFETCH(bool, scheduled);
    const auto finalResponse
        = success ? HttpScriptServer::response(200, "ok")
                  : HttpScriptServer::Response{"HTTP/1.1 200 OK\r\nContent-Length: 20\r\n"
                                               "Connection: close\r\n\r\n",
                                               "partial"};
    HttpScriptServer server({{}, finalResponse});
    QVERIFY(server.start());
    QCNetworkAccessManager manager;
    manager.enableRequestScheduler(scheduled);
    auto *reply = manager.get(retryRequest(server.url()));
    QSignalSpy finished(reply, &QCNetworkReply::finished);
    QSignalSpy retries(reply, &QCNetworkReply::retryAttempt);
    int intermediateCode = -1;
    connect(reply, &QCNetworkReply::retryAttempt, this, [&]() {
        intermediateCode = reply->diagnosticCurlCode();
    });
    QVERIFY(finished.wait(5000));
    QCOMPARE(intermediateCode, 0);
    QCOMPARE(retries.size(), 1);
    QCOMPARE(server.requests().size(), 2);
    QCOMPARE(reply->httpStatusCode(), 200);
    QCOMPARE(reply->error(), success ? NetworkError::NoError : fromCurlCode(CURLE_PARTIAL_FILE));
    QCOMPARE(reply->diagnosticCurlCode(), success ? 0 : int(CURLE_PARTIAL_FILE));
    QCOMPARE(finished.size(), 1);
}

void tst_QCNetworkNativeDiagnostics::retryRestoreFailure()
{
    const QByteArray previous = qgetenv("QCURL_TEST_FORCE_DOWNLOAD_RESTORE_ERROR");
    const auto restore        = qScopeGuard([previous]() {
        if (previous.isNull()) {
            qunsetenv("QCURL_TEST_FORCE_DOWNLOAD_RESTORE_ERROR");
        } else {
            qputenv("QCURL_TEST_FORCE_DOWNLOAD_RESTORE_ERROR", previous);
        }
    });
    qputenv("QCURL_TEST_FORCE_DOWNLOAD_RESTORE_ERROR", "1");
    HttpScriptServer server({{"HTTP/1.1 200 OK\r\nContent-Length: 20\r\n"
                              "Connection: close\r\n\r\n",
                              "partial"}});
    QVERIFY(server.start());
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("output"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    QCOMPARE(file.write("original"), 8);
    file.close();
    QCNetworkAccessManager manager;
    QCNetworkResumableDownloadJob job(&manager, retryRequest(server.url()), path, true);
    QSignalSpy finished(&job, &QCNetworkTransferJob::finished);
    job.start();
    QVERIFY(finished.wait(5000));
    QVERIFY(job.reply());
    QCOMPARE(job.error(), NetworkError::OutputDeviceError);
    QCOMPARE(job.reply()->httpStatusCode(), 200);
    QCOMPARE(job.reply()->diagnosticCurlCode(), int(CURLE_PARTIAL_FILE));
    QCOMPARE(server.requests().size(), 1);
    QCOMPARE(finished.size(), 1);
}

void tst_QCNetworkNativeDiagnostics::cancelDuringBackoff_data()
{
    QTest::addColumn<bool>("abort");
    QTest::newRow("cancel") << false;
    QTest::newRow("local-error") << true;
}

void tst_QCNetworkNativeDiagnostics::cancelDuringBackoff()
{
    QFETCH(bool, abort);
    HttpScriptServer server({{}});
    QVERIFY(server.start());
    QCNetworkAccessManager manager;
    auto *reply = manager.get(retryRequest(server.url(), 100));
    QSignalSpy finished(reply, &QCNetworkReply::finished);
    int intermediateCode = -1;
    connect(reply, &QCNetworkReply::retryAttempt, this, [&]() {
        intermediateCode = reply->diagnosticCurlCode();
        if (abort) {
            reply->abortWithError(NetworkError::InvalidRequest, QStringLiteral("本地终止"));
        } else {
            reply->cancel();
        }
    });
    QVERIFY(finished.wait(5000));
    QCOMPARE(intermediateCode, 0);
    QCOMPARE(reply->state(), abort ? ReplyState::Error : ReplyState::Cancelled);
    QCOMPARE(reply->error(),
             abort ? NetworkError::InvalidRequest : NetworkError::OperationCancelled);
    QCOMPARE(reply->httpStatusCode(), 0);
    QCOMPARE(reply->diagnosticCurlCode(), 0);
    QTest::qWait(150);
    QCOMPARE(server.requests().size(), 1);
    QCOMPARE(finished.size(), 1);
}

void tst_QCNetworkNativeDiagnostics::mockAfterNetworkFailure()
{
    HttpScriptServer server({{}});
    QVERIFY(server.start());
    QCNetworkAccessManager manager;
    QCNetworkMockHandler mock;
    TestSupport::setMockHandler(manager, &mock);
    auto *reply = manager.get(retryRequest(server.url()));
    QSignalSpy finished(reply, &QCNetworkReply::finished);
    QSignalSpy retries(reply, &QCNetworkReply::retryAttempt);
    connect(reply, &QCNetworkReply::retryAttempt, this, [&]() {
        mock.mockResponse(HttpMethod::Get, server.url(), "mock-result", 200);
    });
    QVERIFY(finished.wait(5000));
    QCOMPARE(reply->error(), NetworkError::NoError);
    QCOMPARE(reply->httpStatusCode(), 200);
    QCOMPARE(reply->diagnosticCurlCode(), 0);
    QCOMPARE(reply->readAll().value(), QByteArray("mock-result"));
    QCOMPARE(retries.size(), 1);
    QCOMPARE(server.requests().size(), 1);
}
