// SPDX-License-Identifier: MIT
// Copyright (c) 2025 QCurl Project

#include "QCNetworkAccessManager.h"
#include "QCNetworkDefaultLogger.h"
#include "QCNetworkLogger.h"
#include "QCNetworkReply.h"
#include "QCNetworkReply_p.h"

#include <QCoreApplication>
#include <QDate>
#include <QEvent>
#include <QFile>
#include <QTemporaryDir>
#include <QTime>
#include <QTimeZone>
#include <QUrl>
#include <QtTest>

#include <type_traits>
#include <utility>

using namespace QCurl;

static_assert(!std::is_copy_constructible_v<QCNetworkDefaultLogger>);
static_assert(!std::is_move_constructible_v<QCNetworkDefaultLogger>);

/**
 * @brief 验证 logger Core contract、默认实现与 manager wiring。
 */
class TestQCNetworkLogger : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void testLogEntryDefaults();
    void testLogEntryAccessorsNormalizeUtc();
    void testLogEntryCopyAndMove();
    void testConvenienceOverloadForwards();
    void testDefaultLoggerFiltersAndStoresEntries();
    void testDefaultLoggerCallbackAndClear();
    void testDefaultLoggerFileOutput();
    void testSetAndGetLogger();
    void testLoggerNullptr();
    void testDebugTraceFlag();
    void testDebugTraceRedaction();
    void testDebugTraceSignedUrlRedaction();

private:
    QCNetworkAccessManager *m_manager = nullptr;
};

void TestQCNetworkLogger::init()
{
    m_manager = new QCNetworkAccessManager(this);
}

void TestQCNetworkLogger::cleanup()
{
    if (m_manager) {
        m_manager->setLogger(nullptr);
        m_manager->deleteLater();
        m_manager = nullptr;
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    }
}

/**
 * @brief 验证默认构造出的 log entry 拥有可用的 UTC 时间戳。
 */
void TestQCNetworkLogger::testLogEntryDefaults()
{
    const NetworkLogEntry entry;

    QCOMPARE(entry.level(), NetworkLogLevel::Info);
    QCOMPARE(entry.category(), QString());
    QCOMPARE(entry.message(), QString());
    QVERIFY(entry.timestampUtc().isValid());
    QCOMPARE(entry.timestampUtc().timeSpec(), Qt::UTC);
}

/**
 * @brief 验证 accessor-only contract 会把时间语义统一收口到 UTC。
 */
void TestQCNetworkLogger::testLogEntryAccessorsNormalizeUtc()
{
    const QDateTime seed(
        QDate(2026, 4, 15), QTime(23, 30, 0), QTimeZone::fromSecondsAheadOfUtc(8 * 3600));
    NetworkLogEntry entry(
        NetworkLogLevel::Debug, QStringLiteral("Request"), QStringLiteral("payload"), seed);

    QCOMPARE(entry.level(), NetworkLogLevel::Debug);
    QCOMPARE(entry.category(), QStringLiteral("Request"));
    QCOMPARE(entry.message(), QStringLiteral("payload"));
    QCOMPARE(entry.timestampUtc().offsetFromUtc(), 0);
    QCOMPARE(entry.timestampUtc().toMSecsSinceEpoch(), seed.toMSecsSinceEpoch());

    entry.setLevel(NetworkLogLevel::Warning);
    entry.setCategory(QStringLiteral("Trace"));
    entry.setMessage(QStringLiteral("redacted"));

    const QDateTime second(
        QDate(2026, 4, 16), QTime(8, 0, 0), QTimeZone::fromSecondsAheadOfUtc(-5 * 3600));
    entry.setTimestampUtc(second);

    QCOMPARE(entry.level(), NetworkLogLevel::Warning);
    QCOMPARE(entry.category(), QStringLiteral("Trace"));
    QCOMPARE(entry.message(), QStringLiteral("redacted"));
    QCOMPARE(entry.timestampUtc().offsetFromUtc(), 0);
    QCOMPARE(entry.timestampUtc().toMSecsSinceEpoch(), second.toMSecsSinceEpoch());
}

