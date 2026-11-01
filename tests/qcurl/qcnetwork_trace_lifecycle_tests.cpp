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

namespace {
void verifyCredentialTraceRecord(Internal::ReplyTraceState &trace, const QByteArray &record)
{
    for (qsizetype split = 0; split <= record.size(); ++split) {
        trace.beginNetworkAttempt();
        const QString first  = TraceTest::consume(trace, CURLINFO_TEXT, record.first(split));
        const QString second = TraceTest::consume(trace, CURLINFO_TEXT, record.sliced(split));
        const QString log    = first + second;
        QVERIFY(!log.contains(QStringLiteral("credential-marker")));
        QVERIFY(log.contains(QStringLiteral("[REDACTED")));
        QVERIFY(!TraceTest::consume(trace,
                                    CURLINFO_TEXT,
                                    QByteArrayLiteral(" folded-credential-marker\r\n"))
                     .contains(QStringLiteral("credential-marker")));
        QVERIFY(TraceTest::consume(trace, CURLINFO_TEXT, QByteArrayLiteral("credential-marker'\r\n"))
                    .contains(QStringLiteral("credential continuation")));
        static_cast<void>(
            TraceTest::consume(trace, CURLINFO_HEADER_IN, QByteArrayLiteral("HTTP/1.1 200 OK\r\n")));
        QCOMPARE(TraceTest::consume(trace,
                                    CURLINFO_TEXT,
                                    QByteArrayLiteral("ordinary issue #123\r\n")),
                 QStringLiteral("TEXT: ordinary issue #123"));
    }
}

void verifyCredentialTraceBounds(Internal::ReplyTraceState &trace)
{
    trace.beginNetworkAttempt();
    const QByteArray raw = QByteArrayLiteral("Server auth using Basic with user '")
                           + QByteArray(4097, 'x') + QByteArrayLiteral("\ncredential-marker'\r\n");
    const QString log    = TraceTest::consume(trace, CURLINFO_TEXT, raw);
    QVERIFY(log.contains(QStringLiteral("discarded")));
    QVERIFY(!log.contains(QStringLiteral("credential-marker")));
    QVERIFY(log.size() <= 4096);
    static_cast<void>(TraceTest::consume(trace,
                                         CURLINFO_HEADER_OUT,
                                         QByteArrayLiteral("GET /path HTTP/1.1\r\n\r\n")));
    QCOMPARE(TraceTest::consume(trace, CURLINFO_TEXT, QByteArrayLiteral("ordinary issue #123\r\n")),
             QStringLiteral("TEXT: ordinary issue #123"));
}

void verifyInterleavedCredentialTrace(Internal::ReplyTraceState &trace)
{
    trace.beginNetworkAttempt();
    static_cast<void>(
        TraceTest::consume(trace,
                           CURLINFO_TEXT,
                           QByteArrayLiteral(
                               "Server auth using Basic with user 'credential-marker\n")));
    QVERIFY(TraceTest::consume(trace, CURLINFO_TEXT, QByteArrayLiteral("credential-")).isEmpty());
    static_cast<void>(
        TraceTest::consume(trace, CURLINFO_HEADER_IN, QByteArrayLiteral("HTTP/1.1 200 OK\r\n")));
    QVERIFY(!TraceTest::consume(trace, CURLINFO_TEXT, QByteArrayLiteral("marker'\r\n"))
                 .contains(QStringLiteral("marker")));
    static_cast<void>(TraceTest::consume(trace, CURLINFO_DATA_IN, QByteArrayLiteral("body")));
    QCOMPARE(TraceTest::consume(trace, CURLINFO_TEXT, QByteArrayLiteral("ordinary issue #123\r\n")),
             QStringLiteral("TEXT: ordinary issue #123"));
}
void verifyUnavailableInitialTargets()
{
    for (bool hasQuery : {false, true}) {
        QUrl url(QStringLiteral("http://example.invalid/a/../path"));
        if (hasQuery) {
            url.setQuery(QStringLiteral(""));
        }
        Internal::ReplyTraceState trace(url, QCNetworkDiagnosticQueryPolicy::Full);
        trace.beginNetworkAttempt();
        const QString log = TraceTest::consume(trace,
                                               CURLINFO_HEADER_OUT,
                                               QByteArrayLiteral("GET /path? HTTP/1.1\r\n\r\n"));
        QCOMPARE(log.contains(QStringLiteral("[REDACTED]")), !hasQuery);
        QVERIFY(
            !TraceTest::consume(trace,
                                CURLINFO_HEADER_OUT,
                                QByteArrayLiteral("GET /path?later-query-marker HTTP/1.1\r\n\r\n"))
                 .contains(QStringLiteral("later-query-marker")));
    }
    Internal::ReplyTraceState invalid(QUrl(QStringLiteral("http://[invalid/path?query=marker")),
                                      QCNetworkDiagnosticQueryPolicy::Full);
    invalid.beginNetworkAttempt();
    const QString unavailable = TraceTest::consume(invalid,
                                                   CURLINFO_HEADER_OUT,
                                                   QByteArrayLiteral(
                                                       "GET /path?query=marker HTTP/1.1\r\n\r\n"));
    QVERIFY(unavailable.contains(QStringLiteral("initial target unavailable")));
    QVERIFY(!unavailable.contains(QStringLiteral("query=marker")));
    QVERIFY(!TraceTest::consume(invalid,
                                CURLINFO_HEADER_OUT,
                                QByteArrayLiteral("GET /path?query=marker HTTP/1.1\r\n\r\n"))
                 .contains(QStringLiteral("query=marker")));
}
} // namespace

