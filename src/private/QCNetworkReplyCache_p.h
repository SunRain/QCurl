/**
 * @file
 * @brief Internal RFC 9111 cache integration for QCNetworkReply.
 */

#ifndef QCNETWORKREPLYCACHE_P_H
#define QCNETWORKREPLYCACHE_P_H

#include "private/QCNetworkReplySignal_p.h"

namespace QCurl {

class QCNetworkReplyPrivate;

namespace Internal {

[[nodiscard]] SignalEmissionResult restoreRevalidatedCacheResponse(QCNetworkReplyPrivate *reply);
void storeReplyInCache(QCNetworkReplyPrivate *reply);

} // namespace Internal
} // namespace QCurl

#endif // QCNETWORKREPLYCACHE_P_H
