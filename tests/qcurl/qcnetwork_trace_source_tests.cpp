#include "QCNetworkDefaultLogger.h"
#include "QCNetworkReply_p.h"
#include "private/QCCurlMultiTransferRecord_p.h"
#include "qcnetwork_logger_test.h"
#include "qcnetwork_trace_test_helpers.h"

#include <QtTest>

using namespace QCurl;
using namespace QCurl::Internal;

void TestQCNetworkLogger::testTraceSourceIsolation()
{
    ReplyTraceState trace(QUrl(QStringLiteral("https://example.invalid/path?query=marker")),
                          QCNetworkDiagnosticQueryPolicy::Full);
    trace.beginNetworkAttempt();
    const QString internal = TraceTest::decorated(trace,
                                                  CURLINFO_HEADER_OUT,
                                                  "[7-2] GET /path?query=marker HTTP/2\r\n\r\n",
                                                  {7, 2},
                                                  TraceSource::Internal,
                                                  2);
    QVERIFY(!internal.contains(QStringLiteral("query=marker")));
    QVERIFY(internal.contains(QStringLiteral("[7-2] GET /path?[REDACTED]")));
    const QString caller = TraceTest::decorated(trace,
                                                CURLINFO_HEADER_OUT,
                                                "[8-2] GET /path?query=marker HTTP/2\r\n\r\n",
                                                {8, 2});
    QVERIFY(caller.contains(QStringLiteral("[8-2] GET /path?query=marker")));
    const QString credentials
        = TraceTest::decorated(trace,
                               CURLINFO_TEXT,
                               "[8-2] Server auth using Basic with user 'credential-secret'\n",
                               {8, 2});
    QVERIFY(credentials.contains(QStringLiteral("[8-2] Server auth using Basic")));
    for (auto type : {CURLINFO_HEADER_IN, CURLINFO_HEADER_OUT, CURLINFO_DATA_IN}) {
        static_cast<void>(
            TraceTest::decorated(trace, type, "[7-2] \r\n", {7, 2}, TraceSource::Internal, 2));
        const auto log = TraceTest::decorated(trace, CURLINFO_TEXT, "[8-2] bare-secret\n", {8, 2});
        QVERIFY(!log.contains(QStringLiteral("bare-secret")));
        QVERIFY(log.contains(QStringLiteral("[8-2] [REDACTED: credential continuation]")));
    }
    static_cast<void>(TraceTest::consume(trace, CURLINFO_DATA_IN, "boundary"));
    QCOMPARE(TraceTest::consume(trace, CURLINFO_TEXT, "ordinary #123\n"),
             QStringLiteral("TEXT: ordinary #123"));
}

void TestQCNetworkLogger::testTraceInternalSwitchStaysClosed()
{
    ReplyTraceState trace(QUrl(QStringLiteral("https://example.invalid/path?query=marker")),
                          QCNetworkDiagnosticQueryPolicy::Full);
    trace.beginNetworkAttempt();
    QCOMPARE(TraceTest::decorated(trace,
                                  CURLINFO_TEXT,
                                  "[2-1] ordinary internal\n",
                                  {2, 1},
                                  TraceSource::Internal,
                                  2),
             QStringLiteral("TEXT: [2-1] ordinary internal"));
    // A 完整结束也不能证明 B 的首个 callback 正好从逻辑行首开始。
    for (quintptr token : {3, 2, 3}) {
        for (auto type : {CURLINFO_TEXT, CURLINFO_HEADER_IN, CURLINFO_DATA_IN}) {
            const auto log = TraceTest::decorated(trace,
                                                  type,
                                                  "[3-1] bare-secret\n\n",
                                                  {3, 1},
                                                  TraceSource::Internal,
                                                  token);
            QVERIFY(!log.contains(QStringLiteral("bare-secret")));
            QVERIFY(log.contains(QStringLiteral("[3-1] [REDACTED:")));
        }
    }
    QVERIFY(TraceTest::consume(trace, CURLINFO_HEADER_OUT, "GET /path?query=marker HTTP/2\r\n\r\n")
                .contains(QStringLiteral("query=marker")));
    trace.beginNetworkAttempt();
    QVERIFY(TraceTest::decorated(trace,
                                 CURLINFO_TEXT,
                                 "[2-1] Authorization: partial",
                                 {2, 1},
                                 TraceSource::Internal,
                                 2)
                .isEmpty());
    QVERIFY(!TraceTest::decorated(trace,
                                  CURLINFO_TEXT,
                                  "[3-1] tail-secret\n",
                                  {3, 1},
                                  TraceSource::Internal,
                                  3)
                 .contains(QStringLiteral("tail-secret")));
    trace.beginNetworkAttempt();
    QCOMPARE(TraceTest::decorated(trace,
                                  CURLINFO_TEXT,
                                  "[3-1] ordinary internal\n",
                                  {3, 1},
                                  TraceSource::Internal,
                                  3),
             QStringLiteral("TEXT: [3-1] ordinary internal"));
}

