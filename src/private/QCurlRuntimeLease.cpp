#include "private/QCurlRuntimeState_p.h"

#include <utility>

namespace QCurl::Internal {

RuntimeLease::RuntimeLease(RuntimeRegistry *state, QString diagnostic)
    : m_state(state)
    , m_diagnostic(std::move(diagnostic))
{}

RuntimeLease::~RuntimeLease()
{
    reset();
}

RuntimeLease::RuntimeLease(RuntimeLease &&other) noexcept
    : m_state(std::exchange(other.m_state, nullptr))
    , m_diagnostic(std::move(other.m_diagnostic))
{}

RuntimeLease &RuntimeLease::operator=(RuntimeLease &&other) noexcept
{
    if (this == &other) {
        return *this;
    }
    reset();
    m_state      = std::exchange(other.m_state, nullptr);
    m_diagnostic = std::move(other.m_diagnostic);
    return *this;
}

void RuntimeLease::reset() noexcept
{
    if (!m_state) {
        return;
    }
    RuntimeRegistry *state = std::exchange(m_state, nullptr);
    state->releaseLease();
}

RuntimeLease acquireRuntimeLease()
{
    return RuntimeRegistry::instance().acquireLease();
}

QString runtimeDiagnostic()
{
    return RuntimeRegistry::instance().diagnostic();
}

quint64 registerRuntimeShutdownParticipant(QObject *object, std::function<void()> shutdown)
{
    return RuntimeRegistry::instance().registerShutdownParticipant(object, std::move(shutdown));
}

void unregisterRuntimeShutdownParticipant(quint64 token) noexcept
{
    RuntimeRegistry::instance().unregisterShutdownParticipant(token);
}

void markRuntimePoisoned(const QString &diagnostic) noexcept
{
    RuntimeRegistry::instance().markPoisoned(diagnostic);
}

void markRuntimeInternalFailure(const QString &diagnostic) noexcept
{
    RuntimeRegistry::instance().markInternalFailure(diagnostic);
}

#ifdef QCURL_ENABLE_TEST_HOOKS
int runtimeCleanupCountForTest() noexcept
{
    return RuntimeRegistry::instance().cleanupCountForTest();
}

void markRuntimePoisonedForTest(const QString &diagnostic) noexcept
{
    markRuntimePoisoned(diagnostic);
}
#endif

} // namespace QCurl::Internal
