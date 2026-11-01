#include "qcnetwork_logger_test.h"
#include "qcnetwork_trace_test_helpers.h"

#include <QtTest>

#include <limits>

using namespace QCurl;
using namespace QCurl::Internal;

void TestQCNetworkLogger::testLibIdsStrictDecoration()
{
    for (const TraceIds ids : {TraceIds{0, 0},
                               {0, -1},
                               {-1, 0},
                               {-1, -1},
                               {42, 3},
                               {std::numeric_limits<curl_off_t>::max(), 0}}) {
        const auto field = [](curl_off_t value) {
            return value == -1 ? QByteArrayLiteral("x") : QByteArray::number(value);
        };
        const auto prefix   = '[' + field(ids.transfer) + '-' + field(ids.connection) + "] ";
        const auto raw      = prefix + "ordinary issue #123\n";
        const auto callback = splitTraceCallback(raw, TraceSource::Caller, 1, ids);
        QVERIFY(!callback.context.ambiguous);
        QVERIFY(callback.context.ids.has_value());
        QVERIFY(*callback.context.ids == ids);
        QCOMPARE(callback.body, QByteArrayView("ordinary issue #123\n"));
    }
}

void TestQCNetworkLogger::testLibIdsRejectedDecoration()
{
    for (const auto &raw : {QByteArrayLiteral("[0-0] secret\n"),
                            QByteArrayLiteral("[x-0] secret\n"),
                            QByteArrayLiteral("[+0-0] secret\n"),
                            QByteArrayLiteral("[-1-0] secret\n"),
                            QByteArrayLiteral("[X-x] secret\n"),
                            QByteArrayLiteral("[-] secret\n"),
                            QByteArrayLiteral("[0-] secret\n"),
                            QByteArrayLiteral("[ -0] secret\n"),
                            QByteArrayLiteral("[0-0]secret\n"),
                            QByteArrayLiteral("[0-"),
                            QByteArrayLiteral("[[0-0]] secret\n"),
                            QByteArrayLiteral("[9223372036854775808-0] secret\n")}) {
        const auto callback = splitTraceCallback(raw, TraceSource::Caller, 1, TraceIds{9, 0});
        QVERIFY2(callback.context.ambiguous, raw.constData());
        QVERIFY(!callback.context.ids);
        QVERIFY(callback.body.isEmpty());
    }
    const auto getterFailure = splitTraceCallback("[0-0] secret\n", TraceSource::Caller, 1, {});
    QVERIFY(getterFailure.context.ambiguous);
    QVERIFY(!getterFailure.context.ids);
    const auto nested = splitTraceCallback("[0-0] [0-0] secret\n",
                                           TraceSource::Caller,
                                           1,
                                           TraceIds{0, 0});
    ReplyTraceState nestedTrace;
    nestedTrace.beginNetworkAttempt();
    const auto nestedLog = nestedTrace.consume(CURLINFO_TEXT, nested);
    QVERIFY(nestedLog.contains(QStringLiteral("[0-0] [REDACTED:")));
    QVERIFY(!nestedLog.contains(QStringLiteral("secret")));
    QVERIFY(nested.context.ids.has_value());
    const auto ordinary = splitTraceCallback("[HTTP/2] [1] [x-public: safe]\n",
                                             TraceSource::Caller,
                                             1,
                                             {});
    QVERIFY(!ordinary.context.ambiguous);
    QVERIFY(!ordinary.context.ids);
}

