/**
 * @file
 * @brief 以查询时钟解释最终响应头中的 Retry-After。
 */

#include "QCNetworkHttpHeaders.h"
#include "private/QCNetworkReplyResponse_p.h"

#include <QDebug>

#include <cmath>
#include <curl/curl.h>
#include <limits>

namespace QCurl::Internal {
namespace {

std::optional<std::chrono::milliseconds> secondsToDelay(qint64 seconds)
{
    if (seconds < 0) {
        qWarning() << "QCurl ignored negative Retry-After header";
        return std::nullopt;
    }
    if (seconds > std::numeric_limits<qint64>::max() / 1000) {
        qWarning() << "QCurl ignored overflowing Retry-After header";
        return std::nullopt;
    }
    return std::chrono::milliseconds(seconds * 1000);
}

std::optional<std::chrono::milliseconds> dateToDelay(const QString &raw, std::time_t now)
{
    const QByteArray bytes   = raw.toUtf8();
    const std::time_t parsed = curl_getdate(bytes.constData(), nullptr);
    if (parsed < 0) {
        qWarning() << "QCurl ignored invalid Retry-After HTTP-date";
        return std::nullopt;
    }
    if (parsed <= now) {
        return std::chrono::milliseconds(0);
    }
    const long double delta = static_cast<long double>(parsed) - static_cast<long double>(now);
    if (!std::isfinite(delta)
        || delta > static_cast<long double>(std::numeric_limits<qint64>::max()) / 1000.0L) {
        qWarning() << "QCurl ignored overflowing Retry-After HTTP-date";
        return std::nullopt;
    }
    return std::chrono::milliseconds(static_cast<qint64>(delta * 1000.0L));
}

} // namespace

std::optional<std::chrono::milliseconds> QCReplyResponse::retryAfterDelay(std::time_t now) const
{
    for (const auto &[name, value] : m_finalHeaders) {
        if (name.compare(httpheaders::kRetryAfter, Qt::CaseInsensitive) != 0) {
            continue;
        }
        const QString raw = QString::fromUtf8(value).trimmed();
        if (raw.isEmpty()) {
            qWarning() << "QCurl ignored empty Retry-After header";
            return std::nullopt;
        }
        bool ok              = false;
        const qint64 seconds = raw.toLongLong(&ok);
        return ok ? secondsToDelay(seconds) : dateToDelay(raw, now);
    }
    return std::nullopt;
}

} // namespace QCurl::Internal