/**
 * @brief 验证 logger Core contract、默认实现与 manager wiring。
 */

void TestQCNetworkLogger::testTraceCredentialRecordChunks()
{
    const QList<QByteArray> records = {
        QByteArrayLiteral("Added cookie session=\"credential-marker\" for domain example.invalid, "
                          "path /, expire 0\r\n"),
        QByteArrayLiteral("Replaced cookie session=\"credential-marker\" for domain "
                          "example.invalid, path /, expire 0\r\n"),
        QByteArrayLiteral("Server auth using Basic with user 'credential-marker'\r\n"),
        QByteArrayLiteral("Proxy auth using Digest with user 'credential-marker'\r\n"),
        QByteArrayLiteral("Added cookie session=credential-marker\r\n"),
        QByteArrayLiteral("Replaced cookie credential-marker\r\n"),
        QByteArrayLiteral("Server auth using Basic with user credential-marker\r\n"),
        QByteArrayLiteral("Proxy auth using credential-marker with user 'name'\r\n"),
        QByteArrayLiteral("[HTTP] Added cookie session=\"credential-marker\" for domain "
                          "example.invalid, path /, expire 0\r\n"),
        QByteArrayLiteral("[HTTP] Server auth using Basic with user 'credential-marker'\r\n"),
        QByteArrayLiteral("[HTTP] Proxy auth using credential-marker with user 'name'\r\n"),
        QByteArrayLiteral("\tadded cookie session=credential-marker\r\n"),
        QByteArrayLiteral("Server auth using Basic with user 'credential-marker\n"
                          "credential-marker\ncredential-marker'\r\n"),
        QByteArrayLiteral("Proxy auth using Basic with user 'credential-marker'\n"
                          "credential-marker'\r\n"),
    };
    for (const auto policy :
         {QCNetworkDiagnosticQueryPolicy::Redacted, QCNetworkDiagnosticQueryPolicy::Full}) {
        for (const auto &record : records) {
            Internal::ReplyTraceState trace({}, policy);
            verifyCredentialTraceRecord(trace, record);
        }
        Internal::ReplyTraceState oversized({}, policy);
        verifyCredentialTraceBounds(oversized);
        Internal::ReplyTraceState interleaved({}, policy);
        verifyInterleavedCredentialTrace(interleaved);
    }
}

