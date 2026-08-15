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
#include "private/QCNetworkReplyCallbacks_p.h"

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

namespace {

constexpr auto kReentrancyProbeEnvironment = "QCURL_LOGGER_REENTRANCY_PROBE";
constexpr int kReentrancyProbeTimeoutMs    = 3000;

std::atomic<QCNetworkDefaultLogger *> messageHandlerLogger = nullptr;
std::atomic_int messageHandlerCallCount                    = 0;

bool isReentrancyProbeChild(const char *probeName)
{
    return qgetenv(kReentrancyProbeEnvironment) == probeName;
}

void verifyReentrancyProbeCompletes(const char *probeName, const char *testFunction)
{
    QProcess child;
    child.setProcessChannelMode(QProcess::MergedChannels);

    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    environment.insert(QString::fromLatin1(kReentrancyProbeEnvironment),
                       QString::fromLatin1(probeName));
    child.setProcessEnvironment(environment);
    child.start(QCoreApplication::applicationFilePath(), {QString::fromLatin1(testFunction)});

    QVERIFY2(child.waitForStarted(), qPrintable(child.errorString()));
    if (!child.waitForFinished(kReentrancyProbeTimeoutMs)) {
        child.kill();
        child.waitForFinished();
        QFAIL("同步重入日志探针超时，logger 发生死锁");
    }

    const QByteArray output = child.readAll();
    QVERIFY2(child.exitStatus() == QProcess::NormalExit, output.constData());
    QVERIFY2(child.exitCode() == 0, output.constData());
}

void reentrantQtMessageHandler(QtMsgType type,
                               const QMessageLogContext &context,
                               const QString &message)
{
    Q_UNUSED(type)
    Q_UNUSED(context)
    Q_UNUSED(message)

    const int callIndex = messageHandlerCallCount.fetch_add(1, std::memory_order_relaxed);
    if (callIndex == 0) {
        if (auto *logger = messageHandlerLogger.load(std::memory_order_acquire)) {
            static_cast<void>(logger->log(NetworkLogLevel::Warning,
                                          QStringLiteral("QtHandler"),
                                          QStringLiteral("inner")));
        }
    }
}

} // namespace

/**
 * @brief 验证 logger Core contract、默认实现与 manager wiring。
 */
class TestQCNetworkLogger : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void init();
    void cleanup();

    void testLogEntryDefaults();
    void testLogEntryAccessorsNormalizeUtc();
    void testLogEntryCopyAndMove();
    void testConvenienceOverloadForwards();
    void testDefaultLoggerFiltersAndStoresEntries();
    void testDefaultLoggerReportsSuccessfulAndFilteredOutput();
    void testDefaultLoggerRejectsInvalidFileConfigWithoutMutation();
    void testDefaultLoggerReportsOpenFailure();
    void testDefaultLoggerReportsBackupRemovalFailure();
    void testDefaultLoggerReportsCurrentLogRemovalFailure();
    void testDefaultLoggerReportsRotationRenameFailure();
    void testDefaultLoggerReportsWriteFailure();
    void testDefaultLoggerCallbackAndClear();
    void testDefaultLoggerCustomCallbackReentrant();
    void testDefaultLoggerQtMessageHandlerReentrant();
    void testDefaultLoggerFileOutput();
    void testSetAndGetLogger();
    void testLoggerHandleCopyMoveAndBorrowLifetime();
    void testLoggerNullptr();
    void testReplyRetainsLoggerSnapshotUntilDestroyed();
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
void TestQCNetworkLogger::testDefaultLoggerReportsBackupRemovalFailure()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString logPath = dir.filePath(QStringLiteral("rotate-remove.log"));
    QFile seed(logPath);
    QVERIFY(seed.open(QIODevice::WriteOnly));
    QCOMPARE(seed.write("oversized"), qint64(9));
    seed.close();

    const QString backupPath = QStringLiteral("%1.1").arg(logPath);
    QVERIFY(QDir().mkpath(backupPath));
    QFile blocker(QDir(backupPath).filePath(QStringLiteral("keep")));
    QVERIFY(blocker.open(QIODevice::WriteOnly));
    QCOMPARE(blocker.write("x"), qint64(1));
    blocker.close();

    QCNetworkDefaultLogger logger;
    logger.enableConsoleOutput(false);
    QVERIFY(logger.enableFileOutput(logPath, 1, 1).isSuccess());

    const QCNetworkLogResult result = logger.log(NetworkLogEntry(NetworkLogLevel::Info,
                                                                 QStringLiteral("Rotate"),
                                                                 QStringLiteral("remove failure"),
                                                                 QDateTime::currentDateTimeUtc()));
    QCOMPARE(result.status(), QCNetworkLogResult::Status::RemoveFailed);
    QVERIFY(!result.error().isEmpty());
}

