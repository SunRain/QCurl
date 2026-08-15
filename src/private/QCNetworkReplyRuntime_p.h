/**
 * @file
 * @brief QCNetworkReply retry/capability runtime helpers.
 */

#ifndef QCNETWORKREPLYRUNTIME_P_H
#define QCNETWORKREPLYRUNTIME_P_H

#include "QCNetworkError.h"
#include "private/QCNetworkReplySignal_p.h"

#include <QPointer>
#include <QString>

#include <chrono>
#include <curl/curl.h>
#include <optional>

namespace QCurl {

class QCNetworkReply;
class QCNetworkReplyPrivate;

namespace Internal {

inline constexpr const char kTestCurlPlanDigestProperty[] = "_qcurl_testCurlPlanDigest";

[[nodiscard]] bool isReplyCapabilityRelatedCurlError(CURLcode code) noexcept;
void appendReplyCapabilityWarning(QCNetworkReplyPrivate *reply, const QString &message);

struct ReplyRetryAdvanceResult
{
    SignalEmissionResult emissionResult = SignalEmissionResult::Alive;
    std::optional<std::chrono::milliseconds> delay;
};

[[nodiscard]] ReplyRetryAdvanceResult advanceReplyRetryIfNeeded(QCNetworkReplyPrivate *reply,
                                                                NetworkError error);
void resetReplyForRetry(QCNetworkReplyPrivate *reply, bool setIdleState);
void scheduleAsyncReplyRetry(QPointer<QCNetworkReply> safeReply,
                             QCNetworkReplyPrivate *reply,
                             std::chrono::milliseconds delay);

} // namespace Internal

} // namespace QCurl

#endif // QCNETWORKREPLYRUNTIME_P_H
