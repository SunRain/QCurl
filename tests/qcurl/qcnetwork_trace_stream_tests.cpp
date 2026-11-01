// SPDX-License-Identifier: MIT
// Copyright (c) 2025 QCurl Project

#include "QCNetworkAccessManager.h"
#include "QCNetworkDefaultLogger.h"
#include "QCNetworkLogger.h"
#include "QCNetworkMockHandler.h"
#include "QCNetworkReply.h"
#include "QCNetworkReply_p.h"
#include "QCNetworkRequest.h"
#include "QCNetworkTestSupport.h"
#include "private/QCCurlMultiTransferRecord_p.h"
#include "private/QCNetworkLogRedaction_p.h"
#include "private/QCNetworkReplyTrace_p.h"
#include "qcnetwork_logger_test.h"
#include "qcnetwork_trace_test_helpers.h"

#include <QCoreApplication>
#include <QDate>
#include <QDir>
#include <QEvent>
#include <QFile>
#include <QProcess>
#include <QProcessEnvironment>
#include <QSharedPointer>
#include <QTemporaryDir>
#include <QTime>
#include <QTimeZone>
#include <QUrl>
#include <QtTest>

#include <atomic>
#include <type_traits>
#include <utility>

using namespace QCurl;

/**
 * @brief 验证 logger Core contract、默认实现与 manager wiring。
 */

void TestQCNetworkLogger::testDebugTraceSignedUrlRedaction()
{
    const QByteArray raw = QByteArray(
        "GET /s3/object?X-Amz-Algorithm=AWS4-HMAC-SHA256&"
        "X-Amz-Credential=AKIAIOSFODNN7EXAMPLE&"
        "response-content-disposition=attachment HTTP/1.1\r\n"
        "GET /cf/video.mp4?Policy=cloudfront-policy&Signature=cf-signature&"
        "Key-Pair-Id=K123 HTTP/1.1\r\n"
        "GET /gcs/object?X-Goog-Algorithm=GOOG4-RSA-SHA256&alt=media&"
        "X-Goog-Signature=gcs-signature HTTP/1.1\r\n"
        "GET /azure/blob?sv=2024-11-04&se=2026-06-19T00%3A00%3A00Z&"
        "sp=r&sig=azure-signature&rsct=text/plain HTTP/1.1\r\n"
        "GET /normal/path?file=readme.txt&download=1&token=secret-token "
        "HTTP/1.1\r\n");

    Internal::ReplyTraceState trace;
    trace.beginNetworkAttempt();
    const QString message = TraceTest::consume(trace, CURLINFO_HEADER_OUT, raw);

    QVERIFY(!message.contains(QStringLiteral("AWS4-HMAC-SHA256")));
    QVERIFY(!message.contains(QStringLiteral("AKIAIOSFODNN7EXAMPLE")));
    QVERIFY(!message.contains(QStringLiteral("attachment")));
    QVERIFY(!message.contains(QStringLiteral("cloudfront-policy")));
    QVERIFY(!message.contains(QStringLiteral("cf-signature")));
    QVERIFY(!message.contains(QStringLiteral("K123")));
    QVERIFY(!message.contains(QStringLiteral("GOOG4-RSA-SHA256")));
    QVERIFY(!message.contains(QStringLiteral("media")));
    QVERIFY(!message.contains(QStringLiteral("gcs-signature")));
    QVERIFY(!message.contains(QStringLiteral("2024-11-04")));
    QVERIFY(!message.contains(QStringLiteral("azure-signature")));
    QVERIFY(!message.contains(QStringLiteral("text/plain")));
    QVERIFY(!message.contains(QStringLiteral("secret-token")));

    for (const QString &path : {QStringLiteral("/s3/object"),
                                QStringLiteral("/cf/video.mp4"),
                                QStringLiteral("/gcs/object"),
                                QStringLiteral("/azure/blob"),
                                QStringLiteral("/normal/path")}) {
        QVERIFY(message.contains(QStringLiteral("GET %1?[REDACTED] HTTP/1.1").arg(path)));
    }
    QVERIFY(!message.contains(QStringLiteral("file=readme.txt")));
    QVERIFY(!message.contains(QStringLiteral("download=1")));
}