void TestQCNetworkLogger::testTraceUnknownSourceClosesTarget()
{
    ReplyTraceState trace(QUrl(QStringLiteral("https://example.invalid/path?query=marker")),
                          QCNetworkDiagnosticQueryPolicy::Full);
    trace.beginNetworkAttempt();
    const auto unknown = splitTraceCallback("[0-0] secret\n",
                                            TraceSource::Unknown,
                                            0,
                                            TraceIds{0, 0});
    const auto log     = trace.consume(CURLINFO_HEADER_OUT, unknown);
    QVERIFY(!log.contains(QStringLiteral("secret")));
    QVERIFY(!log.contains(QStringLiteral("[0-0]")));
    QVERIFY(!TraceTest::consume(trace, CURLINFO_HEADER_OUT, "GET /path?query=marker HTTP/2\r\n\r\n")
                 .contains(QStringLiteral("query=marker")));
    trace.beginNetworkAttempt();
    static_cast<void>(
        TraceTest::consume(trace, CURLINFO_TEXT, "Server auth using Basic with user 'secret'\n"));
    static_cast<void>(trace.consume(CURLINFO_HEADER_IN, unknown));
    QVERIFY(!TraceTest::consume(trace, CURLINFO_TEXT, "bare-secret\n")
                 .contains(QStringLiteral("bare-secret")));
}

void TestQCNetworkLogger::testLibIdsProductionCallbacks()
{
    QCNetworkRequest request(QUrl(QStringLiteral("http://example.invalid/path?query=marker")));
    request.setDiagnosticQueryPolicy(QCNetworkDiagnosticQueryPolicy::Full);
    QCNetworkReply observer(QCNetworkReply::TestOnlyKey{}, request, HttpMethod::Get);
    QCNetworkReplyPrivate snapshot(&observer,
                                   request,
                                   HttpMethod::Get,
                                   Internal::makeEmptyRequestBody(),
                                   {});
    QCNetworkDefaultLogger *capture = nullptr;
    const auto logger               = QCNetworkLoggerHandle::createWithBorrow(&capture);
    capture->enableConsoleOutput(false);
    capture->setMinLogLevel(NetworkLogLevel::Debug);
    snapshot.logger            = logger;
    snapshot.debugTraceEnabled = true;
    QCCurlMultiTransferRecord record{QCCurlHandleManager{}};
    record.bindObserver(snapshot.transferState, &observer);
    for (bool multi : {false, true}) {
        auto *handle = multi ? record.handle() : snapshot.curlManager.handle();
        QVERIFY(handle);
        curl_off_t transfer   = 0;
        curl_off_t connection = 0;
        QCOMPARE(curl_easy_getinfo(handle, CURLINFO_XFER_ID, &transfer), CURLE_OK);
        QCOMPARE(curl_easy_getinfo(handle, CURLINFO_CONN_ID, &connection), CURLE_OK);
        QCOMPARE(transfer, -1);
        QCOMPARE(connection, -1);
        snapshot.transferState->trace.beginNetworkAttempt();
        QByteArray raw("[x-x] X-Auth-Token: secret\r\nSet-Cookie: secret\r\n");
        const auto call = [&](CURL *source, curl_infotype type, QByteArray &bytes) {
            return multi
                       ? QCCurlMultiTransferRecord::debugCallback(source,
                                                                  type,
                                                                  bytes.data(),
                                                                  static_cast<size_t>(bytes.size()),
                                                                  &record)
                       : QCNetworkReplyPrivate::curlDebugCallback(source,
                                                                  type,
                                                                  bytes.data(),
                                                                  static_cast<size_t>(bytes.size()),
                                                                  &snapshot);
        };
        QCOMPARE(call(handle, CURLINFO_HEADER_IN, raw), 0);
        QCOMPARE(capture->entries().last().message(),
                 QStringLiteral("HEADER_IN: [x-x] X-Auth-Token: [REDACTED]\n"
                                "HEADER_IN: [x-x] Set-Cookie: [REDACTED]"));
        raw = "[x-x] GET /path?query=marker HTTP/1.1\r\n\r\n";
        QCOMPARE(call(handle, CURLINFO_HEADER_OUT, raw), 0);
        QVERIFY(capture->entries().last().message().contains(QStringLiteral("query=marker")));
        raw = "[0-0] X-Auth-Token: secret\r\n";
        QCOMPARE(call(handle, CURLINFO_HEADER_IN, raw), 0);
        QVERIFY(!capture->entries().last().message().contains(QStringLiteral("[0-0]")));
        QVERIFY(!capture->entries().last().message().contains(QStringLiteral("secret")));
    }
}
