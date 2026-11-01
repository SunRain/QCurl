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