void TestQCNetworkLogger::testTraceNormalizedTargetMatching()
{
    const QUrl initial(
        QStringLiteral("http://example.invalid:80/a/../path?encoded=%2B&query=marker"));
    for (const auto &target :
         {QByteArrayLiteral("/path?encoded=%2B&query=marker"),
          QByteArrayLiteral("http://example.invalid/path?encoded=%2B&query=marker")}) {
        Internal::ReplyTraceState trace(initial, QCNetworkDiagnosticQueryPolicy::Full);
        trace.beginNetworkAttempt();
        const QByteArray raw = QByteArrayLiteral("GET ") + target
                               + QByteArrayLiteral(" HTTP/1.1\r\n\r\n");
        const QString log    = TraceTest::consume(trace, CURLINFO_HEADER_OUT, raw);
        QVERIFY(log.contains(QStringLiteral("encoded=%2B&query=marker")));
        QVERIFY(!TraceTest::consume(trace, CURLINFO_HEADER_OUT, raw)
                     .contains(QStringLiteral("query=marker")));
    }
    for (const auto &target :
         {QByteArrayLiteral("/path?encoded=%2B&query=other-secret"),
          QByteArrayLiteral("/a/../path?encoded=%2B&query=marker"),
          QByteArrayLiteral("http://other.invalid/path?encoded=%2B&query=marker"),
          QByteArrayLiteral("/path?encoded=+&query=marker")}) {
        Internal::ReplyTraceState trace(initial, QCNetworkDiagnosticQueryPolicy::Full);
        trace.beginNetworkAttempt();
        const QByteArray raw = QByteArrayLiteral("GET ") + target
                               + QByteArrayLiteral(" HTTP/1.1\r\n\r\n");
        const QString log    = TraceTest::consume(trace, CURLINFO_HEADER_OUT, raw);
        QVERIFY(!log.contains(QStringLiteral("query=")));
        QVERIFY(!TraceTest::consume(trace,
                                    CURLINFO_HEADER_OUT,
                                    QByteArrayLiteral(
                                        "GET /path?encoded=%2B&query=marker HTTP/1.1\r\n\r\n"))
                     .contains(QStringLiteral("query=marker")));
    }
    verifyUnavailableInitialTargets();
}

void TestQCNetworkLogger::testTraceCallbacksShareRedactionAndStop()
{
    for (const auto policy :
         {QCNetworkDiagnosticQueryPolicy::Redacted, QCNetworkDiagnosticQueryPolicy::Full}) {
        QCNetworkRequest request(QUrl(QStringLiteral("http://example.invalid/path?query=marker")));
        request.setDiagnosticQueryPolicy(policy);
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
        QByteArray raw  = QByteArrayLiteral("Added cookie session=\"callback-credential-marker\" "
                                            "for domain example.invalid, path /, expire 0\r\n");
        QByteArray tail = QByteArrayLiteral(
            "Server auth using Basic with user 'unfinished-credential");
        for (bool multi : {false, true}) {
            snapshot.transferState->trace.beginNetworkAttempt();
            const auto callback = [&](QByteArray &bytes) {
                return multi
                           ? QCCurlMultiTransferRecord::debugCallback(record.handle(),
                                                                      CURLINFO_TEXT,
                                                                      bytes.data(),
                                                                      static_cast<size_t>(
                                                                          bytes.size()),
                                                                      &record)
                           : QCNetworkReplyPrivate::curlDebugCallback(snapshot.curlManager.handle(),
                                                                      CURLINFO_TEXT,
                                                                      bytes.data(),
                                                                      static_cast<size_t>(
                                                                          bytes.size()),
                                                                      &snapshot);
            };
            const qsizetype before = capture->entries().size();
            QCOMPARE(callback(raw), 0);
            QCOMPARE(capture->entries().size(), before + 1);
            QCOMPARE(capture->entries().last().message(),
                     QStringLiteral("TEXT: Added cookie [REDACTED]"));
            QCOMPARE(callback(tail), 0);
            QCOMPARE(capture->entries().size(), before + 1);
            record.clearObserver();
            QByteArray ending = QByteArrayLiteral("-tail'\r\n");
            QCOMPARE(callback(ending), 0);
            QCOMPARE(callback(raw), 0);
            QCOMPARE(capture->entries().size(), before + 1);
            record.bindObserver(snapshot.transferState, &observer);
        }
    }
}

void TestQCNetworkLogger::testLibIdsSensitiveHeaders()
{
    for (const auto policy :
         {QCNetworkDiagnosticQueryPolicy::Redacted, QCNetworkDiagnosticQueryPolicy::Full}) {
        Internal::ReplyTraceState trace({}, policy);
        trace.beginNetworkAttempt();
        const QString log = TraceTest::decorated(trace,
                                                 CURLINFO_HEADER_IN,
                                                 QByteArrayLiteral(
                                                     "[0-0] X-Auth-Token: marker-secret\r\n"
                                                     "Set-Cookie: marker-secret\r\n"),
                                                 {0, 0});
        QVERIFY2(!log.contains(QStringLiteral("marker-secret")), qPrintable(log));
        QVERIFY(log.contains(QStringLiteral("X-Auth-Token: [REDACTED]")));
        QVERIFY(log.contains(QStringLiteral("Set-Cookie: [REDACTED]")));
    }
}
