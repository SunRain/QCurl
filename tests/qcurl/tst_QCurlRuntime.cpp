/**
 * @file
 * @brief 验证应用级 QCurlRuntime 的单向关闭合同。
 */

#include "QCCurlHandleManager.h"
#include "QCurlRuntime.h"
#include "private/QCurlRuntimeState_p.h"

#include <QAbstractEventDispatcher>
#include <QCoreApplication>
#include <QProcess>
#include <QProcessEnvironment>
#include <QSemaphore>
#include <QThread>
#include <QtTest/QtTest>

#include <algorithm>
#include <atomic>
#include <future>
#include <memory>

using namespace QCurl;

namespace {

constexpr auto kScenarioEnvironment = "QCURL_RUNTIME_TEST_SCENARIO";

QCurlShutdownResult waitForShutdown(QCurlRuntime *runtime, qint64 timeoutMilliseconds)
{
    return std::async(std::launch::async,
                      [runtime, timeoutMilliseconds]() {
                          return runtime->shutdownAndWait(QDeadlineTimer(timeoutMilliseconds));
                      })
        .get();
}

/**
 * @brief 模拟具有独立 affinity thread 的 runtime participant。
 *
 * participant callback 故意捕获 `this`，用于验证 registration 注销后，已经排入 owner
 * thread 的旧 shutdown token 不会再调用对象。
 */
class RuntimeParticipantHarness final : public QObject
{
    Q_OBJECT

public:
    RuntimeParticipantHarness() = default;

    ~RuntimeParticipantHarness() override
    {
        Internal::unregisterRuntimeShutdownParticipant(m_registrationId);
        m_runtimeLease.reset();
    }

    /**
     * @brief 在当前 owner thread 注册 participant 并持有 runtime lease。
     * @param callbackCount shutdown callback 的调用计数器。
     * @return 有效 registration id；注册失败时返回 0。
     */
    [[nodiscard]] quint64 registerParticipant(std::atomic<int> *callbackCount)
    {
        Q_ASSERT(QThread::currentThread() == thread());
        m_runtimeLease = Internal::acquireRuntimeLease();
        if (!m_runtimeLease.isValid()) {
            return 0;
        }

        m_registrationId
            = Internal::registerRuntimeShutdownParticipant(this, [this, callbackCount]() {
                  callbackCount->fetch_add(1, std::memory_order_relaxed);
                  m_runtimeLease.reset();
              });
        return m_registrationId;
    }

    /**
     * @brief 在 owner thread 注销 registration，并释放 runtime lease。
     *
     * 注销返回后，registry 不得再为该 registration 排入新 token；已经排入的 token 只能在
     * owner thread 解析为“已注销”并安全丢弃。
     */
    void unregisterAndReleaseRuntimeLease() noexcept
    {
        Q_ASSERT(QThread::currentThread() == thread());
        Internal::unregisterRuntimeShutdownParticipant(m_registrationId);
        m_registrationId = 0;
        m_runtimeLease.reset();
    }

private:
    Q_DISABLE_COPY_MOVE(RuntimeParticipantHarness)

    Internal::RuntimeLease m_runtimeLease;
    quint64 m_registrationId = 0;
};

} // namespace

class tst_QCurlRuntime : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void runtimeShutdownContract();

private:
    void runParentScenarios();
    void runInstanceTeardownScenario();
    void runControllerReplacementScenario();
    void runExternalUsersUnverifiedScenario();
    void runFailureIsTerminalScenario();
    void runWrongThreadScenario();
    void runTimeoutAndConcurrentShutdownScenario();
    void runParticipantFenceScenario();
    void runPoisonScenario();
};

void tst_QCurlRuntime::runtimeShutdownContract()
{
    const QByteArray scenario = qgetenv(kScenarioEnvironment);
    if (scenario.isEmpty()) {
        runParentScenarios();
    } else if (scenario == QByteArrayLiteral("instance-teardown")) {
        runInstanceTeardownScenario();
    } else if (scenario == QByteArrayLiteral("controller-replacement")) {
        runControllerReplacementScenario();
    } else if (scenario == QByteArrayLiteral("external-users-unverified")) {
        runExternalUsersUnverifiedScenario();
    } else if (scenario == QByteArrayLiteral("failure-is-terminal")) {
        runFailureIsTerminalScenario();
    } else if (scenario == QByteArrayLiteral("wrong-thread")) {
        runWrongThreadScenario();
    } else if (scenario == QByteArrayLiteral("timeout-and-concurrent-shutdown")) {
        runTimeoutAndConcurrentShutdownScenario();
    } else if (scenario == QByteArrayLiteral("participant-fence")) {
        runParticipantFenceScenario();
    } else if (scenario == QByteArrayLiteral("poison")) {
        runPoisonScenario();
    } else {
        QFAIL("未知 QCurlRuntime 子进程场景");
    }
}

