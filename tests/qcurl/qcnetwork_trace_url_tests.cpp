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
void addAmbiguousQuotedUrlRows()
{
    const QString discarded = QStringLiteral("[REDACTED: ambiguous URL record]");
    QTest::newRow("missing-closing-quote")
        << QByteArrayLiteral("Issue another request to this URL: "
                             "'http://example.invalid/next#fragment-marker\r\n")
        << discarded;
    QTest::newRow("unexpected-record-suffix")
        << QByteArrayLiteral("Issue another request to this URL: "
                             "'http://example.invalid/next#prefix' fragment-marker\r\n")
        << discarded;
    QTest::newRow("invalid-quoted-fragment-space")
        << QByteArrayLiteral("Issue another request to this URL: "
                             "'http://example.invalid/next#prefix fragment-marker'\r\n")
        << discarded;
    QTest::newRow("invalid-quoted-fragment-delimiter")
        << QByteArrayLiteral("Issue another request to this URL: "
                             "'http://example.invalid/next#prefix<fragment-marker'\r\n")
        << discarded;
    QTest::newRow("unquoted-fragment-ambiguous-tail")
        << QByteArrayLiteral("URL http://example.invalid/next#prefix fragment-marker\r\n")
        << discarded;
}
} // namespace

/**
 * @brief 验证 logger Core contract、默认实现与 manager wiring。
 */

void TestQCNetworkLogger::testTraceQuotedUrlRecordChunks_data()
{
    QTest::addColumn<QByteArray>("record");
    QTest::addColumn<QString>("expected");
    const QString redirectPrefix = QStringLiteral("Issue another request to this URL: '");
    const QString discarded      = QStringLiteral("[REDACTED: ambiguous URL record]");
    QTest::newRow("quoted-fragment")
        << QByteArrayLiteral("Issue another request to this URL: "
                             "'http://example.invalid/next#prefix'fragment-marker'\r\n")
        << redirectPrefix + QStringLiteral("http://example.invalid/next'");
    QTest::newRow("bracketed-quoted-fragment")
        << QByteArrayLiteral("[HTTP] Issue another request to this URL: "
                             "'http://example.invalid/next#prefix'fragment-marker'\r\n")
        << redirectPrefix + QStringLiteral("http://example.invalid/next'");
    QTest::newRow("quoted-userinfo")
        << QByteArrayLiteral("Issue another request to this URL: "
                             "'http://user:prefix'credential-marker@example.invalid/next'\r\n")
        << redirectPrefix + QStringLiteral("http://example.invalid/next'");
    QTest::newRow("quoted-query-userinfo-fragment")
        << QByteArrayLiteral("Issue another request to this URL: "
                             "'http://user:prefix'credential-marker@example.invalid/next"
                             "?route=query-marker#prefix'fragment-marker'\r\n")
        << redirectPrefix + QStringLiteral("http://example.invalid/next?[REDACTED]'");
    QTest::newRow("quote-in-path-is-not-a-record-boundary")
        << QByteArrayLiteral("Issue another request to this URL: "
                             "'http://example.invalid/next'path#prefix'fragment-marker'\r\n")
        << redirectPrefix + QStringLiteral("http://example.invalid/next'path'");
    QTest::newRow("unquoted-fragment")
        << QByteArrayLiteral("URL http://example.invalid/next#prefix'fragment-marker\r\n")
        << QStringLiteral("URL http://example.invalid/next");
    QTest::newRow("unquoted-userinfo")
        << QByteArrayLiteral("URL http://user:prefix'credential-marker@example.invalid/next\r\n")
        << QStringLiteral("URL http://example.invalid/next");
    QTest::newRow("unknown-quoted-url-wrapper")
        << QByteArrayLiteral("URL 'http://user:prefix'credential-marker@example.invalid/next'\r\n")
        << discarded;
    QTest::newRow("bracketed-relative-target")
        << QByteArrayLiteral("[HTTP/2] [1] [:path: /next#prefix'fragment-marker]\r\n")
        << QStringLiteral("[HTTP/2] [1] [:path: /next]");
    addAmbiguousQuotedUrlRows();
    QTest::newRow("ordinary-text-hash")
        << QByteArrayLiteral("ordinary issue #123\r\n") << QStringLiteral("ordinary issue #123");
}

void TestQCNetworkLogger::testTraceQuotedUrlRecordChunks()
{
    QFETCH(QByteArray, record);
    QFETCH(QString, expected);
    for (const auto policy :
         {QCNetworkDiagnosticQueryPolicy::Redacted, QCNetworkDiagnosticQueryPolicy::Full}) {
        // 穷举分块位置，包括引号、CR/LF 和秘密中间；未完成记录不能提前输出。
        for (qsizetype split = 0; split <= record.size(); ++split) {
            Internal::ReplyTraceState trace({}, policy);
            trace.beginNetworkAttempt();
            const QString first  = TraceTest::consume(trace, CURLINFO_TEXT, record.first(split));
            const QString second = TraceTest::consume(trace, CURLINFO_TEXT, record.sliced(split));
            const QString log    = first + second;
            if (split < record.size()) {
                QVERIFY(first.isEmpty());
            }
            QCOMPARE(log, QStringLiteral("TEXT: ") + expected);
            for (const auto &marker : {QStringLiteral("credential-marker"),
                                       QStringLiteral("fragment-marker"),
                                       QStringLiteral("query-marker")}) {
                QVERIFY2(!log.contains(marker), qPrintable(log));
            }
            QCOMPARE(TraceTest::consume(trace,
                                        CURLINFO_TEXT,
                                        QByteArrayLiteral("ordinary issue #123\r\n")),
                     QStringLiteral("TEXT: ordinary issue #123"));
        }
    }
}