void TestQCNetworkLogger::testTraceInitialTargetAndConnect()
{
    Internal::ReplyTraceState trace(QUrl(QStringLiteral("https://example.invalid/path?page=2")),
                                    QCNetworkDiagnosticQueryPolicy::Full);
    trace.beginNetworkAttempt();
    const auto format = [&trace](const QByteArray &raw) {
        return TraceTest::consume(trace, CURLINFO_HEADER_OUT, raw);
    };
    QVERIFY(format(QByteArrayLiteral("CONNECT example.invalid:443 HTTP/1.1\r\n"
                                     "Proxy-Authorization: Basic proxy-secret\r\n\r\n"))
                .contains(QStringLiteral("Proxy-Authorization: [REDACTED]")));
    format(QByteArrayLiteral("CONNECT example.invalid:443 HTTP/1.1\r\n\r\n"));
    QVERIFY(format(QByteArrayLiteral("GET /path?page=2 HT")).isEmpty());
    QVERIFY(format(QByteArrayLiteral("TP/2\r")).isEmpty());
    const QString initial = format(QByteArrayLiteral("\nAuthorization: Bearer secret\r\n\r\n"));
    QVERIFY(initial.contains(QStringLiteral("GET /path?page=2 HTTP/2")));
    QVERIFY(initial.contains(QStringLiteral("Authorization: [REDACTED]")));
    QVERIFY(!format(QByteArrayLiteral("GET /path?page=2 HTTP/2\r\n\r\n"))
                 .contains(QStringLiteral("page=2")));
    QVERIFY(!format(QByteArrayLiteral("GET /redirect?redirect-secret HTTP/2\r\n\r\n"))
                 .contains(QStringLiteral("redirect-secret")));
    trace.beginNetworkAttempt();
    QVERIFY(format(QByteArrayLiteral("GET https://example.invalid/path?page=2 HTTP/1.1\r\n\r\n"))
                .contains(QStringLiteral("page=2")));
    trace.beginNetworkAttempt();
    format(QByteArrayLiteral("NOT-A-TARGET\r\n\r\n"));
    QVERIFY(!format(QByteArrayLiteral("GET /path?page=2 HTTP/1.1\r\n"))
                 .contains(QStringLiteral("page=2")));
    trace.beginNetworkAttempt();
    format(QByteArrayLiteral("GET /path HTTP/1.1\r\n\r\n"));
    QVERIFY(!format(QByteArrayLiteral("GET /path?page=2 HTTP/1.1\r\n"))
                 .contains(QStringLiteral("page=2")));
}

void TestQCNetworkLogger::testTraceSensitiveHeaderChunks()
{
    const QByteArray header = QByteArrayLiteral("Authorization: Bearer ")
                              + QByteArray(66138 - 22, 'x') + QByteArrayLiteral("\r\n");
    QCOMPARE(header.size(), 66140);
    Internal::ReplyTraceState trace;
    trace.beginNetworkAttempt();
    const QString first  = TraceTest::consume(trace, CURLINFO_HEADER_IN, header.first(65536));
    const QString second = TraceTest::consume(trace, CURLINFO_HEADER_IN, header.sliced(65536));
    QVERIFY(!first.contains(QString(20, QLatin1Char('x'))));
    QVERIFY(!second.contains(QString(20, QLatin1Char('x'))));
    for (const auto &name : {QByteArrayLiteral("Authorization"),
                             QByteArrayLiteral("Cookie"),
                             QByteArrayLiteral("Proxy-Authorization"),
                             QByteArrayLiteral("Set-Cookie"),
                             QByteArrayLiteral("X-Custom-Token")}) {
        const QByteArray line = name + QByteArrayLiteral(": synthetic-secret\r\n");
        for (qsizetype split = 0; split <= line.size(); ++split) {
            trace.beginNetworkAttempt();
            const QString firstPart  = TraceTest::consume(trace,
                                                          CURLINFO_HEADER_IN,
                                                          line.first(split));
            const QString secondPart = TraceTest::consume(trace,
                                                          CURLINFO_HEADER_IN,
                                                          line.sliced(split));
            const QString log        = firstPart + secondPart;
            QVERIFY(!log.contains(QStringLiteral("synthetic-secret")));
            QVERIFY(log.contains(QStringLiteral("[REDACTED]")));
        }
    }
    trace.beginNetworkAttempt();
    const QString folded
        = TraceTest::consume(trace,
                             CURLINFO_HEADER_IN,
                             QByteArrayLiteral(
                                 "Authorization: first-secret\r\n second-secret\r\n"));
    QVERIFY(!folded.contains(QStringLiteral("first-secret")));
    QVERIFY(!folded.contains(QStringLiteral("second-secret")));
    trace.beginNetworkAttempt();
    const QString foldedLocation
        = TraceTest::consume(trace,
                             CURLINFO_HEADER_IN,
                             QByteArrayLiteral(
                                 "Location: /redirect?query-secret\r\n folded-query-secret\r\n"));
    QVERIFY2(!foldedLocation.contains(QStringLiteral("query-secret")), qPrintable(foldedLocation));
}

