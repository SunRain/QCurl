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

void TestQCNetworkLogger::testLoggerNullptr()
{
    const auto logger = QCNetworkLoggerHandle::create<QCNetworkDefaultLogger>();
    m_manager->setLogger(logger);
    m_manager->setLogger({});
    QVERIFY(!m_manager->logger());
}

void TestQCNetworkLogger::testReplyRetainsLoggerSnapshotUntilDestroyed()
{
    QCNetworkMockHandler mock;
    const QUrl url(QStringLiteral("http://example.com/logger-snapshot"));
    mock.mockResponse(HttpMethod::Get, url, QByteArrayLiteral("ok"), 200);
    TestSupport::setMockHandler(m_manager, &mock);

    int destructionCount = 0;
    class DestructionTrackingLogger final : public QCNetworkDefaultLogger
    {
    public:
        explicit DestructionTrackingLogger(int *count)
            : m_count(count)
        {}

        ~DestructionTrackingLogger() override { ++(*m_count); }

    private:
        int *m_count = nullptr;
    };

    auto originalLogger = QCNetworkLoggerHandle::create<DestructionTrackingLogger>(
        &destructionCount);
    m_manager->setLogger(originalLogger);

    auto *reply = m_manager->get(QCNetworkRequest(url));
    QSignalSpy finishedSpy(reply, &QCNetworkReply::finished);

    const auto replacementLogger = QCNetworkLoggerHandle::create<QCNetworkDefaultLogger>();
    m_manager->setLogger(replacementLogger);
    originalLogger = {};

    QCOMPARE(destructionCount, 0);
    if (!reply->isFinished()) {
        QVERIFY(finishedSpy.wait(2000));
    }
    QCOMPARE(destructionCount, 0);

    reply->deleteLater();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QCOMPARE(destructionCount, 1);

    TestSupport::setMockHandler(m_manager, nullptr);
}

void TestQCNetworkLogger::testDebugTraceFlag()
{
    QVERIFY(!m_manager->debugTraceEnabled());

    m_manager->setDebugTraceEnabled(true);
    QVERIFY(m_manager->debugTraceEnabled());

    m_manager->setDebugTraceEnabled(false);
    QVERIFY(!m_manager->debugTraceEnabled());
}

void TestQCNetworkLogger::testDebugTraceRedaction()
{
    const QByteArray raw = QByteArray("GET /path?token=abc&foo=bar HTTP/1.1\r\n"
                                      "Authorization: Bearer secret_token\r\n"
                                      "Proxy-Authorization: Basic dXNlcjpwYXNz\r\n"
                                      "Cookie: sessionid=abc\r\n"
                                      "Set-Cookie: sid=def\r\n");
    Internal::ReplyTraceState trace;
    trace.beginNetworkAttempt();
    const QString message = TraceTest::consume(trace, CURLINFO_HEADER_OUT, raw);

    QVERIFY(!message.isEmpty());
    QVERIFY(message.startsWith("HEADER_OUT: "));
    QVERIFY(!message.contains("secret_token"));
    QVERIFY(!message.contains("dXNlcjpwYXNz"));
    QVERIFY(!message.contains("sessionid=abc"));
    QVERIFY(!message.contains("token=abc"));
    QVERIFY(message.contains("Authorization: [REDACTED]"));
    QVERIFY(message.contains("Proxy-Authorization: [REDACTED]"));
    QVERIFY(message.contains("Cookie: [REDACTED]"));
    QVERIFY(message.contains("Set-Cookie: [REDACTED]"));
    QVERIFY(message.contains("GET /path?[REDACTED] HTTP/1.1"));
    QVERIFY(!message.contains("foo=bar"));
}
