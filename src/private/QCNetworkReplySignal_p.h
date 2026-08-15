/**
 * @file
 * @brief Synchronous signal emission guard for QCNetworkReply.
 */

#ifndef QCNETWORKREPLYSIGNAL_P_H
#define QCNETWORKREPLYSIGNAL_P_H

#include "QCNetworkReply.h"

#include <QPointer>

#include <utility>

namespace QCurl::Internal {

enum class SignalEmissionResult {
    Alive,
    Destroyed,
};

template<typename Emit>
[[nodiscard]] SignalEmissionResult emitReplySignal(const QPointer<QCNetworkReply> &observer,
                                                   Emit &&emitter)
{
    if (!observer) {
        return SignalEmissionResult::Destroyed;
    }

    std::forward<Emit>(emitter)(observer.data());
    return observer ? SignalEmissionResult::Alive : SignalEmissionResult::Destroyed;
}

} // namespace QCurl::Internal

#endif // QCNETWORKREPLYSIGNAL_P_H