void tst_QCurlRuntime::runParentScenarios()
{
    const QList<QByteArray> scenarios = {
        QByteArrayLiteral("instance-teardown"),
        QByteArrayLiteral("controller-replacement"),
        QByteArrayLiteral("external-users-unverified"),
        QByteArrayLiteral("failure-is-terminal"),
        QByteArrayLiteral("wrong-thread"),
        QByteArrayLiteral("timeout-and-concurrent-shutdown"),
        QByteArrayLiteral("participant-fence"),
        QByteArrayLiteral("poison"),
    };

    for (const QByteArray &scenario : scenarios) {
        QProcess process;
        QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
        environment.insert(QString::fromLatin1(kScenarioEnvironment), QString::fromLatin1(scenario));
        process.setProcessEnvironment(environment);
        process.start(QCoreApplication::applicationFilePath(), {});
        QVERIFY2(process.waitForFinished(30000), qPrintable(process.errorString()));
        const QByteArray output = process.readAllStandardOutput() + process.readAllStandardError();
        QCOMPARE(process.exitStatus(), QProcess::NormalExit);
        QVERIFY2(process.exitCode() == 0, output.constData());
    }
}

void tst_QCurlRuntime::runInstanceTeardownScenario()
{
    QCurlRuntime runtime;
    QVERIFY(runtime.isProcessOwner());
    QCOMPARE(runtime.state(), QCurlRuntimeState::Uninitialized);

    {
        QCCurlHandleManager first;
        QVERIFY2(first.isValid(), qPrintable(first.initializationError()));
    }

    QCOMPARE(Internal::runtimeCleanupCountForTest(), 0);
    QCCurlHandleManager second;
    QVERIFY2(second.isValid(), qPrintable(second.initializationError()));
    QCOMPARE(runtime.state(), QCurlRuntimeState::Running);
}

void tst_QCurlRuntime::runControllerReplacementScenario()
{
    {
        QCurlRuntime firstController;
        QVERIFY(firstController.isProcessOwner());
        QVERIFY(firstController.confirmExternalUsersStopped());
    }

    QCurlRuntime replacementController;
    QVERIFY(replacementController.isProcessOwner());
    QCOMPARE(replacementController.beginShutdown(),
             QCurlShutdownStartResult::ExternalUsersUnverified);
    QCOMPARE(replacementController.state(), QCurlRuntimeState::Failed);
    QCOMPARE(Internal::runtimeCleanupCountForTest(), 0);
}

void tst_QCurlRuntime::runExternalUsersUnverifiedScenario()
{
    QCurlRuntime runtime;
    QCOMPARE(runtime.beginShutdown(), QCurlShutdownStartResult::ExternalUsersUnverified);
    QCOMPARE(runtime.state(), QCurlRuntimeState::Failed);
    QCOMPARE(waitForShutdown(&runtime, 1000), QCurlShutdownResult::ExternalUsersUnverified);
    QCOMPARE(Internal::runtimeCleanupCountForTest(), 0);
}

void tst_QCurlRuntime::runFailureIsTerminalScenario()
{
    QCurlRuntime runtime;
    QCOMPARE(runtime.beginShutdown(), QCurlShutdownStartResult::ExternalUsersUnverified);
    const QString diagnostic = runtime.diagnostic();
    QVERIFY(!diagnostic.isEmpty());

    Internal::markRuntimePoisonedForTest(QStringLiteral("late poison"));
    QCOMPARE(runtime.state(), QCurlRuntimeState::Failed);
    QCOMPARE(runtime.diagnostic(), diagnostic);
    QCOMPARE(waitForShutdown(&runtime, 1000), QCurlShutdownResult::ExternalUsersUnverified);
    QCOMPARE(Internal::runtimeCleanupCountForTest(), 0);
}

void tst_QCurlRuntime::runWrongThreadScenario()
{
    QCurlRuntime runtime;
    QVERIFY(runtime.confirmExternalUsersStopped());
    QCOMPARE(runtime.beginShutdown(), QCurlShutdownStartResult::Started);
    QCOMPARE(runtime.shutdownAndWait(QDeadlineTimer(1000)), QCurlShutdownResult::WrongThread);
    QCOMPARE(runtime.state(), QCurlRuntimeState::Stopping);
    QCOMPARE(waitForShutdown(&runtime, 1000), QCurlShutdownResult::Succeeded);
    QCOMPARE(Internal::runtimeCleanupCountForTest(), 1);
}