void TestQCNetworkLogger::testLibIdsLineAssociation()
{
    ReplyTraceState trace;
    trace.beginNetworkAttempt();
    const QString multiline
        = TraceTest::decorated(trace,
                               CURLINFO_HEADER_IN,
                               "[12-3] X-Public: safe\r\nSet-Cookie: secret\r\n folded-secret\r\n",
                               {12, 3});
    QCOMPARE(multiline,
             QStringLiteral("HEADER_IN: [12-3] X-Public: safe\n"
                            "HEADER_IN: [12-3] Set-Cookie: [REDACTED]\n"
                            "HEADER_IN: [12-3] [REDACTED]"));
    QVERIFY(TraceTest::decorated(trace, CURLINFO_TEXT, "[12-3] ordinary ", {12, 3}).isEmpty());
    QCOMPARE(TraceTest::consume(trace, CURLINFO_TEXT, "中文\r\nnew line\n"),
             QStringLiteral("TEXT: [12-3] ordinary 中文\nTEXT: new line"));
    QVERIFY(TraceTest::consume(trace, CURLINFO_TEXT, "first ").isEmpty());
    QCOMPARE(TraceTest::decorated(trace, CURLINFO_TEXT, "[12-3] end\n", {12, 3}),
             QStringLiteral("TEXT: first end"));
    QVERIFY(TraceTest::decorated(trace, CURLINFO_TEXT, "[12-3] credential-", {12, 3}).isEmpty());
    const QString conflict = TraceTest::decorated(trace, CURLINFO_TEXT, "[13-3] secret\n", {13, 3});
    QVERIFY(!conflict.contains(QStringLiteral("secret")));
    QVERIFY(!conflict.contains(QStringLiteral("[12-3]")));
    QVERIFY(!conflict.contains(QStringLiteral("[13-3]")));
    trace.beginNetworkAttempt();
    const QByteArray body = QStringLiteral("[HTTP/2] [1] [x-token: 中文-secret]\r\n").toUtf8();
    for (qsizetype split = 0; split <= body.size(); ++split) {
        trace.beginNetworkAttempt();
        const QByteArray first  = "[12-3] " + body.first(split);
        const QByteArray second = "[12-3] " + body.sliced(split);
        const QString firstLog  = TraceTest::decorated(trace, CURLINFO_TEXT, first, {12, 3});
        const QString secondLog = TraceTest::decorated(trace, CURLINFO_TEXT, second, {12, 3});
        const QString log       = firstLog + secondLog;
        QVERIFY2(log == QStringLiteral("TEXT: [12-3] [HTTP/2] [1] [x-token: [REDACTED]]"),
                 qPrintable(QStringLiteral("split=%1 %2").arg(split).arg(log)));
    }
}

void TestQCNetworkLogger::testLibIdsFragmentConflict()
{
    ReplyTraceState trace;
    trace.beginNetworkAttempt();
    QVERIFY(TraceTest::consume(trace, CURLINFO_TEXT, "first ").isEmpty());
    QVERIFY(TraceTest::decorated(trace, CURLINFO_TEXT, "[12-3] middle ", {12, 3}).isEmpty());
    const auto conflict = TraceTest::decorated(trace, CURLINFO_TEXT, "[13-3] secret\n", {13, 3});
    QCOMPARE(conflict, QStringLiteral("TEXT: [REDACTED: ambiguous trace callback]"));
    static_cast<void>(TraceTest::consume(trace, CURLINFO_DATA_IN, "boundary"));
    QVERIFY(!TraceTest::consume(trace, CURLINFO_TEXT, "secret-tail\n")
                 .contains(QStringLiteral("secret-tail")));
}

void TestQCNetworkLogger::testLibIdsTargetAndConnect()
{
    const QUrl url(QStringLiteral("https://example.invalid/a/../path?encoded=%2B"));
    ReplyTraceState trace(url, QCNetworkDiagnosticQueryPolicy::Full);
    trace.beginNetworkAttempt();
    for (int i = 0; i < 2; ++i) {
        const auto connect = TraceTest::decorated(trace,
                                                  CURLINFO_HEADER_OUT,
                                                  "[4-2] CONNECT example.invalid:443 HTTP/1.1\r\n"
                                                  "Proxy-Authorization: secret\r\n\r\n",
                                                  {4, 2});
        QVERIFY(!connect.contains(QStringLiteral("secret")));
        QVERIFY(connect.contains(QStringLiteral("[4-2] Proxy-Authorization: [REDACTED]")));
    }
    const auto target = QByteArrayLiteral("[4-2] GET /path?encoded=%2B HTTP/2\r\n\r\n");
    QVERIFY(TraceTest::decorated(trace, CURLINFO_HEADER_OUT, target, {4, 2})
                .contains(QStringLiteral("[4-2] GET /path?encoded=%2B HTTP/2")));
    QVERIFY(!TraceTest::decorated(trace, CURLINFO_HEADER_OUT, target, {4, 2})
                 .contains(QStringLiteral("encoded=%2B")));
    trace.beginNetworkAttempt();
    QVERIFY(TraceTest::decorated(trace, CURLINFO_HEADER_OUT, target, {4, 2})
                .contains(QStringLiteral("encoded=%2B")));
}

