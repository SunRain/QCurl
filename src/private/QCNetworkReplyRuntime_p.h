/**
 * @file
 * @brief QCNetworkReply retry/capability runtime helpers.
 */

#ifndef QCNETWORKREPLYRUNTIME_P_H
#define QCNETWORKREPLYRUNTIME_P_H

#include "QCNetworkError.h"

#include <QPointer>
#include <QString>

#include <curl/curl.h>

#include <chrono>
#include <optional>

namespace QCurl {

class QCNetworkReply;
class QCNetworkReplyPrivate;

namespace Internal {

inline constexpr const char kTestCurlPlanDigestProperty[] = "_qcurl_testCurlPlanDigest";

[[nodiscard]] bool isReplyCapabilityRelatedCurlError(CURLcode code) noexcept;
void appendReplyCapabilityWarning(QCNetworkReplyPrivate *reply, const QString &message);

[[nodiscard]] std::optional<std::chrono::milliseconds> advanceReplyRetryIfNeeded(
    QCNetworkReplyPrivate *reply, NetworkError error);
void resetReplyForRetry(QCNetworkReplyPrivate *reply, bool setIdleState);
void scheduleAsyncReplyRetry(QPointer<QCNetworkReply> safeReply,
                             QCNetworkReplyPrivate *reply,
                             std::chrono::milliseconds delay);

} // namespace Internal

} // namespace QCurl

#endif // QCNETWORKREPLYRUNTIME_P_H