void tst_QCurlRuntime::runTimeoutAndConcurrentShutdownScenario()
{
    QCurlRuntime runtime;
    auto activeHandle = std::make_unique<QCCurlHandleManager>();
    QVERIFY2(activeHandle->isValid(), qPrintable(activeHandle->initializationError()));
    QVERIFY(runtime.confirmExternalUsersStopped());
    QCOMPARE(runtime.beginShutdown(), QCurlShutdownStartResult::Started);

    QCCurlHandleManager rejectedHandle;
    QVERIFY(!rejectedHandle.isValid());
    QVERIFY(rejectedHandle.initializationError().contains(QStringLiteral("Stopping")));
    QCOMPARE(waitForShutdown(&runtime, 10), QCurlShutdownResult::TimedOut);
    QCOMPARE(runtime.state(), QCurlRuntimeState::Stopping);

    activeHandle.reset();
    auto first = std::async(std::launch::async,
                            [&runtime]() { return runtime.shutdownAndWait(QDeadlineTimer(1000)); });
    auto second                        = std::async(std::launch::async, [&runtime]() {
        return runtime.shutdownAndWait(QDeadlineTimer(1000));
    });
    QList<QCurlShutdownResult> results = {first.get(), second.get()};
    std::sort(results.begin(), results.end());
    QCOMPARE(results.at(0), QCurlShutdownResult::Succeeded);
    QCOMPARE(results.at(1), QCurlShutdownResult::AlreadyStopped);
    QCOMPARE(Internal::runtimeCleanupCountForTest(), 1);
    QCOMPARE(runtime.state(), QCurlRuntimeState::Stopped);

    QCCurlHandleManager stoppedHandle;
    QVERIFY(!stoppedHandle.isValid());
    QVERIFY(stoppedHandle.initializationError().contains(QStringLiteral("Stopped")));
}

void tst_QCurlRuntime::runParticipantFenceScenario()
{
    QCurlRuntime runtime;
    QThread participantThread;
    auto *participant = new RuntimeParticipantHarness;
    participant->moveToThread(&participantThread);
    participantThread.start();

    std::atomic<int> callbackCount{0};
    quint64 registrationId = 0;
    QVERIFY(QMetaObject::invokeMethod(
        participant,
        [&]() { registrationId = participant->registerParticipant(&callbackCount); },
        Qt::BlockingQueuedConnection));
    QVERIFY(registrationId != 0);

    QSemaphore blockerEntered;
    QSemaphore releaseBlocker;
    QSemaphore participantDestroyed;
    QVERIFY(QMetaObject::invokeMethod(
        participant,
        [&]() {
            blockerEntered.release();
            releaseBlocker.acquire();
            delete participant;
            participantDestroyed.release();
        },
        Qt::QueuedConnection));
    blockerEntered.acquire();

    QVERIFY(runtime.confirmExternalUsersStopped());
    QCOMPARE(runtime.beginShutdown(), QCurlShutdownStartResult::Started);

    // beginShutdown() 已经排入旧 token；owner-thread 注销后，该 token 必须安全丢弃。
    releaseBlocker.release();
    participantDestroyed.acquire();

    QAbstractEventDispatcher *endpoint = participantThread.eventDispatcher();
    QVERIFY(endpoint);
    QVERIFY(QMetaObject::invokeMethod(endpoint, []() {}, Qt::BlockingQueuedConnection));
    QCOMPARE(callbackCount.load(std::memory_order_relaxed), 0);
    QCOMPARE(waitForShutdown(&runtime, 1000), QCurlShutdownResult::Succeeded);
    QCOMPARE(Internal::runtimeCleanupCountForTest(), 1);

    participantThread.quit();
    QVERIFY(participantThread.wait(1000));
}

void tst_QCurlRuntime::runPoisonScenario()
{
    QCurlRuntime runtime;
    QCCurlHandleManager activeHandle;
    QVERIFY(activeHandle.isValid());
    Internal::markRuntimePoisonedForTest(QStringLiteral("forced poison"));
    QCOMPARE(runtime.state(), QCurlRuntimeState::Failed);
    QCOMPARE(waitForShutdown(&runtime, 1000), QCurlShutdownResult::Poison);
    QCOMPARE(Internal::runtimeCleanupCountForTest(), 0);
}

QTEST_GUILESS_MAIN(tst_QCurlRuntime)
#include "tst_QCurlRuntime.moc"
