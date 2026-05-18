/**
 * @file
 * @brief QCNetworkReply 传输 pause/backpressure 状态辅助。
 */

#ifndef QCNETWORKREPLYFLOWCONTROL_P_H
#define QCNETWORKREPLYFLOWCONTROL_P_H

#include "QCNetworkReply.h"

namespace QCurl {

class QCNetworkReplyPrivate;

namespace Internal {

[[nodiscard]] int desiredReplyPauseMask(const QCNetworkReplyPrivate *reply) noexcept;
[[nodiscard]] bool applyReplyPauseMask(QCNetworkReplyPrivate *reply, int desiredMask);

void setReplyBackpressureActive(QCNetworkReplyPrivate *reply, bool active);
void setReplyUploadSendPaused(QCNetworkReplyPrivate *reply, bool paused);
void maybeResumeReplyRecvFromBackpressure(QCNetworkReplyPrivate *reply);
void resumeReplySendFromRequestBodySourceIfNeeded(QCNetworkReplyPrivate *reply);
void scheduleReplyBackpressureResumeAfterRead(QCNetworkReply *reply,
                                              QCNetworkReplyPrivate *privateReply);
void clearReplyFlowControlOnTerminalState(QCNetworkReplyPrivate *reply);

void pauseReplyTransport(QCNetworkReply *reply, QCNetworkReplyPrivate *privateReply, PauseMode mode);
void resumeReplyTransport(QCNetworkReply *reply, QCNetworkReplyPrivate *privateReply);

} // namespace Internal

} // namespace QCurl

#endif // QCNETWORKREPLYFLOWCONTROL_P_H