/**
 * @brief 验证 log entry 的 copy/move special members 保持值语义。
 */
void TestQCNetworkLogger::testLogEntryCopyAndMove()
{
    const NetworkLogEntry original(NetworkLogLevel::Error,
                                   QStringLiteral("Response"),
                                   QStringLiteral("boom"),
                                   QDateTime::currentDateTimeUtc());

    const NetworkLogEntry copied(original);
    QCOMPARE(copied.level(), original.level());
    QCOMPARE(copied.category(), original.category());
    QCOMPARE(copied.message(), original.message());
    QCOMPARE(copied.timestampUtc(), original.timestampUtc());

    NetworkLogEntry assigned;
    assigned = copied;
    QCOMPARE(assigned.level(), copied.level());
    QCOMPARE(assigned.category(), copied.category());
    QCOMPARE(assigned.message(), copied.message());
    QCOMPARE(assigned.timestampUtc(), copied.timestampUtc());

    NetworkLogEntry moved(std::move(assigned));
    QCOMPARE(moved.level(), copied.level());
    QCOMPARE(moved.category(), copied.category());
    QCOMPARE(moved.message(), copied.message());
    QCOMPARE(moved.timestampUtc(), copied.timestampUtc());
}

/**
 * @brief 验证 Core logger 的便利重载会转发到 `log(const NetworkLogEntry &)`.
 */
void TestQCNetworkLogger::testConvenienceOverloadForwards()
{
    class CapturingLogger : public QCNetworkLogger
    {
    public:
        using QCNetworkLogger::log;

        int count = 0;
        NetworkLogEntry lastEntry;

        void log(const NetworkLogEntry &entry) override
        {
            ++count;
            lastEntry = entry;
        }
    };

    CapturingLogger logger;
    logger.log(NetworkLogLevel::Warning, QStringLiteral("Trace"), QStringLiteral("masked"));

    QCOMPARE(logger.count, 1);
    QCOMPARE(logger.lastEntry.level(), NetworkLogLevel::Warning);
    QCOMPARE(logger.lastEntry.category(), QStringLiteral("Trace"));
    QCOMPARE(logger.lastEntry.message(), QStringLiteral("masked"));
    QVERIFY(logger.lastEntry.timestampUtc().isValid());
    QCOMPARE(logger.lastEntry.timestampUtc().timeSpec(), Qt::UTC);
}

/**
 * @brief 验证默认 logger 会按最小级别过滤并缓存 entry。
 */
void TestQCNetworkLogger::testDefaultLoggerFiltersAndStoresEntries()
{
    QCNetworkDefaultLogger logger;
    logger.enableConsoleOutput(false);
    logger.setMinLogLevel(NetworkLogLevel::Warning);

    logger.log(NetworkLogEntry(NetworkLogLevel::Info,
                               QStringLiteral("Request"),
                               QStringLiteral("skip"),
                               QDateTime::currentDateTimeUtc()));
    logger.log(NetworkLogEntry(NetworkLogLevel::Error,
                               QStringLiteral("Response"),
                               QStringLiteral("kept"),
                               QDateTime::currentDateTimeUtc()));

    const QList<NetworkLogEntry> stored = logger.entries();
    QCOMPARE(stored.size(), 1);
    QCOMPARE(stored.first().level(), NetworkLogLevel::Error);
    QCOMPARE(stored.first().category(), QStringLiteral("Response"));
    QCOMPARE(stored.first().message(), QStringLiteral("kept"));
}

/**
 * @brief 验证默认 logger 的回调和 clear 合同仍然成立。
 */