void TestQCNetworkLogger::testRedactedUrlTextPreservesLineBoundaries()
{
    const QString raw = QStringLiteral(
        "ordinary issue #123\r\n"
        "Issue another request to this URL: "
        "'http://example.invalid/next#prefix'fragment-marker'\r\n"
        "URL 'http://user:prefix'credential-marker@example.invalid/next'\n"
        "ordinary issue #456");
    const QString expected = QStringLiteral(
        "ordinary issue #123\r\n"
        "Issue another request to this URL: 'http://example.invalid/next'\r\n"
        "[REDACTED: ambiguous URL record]\n"
        "ordinary issue #456");
    // errorString 也可包含多行；一个畸形 URL 不能吞掉后续安全行或把其原文拼回。
    QCOMPARE(QCNetworkLogRedaction::redactSensitiveQueryParams(raw), expected);
}

void TestQCNetworkLogger::testTraceAmbiguousHeadersFailClosed()
{
    Internal::ReplyTraceState trace({}, QCNetworkDiagnosticQueryPolicy::Full);
    trace.beginNetworkAttempt();
    for (const auto &raw : {
             QByteArrayLiteral("[HTTP/2] [1] [authorization: header-secret]\n folded-secret\n"),
             QByteArrayLiteral("[HTTP/2] [1] [ authorization : header-secret]\n"),
             QByteArrayLiteral("[HTTP/2] [1] [x-public: value] [cookie: header-secret]\n"),
             QByteArrayLiteral("[HTTP/2] [1] [authorization header-secret]\n"),
             QByteArrayLiteral("Authorization: header-secret [x-public: public-value]\n"),
             QByteArrayLiteral("Cookie: header-secret [x-public: public-value]\n"),
             QByteArrayLiteral("Authorization header-secret\n"),
             QByteArrayLiteral("Cookie\t header-secret\n folded-secret\n"),
             QByteArrayLiteral("URL https://user:password-secret@example.invalid/#fragment-secret "
                               "[authorization: header-secret]\n"),
         }) {
        trace.beginNetworkAttempt();
        const QString log = TraceTest::consume(trace, CURLINFO_TEXT, raw);
        QVERIFY(!log.isEmpty());
        for (const auto &secret : {QStringLiteral("header-secret"),
                                   QStringLiteral("folded-secret"),
                                   QStringLiteral("password-secret"),
                                   QStringLiteral("fragment-secret")}) {
            QVERIFY2(!log.contains(secret), qPrintable(log));
        }
        QVERIFY(log.contains(QStringLiteral("[REDACTED")));
    }
    trace.beginNetworkAttempt();
    QCOMPARE(TraceTest::consume(trace,
                                CURLINFO_TEXT,
                                QByteArrayLiteral(
                                    "[HTTP/2] [1] [:path: /resource?query-secret]\n")),
             QStringLiteral("TEXT: [HTTP/2] [1] [:path: /resource?[REDACTED]]"));
}

void TestQCNetworkLogger::testTraceBlockPhaseAndStoppedStream()
{
    Internal::ReplyTraceState trace(QUrl(QStringLiteral("https://example.invalid/path?page=2")),
                                    QCNetworkDiagnosticQueryPolicy::Full);
    trace.beginNetworkAttempt();
    const QString misplacedTarget
        = TraceTest::consume(trace,
                             CURLINFO_HEADER_OUT,
                             QByteArrayLiteral("CONNECT example.invalid:443 HTTP/1.1\r\n"
                                               "GET /path?page=2 HTTP/1.1\r\n\r\n"));
    QVERIFY(!misplacedTarget.contains(QStringLiteral("page=2")));

    QVERIFY(TraceTest::consume(trace, CURLINFO_HEADER_IN, QByteArrayLiteral("Authorization: secret"))
                .isEmpty());
    trace.stop();
    QVERIFY(TraceTest::consume(trace, CURLINFO_HEADER_IN, QByteArrayLiteral("-tail\r\n")).isEmpty());
    QVERIFY(TraceTest::consume(trace, CURLINFO_TEXT, QByteArrayLiteral("late-secret\n")).isEmpty());
    QVERIFY(
        TraceTest::consume(trace, CURLINFO_DATA_IN, QByteArrayLiteral("late-payload")).isEmpty());
    trace.beginNetworkAttempt();
    QVERIFY(TraceTest::consume(trace,
                               CURLINFO_HEADER_OUT,
                               QByteArrayLiteral("GET /path?page=2 HTTP/1.1\r\n\r\n"))
                .contains(QStringLiteral("page=2")));
}
