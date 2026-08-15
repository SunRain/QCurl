#include "QCurlRuntime.h"

#include "private/QCurlRuntimeState_p.h"

#include <QThread>

namespace QCurl {

/// 保存进程 runtime controller 的构造线程和唯一 owner 身份。
class QCurlRuntimePrivate
{
public:
    QThread *ownerThread = QThread::currentThread();
    bool processOwner    = false;
};

QCurlRuntime::QCurlRuntime()
    : d_ptr(new QCurlRuntimePrivate)
{
    d_ptr->processOwner = Internal::RuntimeRegistry::instance().registerController(this);
}

QCurlRuntime::~QCurlRuntime()
{
    if (d_ptr->processOwner) {
        Internal::RuntimeRegistry::instance().unregisterController(this);
    }
}

bool QCurlRuntime::isProcessOwner() const noexcept
{
    return d_ptr->processOwner;
}

QCurlRuntimeState QCurlRuntime::state() const noexcept
{
    return Internal::RuntimeRegistry::instance().state();
}

QString QCurlRuntime::diagnostic() const
{
    return Internal::RuntimeRegistry::instance().diagnostic();
}

bool QCurlRuntime::confirmExternalUsersStopped()
{
    if (!d_ptr->processOwner || QThread::currentThread() != d_ptr->ownerThread) {
        return false;
    }
    return Internal::RuntimeRegistry::instance().confirmExternalUsersStopped(this);
}

QCurlShutdownStartResult QCurlRuntime::beginShutdown()
{
    if (!d_ptr->processOwner) {
        return QCurlShutdownStartResult::InternalFailure;
    }
    if (QThread::currentThread() != d_ptr->ownerThread) {
        return QCurlShutdownStartResult::WrongThread;
    }
    return Internal::RuntimeRegistry::instance().beginShutdown(this);
}

QCurlShutdownResult QCurlRuntime::shutdownAndWait(QDeadlineTimer deadline)
{
    if (!d_ptr->processOwner) {
        return QCurlShutdownResult::InternalFailure;
    }
    if (QThread::currentThread() == d_ptr->ownerThread) {
        return QCurlShutdownResult::WrongThread;
    }
    return Internal::RuntimeRegistry::instance().shutdownAndWait(this, deadline);
}

} // namespace QCurl