/**
 * @brief 验证零备份轮转无法删除当前日志时返回结构化失败。
 */
void TestQCNetworkLogger::testDefaultLoggerReportsCurrentLogRemovalFailure()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString logPath = dir.filePath(QStringLiteral("current-directory"));
    QVERIFY(QDir().mkpath(logPath));
    QFile child(QDir(logPath).filePath(QStringLiteral("oversized")));
    QVERIFY(child.open(QIODevice::WriteOnly));
    QCOMPARE(child.write("payload"), qint64(7));
    child.close();

    QCNetworkDefaultLogger logger;
    logger.enableConsoleOutput(false);
    QVERIFY(logger.enableFileOutput(logPath, 1, 0).isSuccess());

    const QCNetworkLogResult result = logger.log(
        NetworkLogEntry(NetworkLogLevel::Info,
                        QStringLiteral("Rotate"),
                        QStringLiteral("current removal failure"),
                        QDateTime::currentDateTimeUtc()));
    QCOMPARE(result.status(), QCNetworkLogResult::Status::RemoveFailed);
    QVERIFY(!result.error().isEmpty());
}

/**
 * @brief 验证当前日志无法重命名时返回结构化轮转失败。
 */
void TestQCNetworkLogger::testDefaultLoggerReportsRotationRenameFailure()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString logPath = dir.filePath(QStringLiteral("directory-as-log"));
    QVERIFY(QDir().mkpath(logPath));
    QFile child(QDir(logPath).filePath(QStringLiteral("oversized")));
    QVERIFY(child.open(QIODevice::WriteOnly));
    QCOMPARE(child.write("payload"), qint64(7));
    child.close();

    QCNetworkDefaultLogger logger;
    logger.enableConsoleOutput(false);
    QVERIFY(logger.enableFileOutput(logPath, 1, 1).isSuccess());

    const QCNetworkLogResult result = logger.log(NetworkLogEntry(NetworkLogLevel::Info,
                                                                 QStringLiteral("Rotate"),
                                                                 QStringLiteral("rename failure"),
                                                                 QDateTime::currentDateTimeUtc()));
    QCOMPARE(result.status(), QCNetworkLogResult::Status::RenameFailed);
    QVERIFY(!result.error().isEmpty());
}

/**
 * @brief 验证底层设备拒绝写入时返回结构化写入失败。
 */
void TestQCNetworkLogger::testDefaultLoggerReportsWriteFailure()
{
#ifdef Q_OS_LINUX
    if (!QFile::exists(QStringLiteral("/dev/full"))) {
        QSKIP("当前 Linux 环境没有 /dev/full");
    }

    QCNetworkDefaultLogger logger;
    logger.enableConsoleOutput(false);
    QVERIFY(logger.enableFileOutput(QStringLiteral("/dev/full"), 1024 * 1024, 0).isSuccess());

    const QCNetworkLogResult result = logger.log(NetworkLogEntry(NetworkLogLevel::Info,
                                                                 QStringLiteral("File"),
                                                                 QStringLiteral("write failure"),
                                                                 QDateTime::currentDateTimeUtc()));
    QCOMPARE(result.status(), QCNetworkLogResult::Status::WriteFailed);
    QVERIFY(!result.error().isEmpty());
#else
    QSKIP("写入失败夹具当前仅在 Linux 使用 /dev/full 验证");
#endif
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

    static_cast<void>(logger.log(NetworkLogEntry(NetworkLogLevel::Debug,
                                                 QStringLiteral("Trace"),
                                                 QStringLiteral("payload"),
                                                 QDateTime::currentDateTimeUtc())));

    QCOMPARE(callbacks.size(), 1);
    QCOMPARE(callbacks.first().category(), QStringLiteral("Trace"));
    QCOMPARE(logger.entries().size(), 1);

    logger.clear();
    QVERIFY(logger.entries().isEmpty());
}

void TestQCNetworkLogger::testDefaultLoggerCustomCallbackReentrant()
{
    if (!isReentrancyProbeChild("custom-callback")) {
        verifyReentrancyProbeCompletes("custom-callback",
                                       "testDefaultLoggerCustomCallbackReentrant");
        return;
    }

    QCNetworkDefaultLogger logger;
    logger.enableConsoleOutput(false);

    int callbackCount = 0;
    logger.setCustomCallback([&logger, &callbackCount](const NetworkLogEntry &) {
        ++callbackCount;
        if (callbackCount == 1) {
            static_cast<void>(logger.log(NetworkLogLevel::Info,
                                         QStringLiteral("Callback"),
                                         QStringLiteral("inner")));
        }
    });

    static_cast<void>(
        logger.log(NetworkLogLevel::Info, QStringLiteral("Callback"), QStringLiteral("outer")));

    QCOMPARE(callbackCount, 2);
    const QList<NetworkLogEntry> stored = logger.entries();
    QCOMPARE(stored.size(), 2);
    QCOMPARE(stored.at(0).message(), QStringLiteral("outer"));
    QCOMPARE(stored.at(1).message(), QStringLiteral("inner"));
}