void TestQCNetworkLogger::testLibIdsBudgetsAndTruncation()
{
    ReplyTraceState trace;
    for (qsizetype size : {4095, 4096, 4097}) {
        trace.beginNetworkAttempt();
        const QByteArray raw = "[1-0] " + QByteArray(size, 'a') + '\n';
        const auto log       = TraceTest::decorated(trace, CURLINFO_TEXT, raw, {1, 0});
        QVERIFY(log.startsWith(QStringLiteral("TEXT: [1-0] ")));
        QVERIFY(log.size() <= 4096);
        QCOMPARE(log.contains(QStringLiteral("discarded")), size > 4096);
    }
    trace.beginNetworkAttempt();
    const QString log = TraceTest::decorated(trace,
                                             CURLINFO_TEXT,
                                             "[1-0] ordinary first\nAuthorization: prefix...\n",
                                             {1, 0});
    QVERIFY(log.contains(QStringLiteral("[1-0] ordinary first")));
    QVERIFY(log.contains(QStringLiteral("[1-0] [REDACTED: trace line discarded]")));
    QVERIFY(!TraceTest::decorated(trace, CURLINFO_TEXT, "[1-0] bare-secret\n", {1, 0})
                 .contains(QStringLiteral("bare-secret")));
    static_cast<void>(TraceTest::consume(trace, CURLINFO_DATA_IN, "boundary"));
    QCOMPARE(TraceTest::consume(trace, CURLINFO_TEXT, "ordinary after boundary\n"),
             QStringLiteral("TEXT: ordinary after boundary"));
    trace.beginNetworkAttempt();
    QVERIFY(TraceTest::decorated(trace, CURLINFO_TEXT, "[1-0] prefix ", {1, 0}).isEmpty());
    QCOMPARE(TraceTest::decorated(trace, CURLINFO_TEXT, "tail...\n", {1, 0}),
             QStringLiteral("TEXT: [1-0] [REDACTED: trace line discarded]"));
    trace.beginNetworkAttempt();
    const QByteArray markerBoundary = "[1-0] " + QByteArray(4015, 'a') + '\n'
                                      + QByteArray(4097, 'b') + '\n';
    const QString boundaryLog = TraceTest::decorated(trace, CURLINFO_TEXT, markerBoundary, {1, 0});
    QVERIFY(boundaryLog.size() <= 4096);
    QVERIFY(boundaryLog.endsWith(QStringLiteral("[1-0] [REDACTED: trace line discarded]")));
    trace.stop();
    QVERIFY(TraceTest::consume(trace, CURLINFO_TEXT, "tail\n").isEmpty());
    trace.beginNetworkAttempt();
    QCOMPARE(TraceTest::consume(trace, CURLINFO_TEXT, "ordinary\n"),
             QStringLiteral("TEXT: ordinary"));
}

void TestQCNetworkLogger::testLibIdsAggregateBudget()
{
    ReplyTraceState trace;
    trace.beginNetworkAttempt();
    // 填满三个 caller 通道后，internal 不得获得第二份原始缓存预算。
    for (auto type : {CURLINFO_TEXT, CURLINFO_HEADER_IN, CURLINFO_HEADER_OUT}) {
        QVERIFY(TraceTest::consume(trace, type, QByteArray(4096, 'a')).isEmpty());
    }
    const QString overflow = TraceTest::decorated(trace,
                                                  CURLINFO_TEXT,
                                                  "[2-1] secret",
                                                  {2, 1},
                                                  TraceSource::Internal,
                                                  2);
    QVERIFY(overflow.contains(QStringLiteral("discarded")));
    QVERIFY(!overflow.contains(QStringLiteral("secret")));
    QVERIFY(
        !TraceTest::decorated(trace, CURLINFO_TEXT, "[2-1] -tail\n", {2, 1}, TraceSource::Internal, 2)
             .contains(QStringLiteral("tail")));
}
