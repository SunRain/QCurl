/**
 * @file
 * @brief 声明流控的 owner-thread、信号与 multi 唤醒适配。
 */

#ifndef QCNETWORKREPLYFLOWCONTROLEXECUTION_P_H
#define QCNETWORKREPLYFLOWCONTROLEXECUTION_P_H

#include "QCNetworkReply.h"
#include "private/QCNetworkReplySignal_p.h"

namespace QCurl {
class QCNetworkReplyPrivate;

namespace Internal {
[[nodiscard]] SignalEmissionResult maybeResumeReplyRecvFromBackpressure(QCNetworkReplyPrivate *reply);
[[nodiscard]] SignalEmissionResult resumeReplySendFromRequestBodySourceIfNeeded(
    QCNetworkReplyPrivate *reply);
void scheduleReplyBackpressureResumeAfterRead(QCNetworkReply *reply,
                                              QCNetworkReplyPrivate *privateReply);
[[nodiscard]] SignalEmissionResult clearReplyFlowControlOnTerminalState(QCNetworkReplyPrivate *reply);
void pauseReplyTransport(QCNetworkReply *reply, QCNetworkReplyPrivate *privateReply, PauseMode mode);
void resumeReplyTransport(QCNetworkReply *reply, QCNetworkReplyPrivate *privateReply);
} // namespace Internal
} // namespace QCurl

#endif // QCNETWORKREPLYFLOWCONTROLEXECUTION_P_H