void TestQCNetworkLogger::testDefaultLoggerQtMessageHandlerReentrant()
{
    if (!isReentrancyProbeChild("qt-message-handler")) {
        verifyReentrancyProbeCompletes("qt-message-handler",
                                       "testDefaultLoggerQtMessageHandlerReentrant");
        return;
    }

    QCNetworkDefaultLogger logger;
    messageHandlerCallCount.store(0, std::memory_order_relaxed);
    messageHandlerLogger.store(&logger, std::memory_order_release);
    const QtMessageHandler previousHandler = qInstallMessageHandler(reentrantQtMessageHandler);

    static_cast<void>(
        logger.log(NetworkLogLevel::Warning, QStringLiteral("QtHandler"), QStringLiteral("outer")));

    qInstallMessageHandler(previousHandler);
    messageHandlerLogger.store(nullptr, std::memory_order_release);

    QCOMPARE(messageHandlerCallCount.load(std::memory_order_relaxed), 2);
    const QList<NetworkLogEntry> stored = logger.entries();
    QCOMPARE(stored.size(), 2);
    QCOMPARE(stored.at(0).message(), QStringLiteral("outer"));
    QCOMPARE(stored.at(1).message(), QStringLiteral("inner"));
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
    QVERIFY(logger.enableFileOutput(logPath, 1024 * 1024, 2).isSuccess());
    QVERIFY(logger
                .log(NetworkLogEntry(NetworkLogLevel::Info,
                                     QStringLiteral("File"),
                                     QStringLiteral("written"),
                                     QDateTime::currentDateTimeUtc()))
                .isSuccess());

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
    const auto logger1 = QCNetworkLoggerHandle::create<QCNetworkDefaultLogger>();
    const auto logger2 = QCNetworkLoggerHandle::create<QCNetworkDefaultLogger>();

    m_manager->setLogger(logger1);
    QCOMPARE(m_manager->logger(), logger1);

    m_manager->setLogger(logger2);
    QCOMPARE(m_manager->logger(), logger2);
}

/**
 * @brief 验证 opaque handle 的复制、移动和非 owning 借用生命周期。
 */
void TestQCNetworkLogger::testLoggerHandleCopyMoveAndBorrowLifetime()
{
    int destructionCount = 0;
    class DestructionTrackingLogger final : public QCNetworkLogger
    {
    public:
        explicit DestructionTrackingLogger(int *count)
            : m_count(count)
        {}

        ~DestructionTrackingLogger() override { ++(*m_count); }

        QCNetworkLogResult log(const NetworkLogEntry &) override { return {}; }

    private:
        int *m_count = nullptr;
    };

    auto original = QCNetworkLoggerHandle::create<DestructionTrackingLogger>(&destructionCount);
    QCNetworkLogger *borrowed = original.get();
    QVERIFY(borrowed != nullptr);

    QCNetworkLoggerHandle copied(original);
    QCOMPARE(copied.get(), borrowed);

    QCNetworkLoggerHandle assigned;
    assigned = copied;
    QCOMPARE(assigned.get(), borrowed);

    QCNetworkLoggerHandle moved(std::move(original));
    QVERIFY(!original);
    QCOMPARE(moved.get(), borrowed);

    copied   = {};
    assigned = {};
    QCOMPARE(destructionCount, 0);

    moved = {};
    QCOMPARE(destructionCount, 1);
}

/**
 * @brief 验证传入 nullptr 会显式关闭 logger。
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
    const QByteArray raw  = QByteArray("GET /path?token=abc&foo=bar HTTP/1.1\r\n"
                                       "Authorization: Bearer secret_token\r\n"
                                       "Proxy-Authorization: Basic dXNlcjpwYXNz\r\n"
                                       "Cookie: sessionid=abc\r\n"
                                       "Set-Cookie: sid=def\r\n");
    const QString message = Internal::formatReplyDebugTraceMessage(CURLINFO_HEADER_OUT, raw);

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

    const QString message = Internal::formatReplyDebugTraceMessage(CURLINFO_HEADER_OUT, raw);

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
