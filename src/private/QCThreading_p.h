#ifndef QCTHREADING_P_H
#define QCTHREADING_P_H

#include <QAbstractEventDispatcher>
#include <QThread>

namespace QCurl::Internal {

inline bool hasEventDispatcher(QThread *thread) noexcept
{
    return thread != nullptr && QAbstractEventDispatcher::instance(thread) != nullptr;
}

} // namespace QCurl::Internal

#endif // QCTHREADING_P_H
