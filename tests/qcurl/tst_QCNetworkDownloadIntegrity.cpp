#include "QCNetworkAccessManager.h"
#include "QCNetworkDownloadToDeviceJob.h"
#include "QCNetworkReply.h"
#include "QCNetworkRequest.h"
#include "QCNetworkResumableDownloadJob.h"
#include "QCNetworkRetryPolicy.h"
#include "qcurl_http_script_server.h"

#include <QBuffer>
#include <QFile>
#include <QPointer>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

#include <curl/curl.h>

using namespace QCurl;

namespace {

QCNetworkRequest retryRequest(const QUrl &url)
{
    QCNetworkRetryPolicy policy;
    if (policy.setMaxRetries(1) != QCNetworkRetryPolicy::UpdateResult::Applied
        || policy.setInitialDelay(std::chrono::milliseconds(0))
               != QCNetworkRetryPolicy::UpdateResult::Applied) {
        qFatal("Invalid test retry policy");
    }
    policy.setRetryableErrors(
        {NetworkError::HttpServiceUnavailable, fromCurlCode(CURLE_PARTIAL_FILE)});
    QCNetworkRequest request(url);
    request.setRetryPolicy(policy);
    return request;
}

HttpScriptServer::Response failedAttemptResponse(bool partial)
{
    if (partial) {
        return {"HTTP/1.1 200 OK\r\nContent-Length: 20\r\nConnection: close\r\n\r\n", "BAD"};
    }
    return HttpScriptServer::response(503, "BAD");
}

QByteArray readFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    return file.readAll();
}

} // namespace

class tst_QCNetworkDownloadIntegrity : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void borrowedDeviceDoesNotRetryAfterDelivery();
    void fileRetryRestoresOutput_data();
    void fileRetryRestoresOutput();
    void rejectedResponsePreservesExistingFile();
    void appendRetryRestoresOriginalRange();
    void retryBeforeDelivery();
    void cancelDuringRetryBackoff_data();
    void cancelDuringRetryBackoff();
    void failureSlotMayDeleteJob();
    void incompleteOverwriteRangePreservesExistingFile();
    void restoreFailureStopsRetry();
    void ranges_data();
    void ranges();
};

void tst_QCNetworkDownloadIntegrity::borrowedDeviceDoesNotRetryAfterDelivery()
{
    HttpScriptServer server(
        {HttpScriptServer::response(503, "BAD"), HttpScriptServer::response(200, "GOOD")});
    QVERIFY(server.start());
    QCNetworkAccessManager manager;
    QBuffer output;
    QVERIFY(output.open(QIODevice::ReadWrite));
    QCNetworkDownloadToDeviceJob job(&manager, retryRequest(server.url()), &output);
    QSignalSpy finished(&job, &QCNetworkTransferJob::finished);
    QSignalSpy failed(&job, &QCNetworkTransferJob::failed);
    job.start();
    QVERIFY(finished.wait());
    QCOMPARE(server.requests().size(), 1);
    QCOMPARE(job.error(), NetworkError::HttpServiceUnavailable);
    QCOMPARE(output.data(), QByteArray("BAD"));
    QCOMPARE(finished.size(), 1);
    QCOMPARE(failed.size(), 1);
}

void tst_QCNetworkDownloadIntegrity::fileRetryRestoresOutput_data()
{
    QTest::addColumn<bool>("existing");
    QTest::addColumn<bool>("partial");
    QTest::newRow("new-503") << false << false;
    QTest::newRow("overwrite-503") << true << false;
    QTest::newRow("new-partial-200") << false << true;
    QTest::newRow("overwrite-partial-200") << true << true;
}

void tst_QCNetworkDownloadIntegrity::fileRetryRestoresOutput()
{
    QFETCH(bool, existing);
    QFETCH(bool, partial);
    const auto first = failedAttemptResponse(partial);
    HttpScriptServer server({first, HttpScriptServer::response(200, "GOOD")});
    QVERIFY(server.start());
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath("output");
    if (existing) {
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write("ORIGINAL"), 8);
    }
    QCNetworkAccessManager manager;
    QCNetworkResumableDownloadJob job(&manager, retryRequest(server.url()), path, true);
    QSignalSpy finished(&job, &QCNetworkTransferJob::finished);
    job.start();
    QVERIFY(finished.wait());
    QCOMPARE(job.error(), NetworkError::NoError);
    QCOMPARE(server.requests().size(), 2);
    QCOMPARE(readFile(path), QByteArray("GOOD"));
    QCOMPARE(finished.size(), 1);
}

void tst_QCNetworkDownloadIntegrity::rejectedResponsePreservesExistingFile()
{
    HttpScriptServer server({HttpScriptServer::response(503, "BAD")});
    QVERIFY(server.start());
    QTemporaryDir directory;
    const QString path = directory.filePath("output");
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    QCOMPARE(file.write("ORIGINAL"), 8);
    file.close();
    QCNetworkAccessManager manager;
    QCNetworkResumableDownloadJob job(&manager, retryRequest(server.url()), path, true);
    QSignalSpy finished(&job, &QCNetworkTransferJob::finished);
    job.start();
    QVERIFY(finished.wait());
    QCOMPARE(job.error(), NetworkError::HttpServiceUnavailable);
    QCOMPARE(readFile(path), QByteArray("ORIGINAL"));
}

