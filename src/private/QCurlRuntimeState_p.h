/**
 * @file
 * @brief 声明 QCurl 进程运行时 registry 与内部 lease。
 */

#ifndef QCURLRUNTIMESTATE_P_H
#define QCURLRUNTIMESTATE_P_H

#include "QCurlRuntime.h"

#include <QHash>
#include <QMutex>
#include <QString>
#include <QWaitCondition>

#include <curl/curl.h>
#include <functional>

class QObject;
class QThread;

namespace QCurl::Internal {

class RuntimeRegistry;

/**
 * @brief 表示一个受管 libcurl 对象图仍在使用进程运行时。
 *
 * lease 可移动但不可复制；最后一个有效 lease 释放时唤醒关闭协调线程。
 */
class RuntimeLease final
{
public:
    RuntimeLease() = default;
    ~RuntimeLease();

    RuntimeLease(RuntimeLease &&other) noexcept;
    RuntimeLease &operator=(RuntimeLease &&other) noexcept;

    RuntimeLease(const RuntimeLease &)            = delete;
    RuntimeLease &operator=(const RuntimeLease &) = delete;

    [[nodiscard]] bool isValid() const noexcept { return m_state != nullptr; }
    [[nodiscard]] QString diagnostic() const { return m_diagnostic; }
    void reset() noexcept;

private:
    friend class RuntimeRegistry;

    explicit RuntimeLease(RuntimeRegistry *state, QString diagnostic = {});

    RuntimeRegistry *m_state = nullptr;
    QString m_diagnostic;
};

/**
 * @brief 进程期 registry；故意不析构，以覆盖全部 `thread_local` teardown。
 *
 * registry 的 mutex 保护状态、计数、registration 与 enqueue fence。queued token 只携带纯值
 * registration id；participant callback 只在其 owner thread 重新解析后执行。
 */
class RuntimeRegistry final
{
public:
    static RuntimeRegistry &instance();

    [[nodiscard]] RuntimeLease acquireLease();
    void releaseLease() noexcept;

    [[nodiscard]] bool registerController(const void *controller);
    void unregisterController(const void *controller) noexcept;
    [[nodiscard]] bool confirmExternalUsersStopped(const void *controller);

    [[nodiscard]] QCurlShutdownStartResult beginShutdown(const void *controller);
    [[nodiscard]] QCurlShutdownResult shutdownAndWait(const void *controller,
                                                      QDeadlineTimer deadline);

    [[nodiscard]] QCurlRuntimeState state() const noexcept;
    [[nodiscard]] QString diagnostic() const;

    [[nodiscard]] quint64 registerShutdownParticipant(QObject *object,
                                                      std::function<void()> shutdown);
    void unregisterShutdownParticipant(quint64 token) noexcept;

    void markPoisoned(const QString &diagnostic) noexcept;
    void markInternalFailure(const QString &diagnostic) noexcept;

#ifdef QCURL_ENABLE_TEST_HOOKS
    [[nodiscard]] int cleanupCountForTest() const noexcept;
#endif

private:
    enum class FailureReason {
        None,
        Poison,
        ExternalUsersUnverified,
        InternalFailure,
    };

    struct ShutdownParticipant
    {
        QObject *endpoint    = nullptr;
        QThread *ownerThread = nullptr;
        std::function<void()> shutdown;
    };

    RuntimeRegistry() = default;

    [[nodiscard]] bool ensureInitialized();
    [[nodiscard]] QString admissionDiagnosticLocked() const;
    [[nodiscard]] QCurlShutdownResult failureResultLocked() const noexcept;
    void dispatchShutdownParticipant(quint64 token);
    void setFailureLocked(FailureReason reason, const QString &diagnostic) noexcept;

    mutable QMutex m_mutex;
    QWaitCondition m_stateChanged;
    QCurlRuntimeState m_state     = QCurlRuntimeState::Uninitialized;
    FailureReason m_failure       = FailureReason::None;
    CURLcode m_initializationCode = CURLE_FAILED_INIT;
    QString m_diagnostic;
    qsizetype m_leaseCount          = 0;
    const void *m_controller        = nullptr;
    bool m_externalUsersStopped     = false;
    bool m_initializationInProgress = false;
    bool m_cleanupInProgress        = false;
    int m_cleanupCount              = 0;
    quint64 m_nextParticipantToken  = 1;
    QHash<quint64, ShutdownParticipant> m_participants;
};

[[nodiscard]] RuntimeLease acquireRuntimeLease();
[[nodiscard]] QString runtimeDiagnostic();
[[nodiscard]] quint64 registerRuntimeShutdownParticipant(QObject *object,
                                                         std::function<void()> shutdown);
void unregisterRuntimeShutdownParticipant(quint64 token) noexcept;
void markRuntimePoisoned(const QString &diagnostic) noexcept;
void markRuntimeInternalFailure(const QString &diagnostic) noexcept;

#ifdef QCURL_ENABLE_TEST_HOOKS
[[nodiscard]] int runtimeCleanupCountForTest() noexcept;
void markRuntimePoisonedForTest(const QString &diagnostic) noexcept;
#endif

} // namespace QCurl::Internal

#endif // QCURLRUNTIMESTATE_P_H
