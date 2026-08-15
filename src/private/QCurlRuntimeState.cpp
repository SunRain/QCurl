#include "private/QCurlRuntimeState_p.h"

#include <QByteArray>
#include <QMetaObject>
#include <QMutexLocker>
#include <QObject>
#include <QThread>

#include <utility>

namespace QCurl::Internal {
namespace {

CURLcode initializeCurlGlobalState()
{
#ifdef QCURL_ENABLE_TEST_HOOKS
    const QByteArray forced = qgetenv("QCURL_TEST_FORCE_GLOBAL_INIT_ERROR").trimmed();
    if (!forced.isEmpty()) {
        bool ok        = false;
        const int code = forced.toInt(&ok);
        if (ok && code > static_cast<int>(CURLE_OK) && code < static_cast<int>(CURL_LAST)) {
            return static_cast<CURLcode>(code);
        }
    }
#endif
    return curl_global_init(CURL_GLOBAL_ALL);
}

QString globalInitDiagnostic(CURLcode code)
{
    if (code == CURLE_OK) {
        return {};
    }
    return QStringLiteral("curl_global_init failed (%1): %2")
        .arg(static_cast<int>(code))
        .arg(QString::fromUtf8(curl_easy_strerror(code)));
}

} // namespace

RuntimeRegistry &RuntimeRegistry::instance()
{
    // registry 故意存活到进程结束，避免跨翻译单元静态析构早于 thread_local manager。
    static auto *state = new RuntimeRegistry;
    return *state;
}

RuntimeLease RuntimeRegistry::acquireLease()
{
    if (!ensureInitialized()) {
        QMutexLocker locker(&m_mutex);
        return RuntimeLease(nullptr, admissionDiagnosticLocked());
    }

    QMutexLocker locker(&m_mutex);
    if (m_state != QCurlRuntimeState::Running) {
        return RuntimeLease(nullptr, admissionDiagnosticLocked());
    }

    ++m_leaseCount;
    return RuntimeLease(this);
}

void RuntimeRegistry::releaseLease() noexcept
{
    QMutexLocker locker(&m_mutex);
    if (m_leaseCount <= 0) {
        setFailureLocked(FailureReason::InternalFailure,
                         QStringLiteral("QCurlRuntime: internal lease underflow"));
        return;
    }
    --m_leaseCount;
    if (m_leaseCount == 0) {
        m_stateChanged.wakeAll();
    }
}

bool RuntimeRegistry::registerController(const void *controller)
{
    QMutexLocker locker(&m_mutex);
    if (!controller || m_controller) {
        return false;
    }
    m_controller           = controller;
    m_externalUsersStopped = false;
    return true;
}

void RuntimeRegistry::unregisterController(const void *controller) noexcept
{
    QMutexLocker locker(&m_mutex);
    if (m_controller == controller) {
        m_controller           = nullptr;
        m_externalUsersStopped = false;
    }
}

bool RuntimeRegistry::confirmExternalUsersStopped(const void *controller)
{
    QMutexLocker locker(&m_mutex);
    if (m_controller != controller
        || (m_state != QCurlRuntimeState::Uninitialized && m_state != QCurlRuntimeState::Running)) {
        return false;
    }
    m_externalUsersStopped = true;
    return true;
}

QCurlShutdownStartResult RuntimeRegistry::beginShutdown(const void *controller)
{
    {
        QMutexLocker locker(&m_mutex);
        if (m_controller != controller) {
            return QCurlShutdownStartResult::InternalFailure;
        }
        if (m_state == QCurlRuntimeState::Stopped) {
            return QCurlShutdownStartResult::AlreadyStopped;
        }
        if (m_state == QCurlRuntimeState::Stopping) {
            return QCurlShutdownStartResult::AlreadyStopping;
        }
        if (m_state == QCurlRuntimeState::Failed) {
            return QCurlShutdownStartResult::InternalFailure;
        }
        if (!m_externalUsersStopped) {
            setFailureLocked(FailureReason::ExternalUsersUnverified,
                             QStringLiteral("QCurlRuntime: 外部 libcurl 使用者尚未确认停止"));
            return QCurlShutdownStartResult::ExternalUsersUnverified;
        }
    }

    if (!ensureInitialized()) {
        return QCurlShutdownStartResult::InternalFailure;
    }

    QMutexLocker locker(&m_mutex);
    if (m_controller != controller || m_state == QCurlRuntimeState::Failed) {
        return QCurlShutdownStartResult::InternalFailure;
    }
    if (m_state == QCurlRuntimeState::Stopped) {
        return QCurlShutdownStartResult::AlreadyStopped;
    }
    if (m_state == QCurlRuntimeState::Stopping) {
        return QCurlShutdownStartResult::AlreadyStopping;
    }
    if (m_state != QCurlRuntimeState::Running) {
        setFailureLocked(FailureReason::InternalFailure,
                         QStringLiteral("QCurlRuntime: beginShutdown 状态无效"));
        return QCurlShutdownStartResult::InternalFailure;
    }
    m_state = QCurlRuntimeState::Stopping;

    for (auto it = m_participants.cbegin(); it != m_participants.cend(); ++it) {
        const quint64 token = it.key();
        QObject *endpoint   = it->endpoint;
        const bool queued   = QMetaObject::invokeMethod(
            endpoint,
            [token]() { RuntimeRegistry::instance().dispatchShutdownParticipant(token); },
            Qt::QueuedConnection);
        if (!queued) {
            setFailureLocked(FailureReason::InternalFailure,
                             QStringLiteral("QCurlRuntime: 无法投递 owner-thread teardown"));
            return QCurlShutdownStartResult::InternalFailure;
        }
    }

    return QCurlShutdownStartResult::Started;
}

QCurlShutdownResult RuntimeRegistry::shutdownAndWait(const void *controller, QDeadlineTimer deadline)
{
    bool cleanupRequired = false;
    {
        QMutexLocker locker(&m_mutex);
        if (m_controller != controller) {
            return QCurlShutdownResult::InternalFailure;
        }

        while (true) {
            if (m_state == QCurlRuntimeState::Failed) {
                return failureResultLocked();
            }
            if (m_state == QCurlRuntimeState::Stopped) {
                return QCurlShutdownResult::AlreadyStopped;
            }
            if (m_state != QCurlRuntimeState::Stopping) {
                setFailureLocked(FailureReason::InternalFailure,
                                 QStringLiteral("QCurlRuntime: shutdownAndWait 调用顺序无效"));
                return QCurlShutdownResult::InternalFailure;
            }
            if (m_leaseCount == 0 && !m_cleanupInProgress) {
                m_cleanupInProgress = true;
                cleanupRequired     = m_initializationCode == CURLE_OK;
                break;
            }
            if (deadline.hasExpired()) {
                return QCurlShutdownResult::TimedOut;
            }
            if (!m_stateChanged.wait(&m_mutex, deadline) && deadline.hasExpired()) {
                return QCurlShutdownResult::TimedOut;
            }
        }
    }

    if (cleanupRequired) {
        curl_global_cleanup();
    }

    QMutexLocker locker(&m_mutex);
    if (cleanupRequired) {
        ++m_cleanupCount;
    }
    m_cleanupInProgress = false;
    m_state             = QCurlRuntimeState::Stopped;
    m_diagnostic.clear();
    m_stateChanged.wakeAll();
    return QCurlShutdownResult::Succeeded;
}

QCurlRuntimeState RuntimeRegistry::state() const noexcept
{
    QMutexLocker locker(&m_mutex);
    return m_state;
}

QString RuntimeRegistry::diagnostic() const
{
    QMutexLocker locker(&m_mutex);
    return m_diagnostic;
}

quint64 RuntimeRegistry::registerShutdownParticipant(QObject *object, std::function<void()> shutdown)
{
    if (!object || !shutdown || QThread::currentThread() != object->thread()) {
        return 0;
    }
    QMutexLocker locker(&m_mutex);
    if (m_state == QCurlRuntimeState::Stopping || m_state == QCurlRuntimeState::Stopped
        || m_state == QCurlRuntimeState::Failed) {
        return 0;
    }
    auto *endpoint      = new QObject;
    const quint64 token = m_nextParticipantToken++;
    m_participants.insert(token,
                          ShutdownParticipant{endpoint,
                                              QThread::currentThread(),
                                              std::move(shutdown)});
    return token;
}

void RuntimeRegistry::unregisterShutdownParticipant(quint64 token) noexcept
{
    if (token == 0) {
        return;
    }
    QObject *endpoint = nullptr;
    {
        QMutexLocker locker(&m_mutex);
        const auto it = m_participants.find(token);
        if (it == m_participants.end()) {
            return;
        }
        if (it->ownerThread != QThread::currentThread()) {
            setFailureLocked(FailureReason::InternalFailure,
                             QStringLiteral("QCurlRuntime: participant 必须在 owner thread 注销"));
            return;
        }
        endpoint = it->endpoint;
        m_participants.erase(it);
    }
    delete endpoint;
}

void RuntimeRegistry::dispatchShutdownParticipant(quint64 token)
{
    std::function<void()> shutdown;
    QObject *endpoint = nullptr;
    {
        QMutexLocker locker(&m_mutex);
        const auto it = m_participants.find(token);
        if (it == m_participants.end()) {
            return;
        }
        if (it->ownerThread != QThread::currentThread()) {
            setFailureLocked(FailureReason::InternalFailure,
                             QStringLiteral("QCurlRuntime: participant token 在错误线程解析"));
            return;
        }
        endpoint = it->endpoint;
        shutdown = std::move(it->shutdown);
        m_participants.erase(it);
    }

    endpoint->deleteLater();
    shutdown();
}

void RuntimeRegistry::markPoisoned(const QString &diagnostic) noexcept
{
    QMutexLocker locker(&m_mutex);
    setFailureLocked(FailureReason::Poison, diagnostic);
}

void RuntimeRegistry::markInternalFailure(const QString &diagnostic) noexcept
{
    QMutexLocker locker(&m_mutex);
    setFailureLocked(FailureReason::InternalFailure, diagnostic);
}

#ifdef QCURL_ENABLE_TEST_HOOKS
int RuntimeRegistry::cleanupCountForTest() const noexcept
{
    QMutexLocker locker(&m_mutex);
    return m_cleanupCount;
}
#endif

bool RuntimeRegistry::ensureInitialized()
{
    {
        QMutexLocker locker(&m_mutex);
        while (m_state == QCurlRuntimeState::Uninitialized && m_initializationInProgress) {
            m_stateChanged.wait(&m_mutex);
        }
        if (m_state != QCurlRuntimeState::Uninitialized) {
            return m_state == QCurlRuntimeState::Running;
        }
        m_initializationInProgress = true;
    }

    const CURLcode initializationCode      = initializeCurlGlobalState();
    const QString initializationDiagnostic = globalInitDiagnostic(initializationCode);

    QMutexLocker locker(&m_mutex);
    m_initializationCode       = initializationCode;
    m_initializationInProgress = false;
    if (m_state == QCurlRuntimeState::Uninitialized) {
        if (m_initializationCode == CURLE_OK) {
            m_state = QCurlRuntimeState::Running;
            m_diagnostic.clear();
        } else {
            setFailureLocked(FailureReason::InternalFailure, initializationDiagnostic);
        }
    }
    m_stateChanged.wakeAll();
    return m_state == QCurlRuntimeState::Running;
}

QString RuntimeRegistry::admissionDiagnosticLocked() const
{
    if (!m_diagnostic.isEmpty()) {
        return m_diagnostic;
    }
    switch (m_state) {
        case QCurlRuntimeState::Uninitialized:
            return QStringLiteral("QCurlRuntime: Uninitialized");
        case QCurlRuntimeState::Running:
            return {};
        case QCurlRuntimeState::Stopping:
            return QStringLiteral("QCurlRuntime 已进入 Stopping，拒绝新工作");
        case QCurlRuntimeState::Stopped:
            return QStringLiteral("QCurlRuntime 已进入 Stopped，拒绝重新初始化");
        case QCurlRuntimeState::Failed:
            return QStringLiteral("QCurlRuntime 已进入 Failed，拒绝新工作");
    }
    return QStringLiteral("QCurlRuntime 状态无效");
}

QCurlShutdownResult RuntimeRegistry::failureResultLocked() const noexcept
{
    switch (m_failure) {
        case FailureReason::Poison:
            return QCurlShutdownResult::Poison;
        case FailureReason::ExternalUsersUnverified:
            return QCurlShutdownResult::ExternalUsersUnverified;
        case FailureReason::None:
        case FailureReason::InternalFailure:
            return QCurlShutdownResult::InternalFailure;
    }
    return QCurlShutdownResult::InternalFailure;
}

void RuntimeRegistry::setFailureLocked(FailureReason reason, const QString &diagnostic) noexcept
{
    if (m_state == QCurlRuntimeState::Stopped || m_state == QCurlRuntimeState::Failed) {
        return;
    }
    m_state      = QCurlRuntimeState::Failed;
    m_failure    = reason;
    m_diagnostic = diagnostic;
    m_stateChanged.wakeAll();
}

} // namespace QCurl::Internal