void tst_QCNetworkDownloadIntegrity::appendRetryRestoresOriginalRange()
{
    const QByteArray headers = "HTTP/1.1 206 Partial Content\r\nContent-Range: bytes 4-7/8\r\n"
                               "Content-Length: 4\r\nConnection: close\r\n\r\n";
    HttpScriptServer server({{headers, "ef"}, {headers, "efgh"}});
    QVERIFY(server.start());
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath("output");
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    QCOMPARE(file.write("abcd"), 4);
    file.close();
    QCNetworkAccessManager manager;
    QCNetworkResumableDownloadJob job(&manager, retryRequest(server.url()), path);
    QSignalSpy finished(&job, &QCNetworkTransferJob::finished);
    QSignalSpy failed(&job, &QCNetworkTransferJob::failed);
    job.start();
    QVERIFY(finished.wait());
    QCOMPARE(job.error(), NetworkError::NoError);
    QCOMPARE(server.requests().size(), 2);
    for (const auto &request : server.requests()) {
        QVERIFY(request.contains("Range: bytes=4-\r\n"));
    }
    QCOMPARE(readFile(path), QByteArray("abcdefgh"));
    QCOMPARE(finished.size(), 1);
    QCOMPARE(failed.size(), 0);
}

void tst_QCNetworkDownloadIntegrity::retryBeforeDelivery()
{
    HttpScriptServer server(
        {HttpScriptServer::response(503, {}), HttpScriptServer::response(200, "GOOD")});
    QVERIFY(server.start());
    QCNetworkAccessManager manager;
    QBuffer output;
    QVERIFY(output.open(QIODevice::WriteOnly));
    QCNetworkDownloadToDeviceJob job(&manager, retryRequest(server.url()), &output);
    QSignalSpy finished(&job, &QCNetworkTransferJob::finished);
    job.start();
    QVERIFY(finished.wait());
    QCOMPARE(job.error(), NetworkError::NoError);
    QCOMPARE(server.requests().size(), 2);
    QCOMPARE(output.data(), QByteArray("GOOD"));
    QCOMPARE(finished.size(), 1);
}

void tst_QCNetworkDownloadIntegrity::cancelDuringRetryBackoff_data()
{
    QTest::addColumn<bool>("existing");
    QTest::addColumn<bool>("partial");
    QTest::newRow("existing-http-error") << true << false;
    QTest::newRow("existing-partial-response") << true << true;
    QTest::newRow("new-partial-response") << false << true;
}

void tst_QCNetworkDownloadIntegrity::cancelDuringRetryBackoff()
{
    QFETCH(bool, existing);
    QFETCH(bool, partial);
    auto first    = failedAttemptResponse(partial);
    first.delayMs = 100;
    HttpScriptServer server({first, HttpScriptServer::response(200, "GOOD")});
    QVERIFY(server.start());
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath("output");
    if (existing) {
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write("ORIGINAL"), 8);
    }
    const QByteArray expected = existing ? QByteArray("ORIGINAL") : QByteArray();
    auto request              = retryRequest(server.url());
    auto retry                = request.retryPolicy();
    QCOMPARE(retry.setInitialDelay(std::chrono::milliseconds(50)),
             QCNetworkRetryPolicy::UpdateResult::Applied);
    request.setRetryPolicy(retry);
    QCNetworkAccessManager manager;
    QCNetworkResumableDownloadJob job(&manager, request, path, true);
    QSignalSpy finished(&job, &QCNetworkTransferJob::finished);
    job.start();
    QTRY_VERIFY(job.reply());
    int attempts  = 0;
    bool restored = false;
    connect(job.reply(), &QCNetworkReply::retryAttempt, this, [&]() {
        ++attempts;
        restored = readFile(path) == expected;
        job.reply()->cancel();
    });
    QTRY_COMPARE(finished.size(), 1);
    QTest::qWait(100);
    QCOMPARE(attempts, 1);
    QVERIFY(restored);
    QCOMPARE(server.requests().size(), 1);
    QCOMPARE(job.error(), NetworkError::OperationCancelled);
    QCOMPARE(readFile(path), expected);
    QCOMPARE(finished.size(), 1);
}

void tst_QCNetworkDownloadIntegrity::failureSlotMayDeleteJob()
{
    HttpScriptServer server({HttpScriptServer::response(503, "BAD")});
    QVERIFY(server.start());
    QCNetworkAccessManager manager;
    QBuffer output;
    QVERIFY(output.open(QIODevice::WriteOnly));
    QPointer<QCNetworkDownloadToDeviceJob> job
        = new QCNetworkDownloadToDeviceJob(&manager, retryRequest(server.url()), &output);
    int failures    = 0;
    int completions = 0;
    connect(job, &QCNetworkTransferJob::finished, this, [&]() { ++completions; });
    connect(job, &QCNetworkTransferJob::failed, this, [&]() {
        ++failures;
        delete job.data();
    });
    job->start();
    QTRY_VERIFY(job.isNull());
    QCOMPARE(failures, 1);
    QCOMPARE(completions, 0);
    QCOMPARE(server.requests().size(), 1);
}

