/**
 * @file
 * @brief Internal versioned disk-cache envelope.
 */

#ifndef QCNETWORKDISKCACHEENTRY_P_H
#define QCNETWORKDISKCACHEENTRY_P_H

#include "QCNetworkCache.h"

#include <optional>

namespace QCurl::Internal {

struct QCNetworkDiskCacheEntry
{
    QByteArray primaryDigest;
    QByteArray variantDigest;
    QList<QByteArray> varyHeaderNames;
    QCNetworkCacheMetadata metadata;
    QByteArray body;
};

[[nodiscard]] bool writeDiskCacheEntry(const QString &filePath,
                                       const QCNetworkDiskCacheEntry &entry);

[[nodiscard]] std::optional<QCNetworkDiskCacheEntry> readDiskCacheEntry(const QString &filePath);

/// Removes one cache file while honoring deterministic test failure injection.
[[nodiscard]] bool removeDiskCacheFile(const QString &filePath);

} // namespace QCurl::Internal

#endif // QCNETWORKDISKCACHEENTRY_P_H
