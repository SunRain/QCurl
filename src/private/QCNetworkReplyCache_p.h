/**
 * @file
 * @brief Internal RFC 9111 cache integration for QCNetworkReply.
 */

#ifndef QCNETWORKREPLYCACHE_P_H
#define QCNETWORKREPLYCACHE_P_H

#include "private/QCNetworkReplySignal_p.h"

namespace QCurl {

class QCNetworkReplyPrivate;
class QCNetworkAccessManager;

namespace Internal {

[[nodiscard]] SignalEmissionResult restoreRevalidatedCacheResponse(QCNetworkReplyPrivate *reply);
/// 在每次网络尝试前按现有缓存容量确定候选收集上限。
void prepareReplyCacheCollection(QCNetworkReplyPrivate *reply, QCNetworkAccessManager *manager);
void storeReplyInCache(QCNetworkReplyPrivate *reply);

} // namespace Internal
} // namespace QCurl

#endif // QCNETWORKREPLYCACHE_P_H