void tst_QCNetworkDownloadIntegrity::incompleteOverwriteRangePreservesExistingFile()
{
    HttpScriptServer server({{"HTTP/1.1 206 Partial Content\r\nContent-Range: bytes 0-5/6\r\n"
                              "Connection: close\r\n\r\n",
                              "ab"}});
    QVERIFY(server.start());
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath("output");
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    QCOMPARE(file.write("ORIGINAL"), 8);
    file.close();
    QCNetworkAccessManager manager;
    QCNetworkResumableDownloadJob job(&manager, server.url(), path, true);
    QSignalSpy finished(&job, &QCNetworkTransferJob::finished);
    job.start();
    QVERIFY(finished.wait());
    QVERIFY(job.error() != NetworkError::NoError);
    QCOMPARE(readFile(path), QByteArray("ORIGINAL"));
    QCOMPARE(finished.size(), 1);
}

void tst_QCNetworkDownloadIntegrity::restoreFailureStopsRetry()
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
                              "BAD"},
                             HttpScriptServer::response(200, "GOOD")});
    QVERIFY(server.start());
    QTemporaryDir directory;
    const QString path = directory.filePath("output");
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    QCOMPARE(file.write("ORIGINAL"), 8);
    file.close();
    QCNetworkAccessManager manager;
    QCNetworkResumableDownloadJob job(&manager, retryRequest(server.url()), path, true);
    QSignalSpy finished(&job, &QCNetworkTransferJob::finished);
    QSignalSpy failed(&job, &QCNetworkTransferJob::failed);
    job.start();
    QVERIFY(finished.wait());
    QCoreApplication::processEvents();
    QCOMPARE(job.error(), NetworkError::OutputDeviceError);
    QCOMPARE(server.requests().size(), 1);
    QCOMPARE(readFile(path), QByteArray("ORIGINAL"));
    QCOMPARE(finished.size(), 1);
    QCOMPARE(failed.size(), 1);
}

void tst_QCNetworkDownloadIntegrity::ranges_data()
{
    QTest::addColumn<QByteArray>("headers");
    QTest::addColumn<QByteArray>("body");
    QTest::addColumn<bool>("success");
    QTest::newRow("valid") << QByteArray("Content-Range: bytes 4-5/6\r\nContent-Length: 2\r\n")
                           << QByteArray("ef") << true;
    QTest::newRow("length-conflict")
        << QByteArray("Content-Range: bytes 4-9/10\r\nContent-Length: 2\r\n") << QByteArray("ef")
        << false;
    QTest::newRow("start-conflict")
        << QByteArray("Content-Range: bytes 3-4/5\r\nContent-Length: 2\r\n") << QByteArray("ef")
        << false;
    QTest::newRow("end-before-start")
        << QByteArray("Content-Range: bytes 4-3/6\r\nContent-Length: 2\r\n") << QByteArray("ef")
        << false;
    QTest::newRow("end-beyond-total")
        << QByteArray("Content-Range: bytes 4-6/6\r\nContent-Length: 3\r\n") << QByteArray("efg")
        << false;
    QTest::newRow("short-body") << QByteArray("Content-Range: bytes 4-6/7\r\nContent-Length: 3\r\n")
                                << QByteArray("ef") << false;
    QTest::newRow("long-body") << QByteArray("Content-Range: bytes 4-5/6\r\n") << QByteArray("efg")
                               << false;
    QTest::newRow("incomplete-total")
        << QByteArray("Content-Range: bytes 4-5/8\r\nContent-Length: 2\r\n") << QByteArray("ef")
        << false;
    QTest::newRow("encoded-range") << QByteArray(
        "Content-Range: bytes 4-5/6\r\nContent-Length: 2\r\nContent-Encoding: gzip\r\n")
                                   << QByteArray("ef") << false;
}

void tst_QCNetworkDownloadIntegrity::ranges()
{
    QFETCH(QByteArray, headers);
    QFETCH(QByteArray, body);
    QFETCH(bool, success);
    HttpScriptServer server(
        {{"HTTP/1.1 206 Partial Content\r\n" + headers + "Connection: close\r\n\r\n", body}});
    QVERIFY(server.start());
    QTemporaryDir directory;
    const QString path = directory.filePath("output");
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    QCOMPARE(file.write("abcd"), 4);
    file.close();
    QCNetworkAccessManager manager;
    QCNetworkResumableDownloadJob job(&manager, server.url(), path, false);
    QSignalSpy finished(&job, &QCNetworkTransferJob::finished);
    job.start();
    QVERIFY(finished.wait());
    QCOMPARE(job.error() == NetworkError::NoError, success);
    QCOMPARE(readFile(path), success ? QByteArray("abcdef") : QByteArray("abcd"));
    QCOMPARE(finished.size(), 1);
}

QTEST_GUILESS_MAIN(tst_QCNetworkDownloadIntegrity)
#include "tst_QCNetworkDownloadIntegrity.moc"