void TestQCNetworkLogger::testTraceBudgetAndTermination()
{
    for (qsizetype size : {4095, 4096, 4097}) {
        Internal::ReplyTraceState trace;
        trace.beginNetworkAttempt();
        QByteArray raw(size, 'a');
        raw.append('\n');
        const QString log = TraceTest::consume(trace, CURLINFO_TEXT, raw);
        QVERIFY(log.size() <= 4096);
        QCOMPARE(log.contains(QStringLiteral("discarded")), size > 4096);
    }
    Internal::ReplyTraceState trace(QUrl(QStringLiteral("https://example.invalid/path?page=2")),
                                    QCNetworkDiagnosticQueryPolicy::Full);
    trace.beginNetworkAttempt();
    QVERIFY(TraceTest::consume(trace, CURLINFO_HEADER_IN, QByteArrayLiteral("Authoriz")).isEmpty());
    const QString incomingText = TraceTest::consume(trace,
                                                    CURLINFO_TEXT,
                                                    QByteArrayLiteral("independent text\n"));
    QCOMPARE(incomingText, QStringLiteral("TEXT: independent text"));
    QVERIFY(!TraceTest::consume(trace, CURLINFO_HEADER_IN, QByteArrayLiteral("ation: secret\r\n"))
                 .contains(QStringLiteral("secret")));
    const QByteArray utf8 = QStringLiteral("中文信息\r\n").toUtf8();
    QVERIFY(TraceTest::consume(trace, CURLINFO_TEXT, utf8.first(1)).isEmpty());
    QCOMPARE(TraceTest::consume(trace, CURLINFO_TEXT, utf8.sliced(1)),
             QStringLiteral("TEXT: 中文信息"));
    QVERIFY(TraceTest::consume(trace, CURLINFO_HEADER_OUT, QByteArray(4097, 'a'))
                .contains(QStringLiteral("discarded")));
    QVERIFY(TraceTest::consume(trace, CURLINFO_HEADER_OUT, QByteArrayLiteral("\n\r\n")).isEmpty());
    QVERIFY(!TraceTest::consume(trace,
                                CURLINFO_HEADER_OUT,
                                QByteArrayLiteral("GET /path?page=2 HTTP/1.1\r\n"))
                 .contains(QStringLiteral("page=2")));
    QVERIFY(TraceTest::consume(trace, CURLINFO_TEXT, QByteArrayLiteral("secret-tail")).isEmpty());
    trace.stop();
    QVERIFY(TraceTest::consume(trace, CURLINFO_TEXT, QByteArrayLiteral("-tail\n")).isEmpty());
    trace.beginNetworkAttempt();
    QCOMPARE(TraceTest::consume(trace, CURLINFO_TEXT, QByteArrayLiteral("new text\n")),
             QStringLiteral("TEXT: new text"));
    QVERIFY(TraceTest::consume(trace,
                               CURLINFO_HEADER_OUT,
                               QByteArrayLiteral("GET /path?page=2 HTTP/1.1\r\n\r\n"))
                .contains(QStringLiteral("page=2")));
}

void TestQCNetworkLogger::testTraceTextCredentialsAndFragments()
{
    Internal::ReplyTraceState trace({}, QCNetworkDiagnosticQueryPolicy::Full);
    trace.beginNetworkAttempt();
    for (const auto &name : {QByteArrayLiteral("authorization"),
                             QByteArrayLiteral("cookie"),
                             QByteArrayLiteral("proxy-authorization"),
                             QByteArrayLiteral("x-token")}) {
        const auto raw    = QByteArrayLiteral("[HTTP/2] [1] [") + name
                            + QByteArrayLiteral(": bracket-secret]\n");
        const QString log = TraceTest::consume(trace, CURLINFO_TEXT, raw);
        QVERIFY(!log.contains(QStringLiteral("bracket-secret")));
        QVERIFY(log.endsWith(QStringLiteral("[REDACTED]]")));
    }
    const QString text = TraceTest::consume(
        trace,
        CURLINFO_TEXT,
        QByteArrayLiteral("Issue another request: "
                          "https://user:pass@example.invalid/path?q=query-secret#fragment-secret\n"
                          "URL https://user:pass@example.invalid/path#fragment-only-secret\n"
                          "ordinary issue #123\n"));
    for (const auto &secret : {QStringLiteral("user:pass"),
                               QStringLiteral("query-secret"),
                               QStringLiteral("fragment-secret"),
                               QStringLiteral("fragment-only-secret")}) {
        QVERIFY(!text.contains(secret));
    }
    QVERIFY(text.contains(QStringLiteral("ordinary issue #123")));
    const QString location
        = TraceTest::consume(trace,
                             CURLINFO_HEADER_IN,
                             QByteArrayLiteral("Location: redirect#relative-fragment-secret\r\n"));
    QVERIFY(!location.contains(QStringLiteral("relative-fragment-secret")));
    for (const auto policy :
         {QCNetworkDiagnosticQueryPolicy::Redacted, QCNetworkDiagnosticQueryPolicy::Full}) {
        const QUrl url(
            QStringLiteral("https://user:pass@example.invalid/path?visible=%2B#fragment-secret"));
        const QString safe = QCNetworkLogRedaction::redactUrl(url, policy);
        QVERIFY(!safe.contains(QStringLiteral("user:pass")));
        QVERIFY(!safe.contains(QStringLiteral("fragment-secret")));
        QCOMPARE(safe.contains(QStringLiteral("visible=%2B")),
                 policy == QCNetworkDiagnosticQueryPolicy::Full);
        const QUrl fragment(QStringLiteral("https://example.invalid/path#fragment-only-secret"));
        QCOMPARE(QCNetworkLogRedaction::redactUrl(fragment, policy),
                 QStringLiteral("https://example.invalid/path"));
    }
}
