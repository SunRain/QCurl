/**
 * @file
 * @brief 构造安全缓存回放头并合并 304 重验证结果。
 */

#include "QCNetworkHttpHeaders.h"
#include "private/QCNetworkCacheIntegration_p.h"
#include "private/QCNetworkReplyCache_p.h"

namespace QCurl::Internal {
namespace {

bool cacheReplayHeaderIsForbidden(const QByteArray &name)
{
    const QByteArray normalized = name.trimmed().toLower();
    return normalized == httpheaders::kSetCookie.toLower()
           || normalized == QByteArrayLiteral("set-cookie2")
           || normalized == httpheaders::kAuthorization.toLower()
           || normalized == httpheaders::kProxyAuthorization.toLower();
}

QByteArray cacheResponseHeaderBlock(const QCNetworkCacheMetadata &metadata)
{
    QByteArray block = QByteArrayLiteral("HTTP/1.1 ") + QByteArray::number(metadata.statusCode())
                       + QByteArrayLiteral(" Cached\r\n");
    const QByteArray currentAge = QByteArray::number(metadata.currentAgeSeconds());
    bool ageWritten             = false;
    for (const auto &[name, value] : metadata.rawHeaders()) {
        if (cacheReplayHeaderIsForbidden(name)) {
            continue;
        }
        const bool isAge = QByteArrayView(name).compare(QByteArrayView("age"), Qt::CaseInsensitive)
                           == 0;
        block += name + QByteArrayLiteral(": ") + (isAge ? currentAge : value)
                 + QByteArrayLiteral("\r\n");
        ageWritten = ageWritten || isAge;
    }
    if (!ageWritten) {
        block += QByteArrayLiteral("Age: ") + currentAge + QByteArrayLiteral("\r\n");
    }
    return block + QByteArrayLiteral("\r\n");
}

QByteArray revalidatedHeaderBlock(int status, const QList<RawHeaderPair> &headers)
{
    QByteArray block = QByteArrayLiteral("HTTP/1.1 ") + QByteArray::number(status)
                       + QByteArrayLiteral(" Revalidated\r\n");
    for (const auto &[name, value] : headers) {
        block += name + QByteArrayLiteral(": ") + value + QByteArrayLiteral("\r\n");
    }
    return block + QByteArrayLiteral("\r\n");
}

} // namespace

std::optional<ReplyCacheReplay> QCReplyCache::lookup(const QCNetworkRequest &request,
                                                     HttpMethod method,
                                                     bool managerUsesCookies,
                                                     QCNetworkCache *cache,
                                                     QCNetworkCacheReadMode mode) const
{
    if (!cache) {
        return std::nullopt;
    }
    const auto cached = cache->lookup(requestKey(request, method, managerUsesCookies), mode);
    if (!cached.hit()) {
        return std::nullopt;
    }
    const auto metadata = cached.metadata();
    return ReplyCacheReplay{metadata.statusCode(),
                            cacheResponseHeaderBlock(metadata),
                            method == HttpMethod::Head ? QByteArray() : cached.body()};
}

std::optional<ReplyCacheReplay> QCReplyCache::revalidate(
    HttpMethod method, int status, const QList<RawHeaderPair> &validationHeaders, qint64 durationMs)
{
    if (!m_revalidation || status != 304 || !m_staleEntry.hit()) {
        return std::nullopt;
    }
    const auto cachedMetadata = m_staleEntry.metadata();
    const auto merged = mergeRevalidatedRawHeaders(cachedMetadata.rawHeaders(), validationHeaders);
    const auto metadata = buildCacheMetadata(m_key, cachedMetadata.statusCode(), merged, durationMs);
    m_staleEntry.setMetadata(metadata);
    return ReplyCacheReplay{metadata.statusCode(),
                            revalidatedHeaderBlock(metadata.statusCode(), merged),
                            method == HttpMethod::Get ? std::optional(m_staleEntry.body())
                                                      : std::nullopt};
}

} // namespace QCurl::Internal