void TestQCNetworkLogger::testDefaultLoggerCallbackAndClear()
{
    QCNetworkDefaultLogger logger;
    logger.enableConsoleOutput(false);
    logger.setMinLogLevel(NetworkLogLevel::Debug);

    QList<NetworkLogEntry> callbacks;
    logger.setCustomCallback(
        [&callbacks](const NetworkLogEntry &entry) { callbacks.append(entry); });

    logger.log(NetworkLogEntry(NetworkLogLevel::Debug,
                               QStringLiteral("Trace"),
                               QStringLiteral("payload"),
                               QDateTime::currentDateTimeUtc()));

    QCOMPARE(callbacks.size(), 1);
    QCOMPARE(callbacks.first().category(), QStringLiteral("Trace"));
    QCOMPARE(logger.entries().size(), 1);

    logger.clear();
    QVERIFY(logger.entries().isEmpty());
}

/**
 * @brief 验证默认 logger 仍可写入文件输出。
 */
void TestQCNetworkLogger::testDefaultLoggerFileOutput()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString logPath = dir.filePath(QStringLiteral("qcurl-default.log"));

    QCNetworkDefaultLogger logger;
    logger.enableConsoleOutput(false);
    logger.enableFileOutput(logPath, 1024 * 1024, 2);
    logger.log(NetworkLogEntry(NetworkLogLevel::Info,
                               QStringLiteral("File"),
                               QStringLiteral("written"),
                               QDateTime::currentDateTimeUtc()));

    QFile file(logPath);
    QVERIFY(file.exists());
    QVERIFY(file.open(QIODevice::ReadOnly | QIODevice::Text));

    const QString content = QString::fromUtf8(file.readAll());
    QVERIFY(content.contains(QStringLiteral("File")));
    QVERIFY(content.contains(QStringLiteral("written")));
}

/**
 * @brief 验证默认 logger 可被 AccessManager 正确持有与替换。
 */
void TestQCNetworkLogger::testSetAndGetLogger()
{
    QCNetworkDefaultLogger logger1;
    QCNetworkDefaultLogger logger2;

    m_manager->setLogger(&logger1);
    QCOMPARE(m_manager->logger(), &logger1);

    m_manager->setLogger(&logger2);
    QCOMPARE(m_manager->logger(), &logger2);
}

/**
 * @brief 验证传入 nullptr 会显式关闭 logger。
 */
void TestQCNetworkLogger::testLoggerNullptr()
{
    QCNetworkDefaultLogger logger;
    m_manager->setLogger(&logger);
    m_manager->setLogger(nullptr);
    QCOMPARE(m_manager->logger(), nullptr);
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
    const QString message = QCNetworkReplyPrivate::formatDebugTraceMessage(CURLINFO_HEADER_OUT, raw);

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
    QVERIFY(message.contains("token=[REDACTED]"));
    QVERIFY(message.contains("foo=bar"));
}

void TestQCNetworkLogger::testDebugTraceSignedUrlRedaction()
{
    const QByteArray raw
        = QByteArray("GET /s3/object?X-Amz-Algorithm=AWS4-HMAC-SHA256&"
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

    const QString message = QCNetworkReplyPrivate::formatDebugTraceMessage(CURLINFO_HEADER_OUT, raw);

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

    QVERIFY(message.contains(QStringLiteral("X-Amz-Algorithm=[REDACTED]")));
    QVERIFY(message.contains(QStringLiteral("X-Amz-Credential=[REDACTED]")));
    QVERIFY(message.contains(QStringLiteral("response-content-disposition=[REDACTED]")));
    QVERIFY(message.contains(QStringLiteral("Policy=[REDACTED]")));
    QVERIFY(message.contains(QStringLiteral("Key-Pair-Id=[REDACTED]")));
    QVERIFY(message.contains(QStringLiteral("X-Goog-Algorithm=[REDACTED]")));
    QVERIFY(message.contains(QStringLiteral("alt=[REDACTED]")));
    QVERIFY(message.contains(QStringLiteral("sv=[REDACTED]")));
    QVERIFY(message.contains(QStringLiteral("rsct=[REDACTED]")));

    QVERIFY(message.contains(QStringLiteral("file=readme.txt")));
    QVERIFY(message.contains(QStringLiteral("download=1")));
    QVERIFY(message.contains(QStringLiteral("token=[REDACTED]")));
}

QTEST_MAIN(TestQCNetworkLogger)
#include "tst_QCNetworkLogger.moc"
