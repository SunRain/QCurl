/**
 * @file
 * @brief Internal request/cache contract helpers.
 */

#ifndef QCNETWORKCACHEINTEGRATION_P_H
#define QCNETWORKCACHEINTEGRATION_P_H

#include "QCNetworkCache.h"
#include "QCNetworkRequest.h"

namespace QCurl::Internal {

/// 请求指令是否要求绕过未验证的缓存响应。
[[nodiscard]] bool requestRequiresCacheRevalidation(const QCNetworkRequest &request);

/// 请求指令是否禁止存储本次网络响应。
[[nodiscard]] bool requestForbidsCacheStorage(const QCNetworkRequest &request);

[[nodiscard]] bool requestHasAuthenticationContext(const QCNetworkRequest &request,
                                                   bool managerUsesCookies);

[[nodiscard]] QCNetworkCacheRequestKey buildCacheRequestKey(const QCNetworkRequest &request,
                                                            HttpMethod method,
                                                            bool managerUsesCookies);

[[nodiscard]] Q_DECL_HIDDEN QCNetworkCacheMetadata
buildCacheMetadata(const QCNetworkCacheRequestKey &key,
                   int statusCode,
                   const QList<RawHeaderPair> &rawResponseHeaders,
                   qint64 responseDelayMs = 0);

/// ordered raw response headers 是 cache admission 的唯一输入事实。
[[nodiscard]] Q_DECL_HIDDEN bool responseHeadersAreCacheable(
    const QList<RawHeaderPair> &rawResponseHeaders);

[[nodiscard]] bool cacheMetadataHasValidator(const QCNetworkCacheMetadata &metadata);

[[nodiscard]] QCNetworkRequest requestWithCacheValidators(const QCNetworkRequest &request,
                                                          const QCNetworkCacheMetadata &metadata);

[[nodiscard]] QList<RawHeaderPair> mergeRevalidatedRawHeaders(
    const QList<RawHeaderPair> &cachedHeaders,
    const QList<RawHeaderPair> &validationHeaders);

} // namespace QCurl::Internal

#endif // QCNETWORKCACHEINTEGRATION_P_H
