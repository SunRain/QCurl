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

static_assert(!std::is_copy_constructible_v<QCNetworkDefaultLogger>);
static_assert(!std::is_move_constructible_v<QCNetworkDefaultLogger>);
static_assert(std::is_copy_constructible_v<QCNetworkLoggerHandle>);
static_assert(std::is_copy_assignable_v<QCNetworkLoggerHandle>);
static_assert(std::is_nothrow_move_constructible_v<QCNetworkLoggerHandle>);
static_assert(std::is_nothrow_move_assignable_v<QCNetworkLoggerHandle>);

/**
 * @brief 验证 logger Core contract、默认实现与 manager wiring。
 */

void TestQCNetworkLogger::init()
{
    m_manager = new QCNetworkAccessManager(this);
}

void TestQCNetworkLogger::cleanup()
{
    if (m_manager) {
        m_manager->setLogger({});
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
    const QDateTime seed(QDate(2026, 4, 15),
                         QTime(23, 30, 0),
                         QTimeZone::fromSecondsAheadOfUtc(8 * 3600));
    NetworkLogEntry entry(NetworkLogLevel::Debug,
                          QStringLiteral("Request"),
                          QStringLiteral("payload"),
                          seed);

    QCOMPARE(entry.level(), NetworkLogLevel::Debug);
    QCOMPARE(entry.category(), QStringLiteral("Request"));
    QCOMPARE(entry.message(), QStringLiteral("payload"));
    QCOMPARE(entry.timestampUtc().offsetFromUtc(), 0);
    QCOMPARE(entry.timestampUtc().toMSecsSinceEpoch(), seed.toMSecsSinceEpoch());

    entry.setLevel(NetworkLogLevel::Warning);
    entry.setCategory(QStringLiteral("Trace"));
    entry.setMessage(QStringLiteral("redacted"));

    const QDateTime second(QDate(2026, 4, 16),
                           QTime(8, 0, 0),
                           QTimeZone::fromSecondsAheadOfUtc(-5 * 3600));
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

        QCNetworkLogResult log(const NetworkLogEntry &entry) override
        {
            ++count;
            lastEntry = entry;
            return {};
        }
    };

    CapturingLogger logger;
    const QCNetworkLogResult result = logger.log(NetworkLogLevel::Warning,
                                                 QStringLiteral("Trace"),
                                                 QStringLiteral("masked"));

    QVERIFY(result.isSuccess());
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

    static_cast<void>(logger.log(NetworkLogEntry(NetworkLogLevel::Info,
                                                 QStringLiteral("Request"),
                                                 QStringLiteral("skip"),
                                                 QDateTime::currentDateTimeUtc())));
    static_cast<void>(logger.log(NetworkLogEntry(NetworkLogLevel::Error,
                                                 QStringLiteral("Response"),
                                                 QStringLiteral("kept"),
                                                 QDateTime::currentDateTimeUtc())));

    const QList<NetworkLogEntry> stored = logger.entries();
    QCOMPARE(stored.size(), 1);
    QCOMPARE(stored.first().level(), NetworkLogLevel::Error);
    QCOMPARE(stored.first().category(), QStringLiteral("Response"));
    QCOMPARE(stored.first().message(), QStringLiteral("kept"));
}

/**
 * @brief 验证默认 logger 会结构化区分成功输出与级别过滤。
 */
void TestQCNetworkLogger::testDefaultLoggerReportsSuccessfulAndFilteredOutput()
{
    QCNetworkDefaultLogger logger;
    logger.enableConsoleOutput(false);
    logger.setMinLogLevel(NetworkLogLevel::Warning);

    const QCNetworkLogResult filtered = logger.log(NetworkLogEntry(NetworkLogLevel::Info,
                                                                   QStringLiteral("Result"),
                                                                   QStringLiteral("filtered"),
                                                                   QDateTime::currentDateTimeUtc()));
    QCOMPARE(filtered.status(), QCNetworkLogResult::Status::Filtered);
    QVERIFY(!filtered.isSuccess());
    QVERIFY(filtered.error().isEmpty());

    const QCNetworkLogResult written = logger.log(NetworkLogEntry(NetworkLogLevel::Error,
                                                                  QStringLiteral("Result"),
                                                                  QStringLiteral("written"),
                                                                  QDateTime::currentDateTimeUtc()));
    QCOMPARE(written.status(), QCNetworkLogResult::Status::Success);
    QVERIFY(written.isSuccess());
    QVERIFY(written.error().isEmpty());
}

/**
 * @brief 验证无效文件配置会返回失败并保留旧配置。
 */
void TestQCNetworkLogger::testDefaultLoggerRejectsInvalidFileConfigWithoutMutation()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString logPath = dir.filePath(QStringLiteral("stable.log"));
    QCNetworkDefaultLogger logger;
    logger.enableConsoleOutput(false);
    QVERIFY(logger.enableFileOutput(logPath, 1024, 1).isSuccess());

    const QCNetworkLogResult invalidPath = logger.enableFileOutput(QString(), 1024, 1);
    QCOMPARE(invalidPath.status(), QCNetworkLogResult::Status::InvalidArgument);
    QVERIFY(!invalidPath.error().isEmpty());

    const QCNetworkLogResult invalidSize = logger.enableFileOutput(logPath, -1, 1);
    QCOMPARE(invalidSize.status(), QCNetworkLogResult::Status::InvalidArgument);

    const QCNetworkLogResult invalidBackupCount = logger.enableFileOutput(logPath, 1024, -1);
    QCOMPARE(invalidBackupCount.status(), QCNetworkLogResult::Status::InvalidArgument);

    const QCNetworkLogResult writeResult = logger.log(
        NetworkLogEntry(NetworkLogLevel::Info,
                        QStringLiteral("Config"),
                        QStringLiteral("old configuration retained"),
                        QDateTime::currentDateTimeUtc()));
    QVERIFY(writeResult.isSuccess());
    QVERIFY(QFile::exists(logPath));
}

/**
 * @brief 验证日志文件无法打开时返回结构化失败。
 */
void TestQCNetworkLogger::testDefaultLoggerReportsOpenFailure()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    QCNetworkDefaultLogger logger;
    logger.enableConsoleOutput(false);
    QVERIFY(logger.enableFileOutput(dir.path(), 1024 * 1024, 1).isSuccess());

    const QCNetworkLogResult result = logger.log(NetworkLogEntry(NetworkLogLevel::Info,
                                                                 QStringLiteral("File"),
                                                                 QStringLiteral("open failure"),
                                                                 QDateTime::currentDateTimeUtc()));
    QCOMPARE(result.status(), QCNetworkLogResult::Status::OpenFailed);
    QVERIFY(!result.error().isEmpty());
}

/**
 * @brief 验证轮转目标无法删除时不会继续伪装成功。
 */
QTEST_MAIN(TestQCNetworkLogger)
