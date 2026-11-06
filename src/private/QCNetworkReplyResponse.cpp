/**
 * @file
 * @brief 统一解析原始头和最终响应块，不覆盖已提交的权威状态码。
 */

#include "QCNetworkHttpHeaders.h"
#include "private/QCNetworkReplyResponse_p.h"

namespace QCurl::Internal {
namespace {

QByteArray joinedSegments(const QList<QByteArray> &segments)
{
    QByteArray value;
    for (const QByteArray &segment : segments) {
        const QByteArray part = segment.trimmed();
        if (!part.isEmpty()) {
            if (!value.isEmpty()) {
                value.append(' ');
            }
            value.append(part);
        }
    }
    return value;
}

void flushHeader(QList<RawHeaderPair> &headers,
                 const QByteArray &name,
                 const QList<QByteArray> &segments)
{
    if (!name.isEmpty()) {
        headers.append({name, joinedSegments(segments)});
    }
}

void parseStatusLine(const QByteArray &line, int *status)
{
    if (!status) {
        return;
    }
    const auto parts = line.split(' ');
    if (parts.size() >= 2) {
        bool ok          = false;
        const int parsed = parts.at(1).trimmed().toInt(&ok);
        if (ok) {
            *status = parsed;
        }
    }
}

QList<RawHeaderPair> parseHeaderPairs(const QByteArray &data, bool finalOnly, int *status)
{
    QList<RawHeaderPair> headers;
    QByteArray name;
    QList<QByteArray> segments;
    for (QByteArray line : data.split('\n')) {
        if (line.endsWith('\r')) {
            line.chop(1);
        }
        if (line.isEmpty() || line.startsWith("HTTP/")) {
            flushHeader(headers, name, segments);
            name.clear();
            segments.clear();
            if (line.startsWith("HTTP/")) {
                if (finalOnly) {
                    headers.clear();
                }
                parseStatusLine(line, status);
            }
            continue;
        }
        if (!name.isEmpty() && (line.startsWith(' ') || line.startsWith('\t'))) {
            segments.append(line);
            continue;
        }
        const qsizetype colon = line.indexOf(':');
        if (colon > 0) {
            flushHeader(headers, name, segments);
            name     = line.first(colon).trimmed();
            segments = {line.sliced(colon + 1)};
        }
    }
    flushHeader(headers, name, segments);
    return headers;
}

} // namespace

void QCReplyResponse::appendHeaders(QByteArrayView bytes)
{
    m_headerData.append(bytes.data(), bytes.size());
    if (m_headerData.endsWith("\r\n\r\n") || m_headerData.endsWith("\n\n")) {
        parseHeaders();
    }
}

void QCReplyResponse::parseHeaders()
{
    int parsedStatus = m_status;
    m_finalHeaders   = parseHeaderPairs(m_headerData, true, &parsedStatus);
    m_finalHeaderMap.clear();
    for (const auto &[name, value] : m_finalHeaders) {
        m_finalHeaderMap.insert(name.trimmed().toLower(), value);
    }
    // CONNECT 拒绝时 RESPONSE_CODE=0；原始头的 403 不能替换 completion 的事实。
    if (!m_authoritativeStatus) {
        m_status = parsedStatus;
    }
}

void QCReplyResponse::commitStatus(int status)
{
    m_status              = status;
    m_authoritativeStatus = true;
}

QByteArray QCReplyResponse::rawHeader(const QByteArray &name) const
{
    return m_finalHeaderMap.value(name.trimmed().toLower());
}

bool QCReplyResponse::hasRawHeader(const QByteArray &name) const
{
    return m_finalHeaderMap.contains(name.trimmed().toLower());
}

QMap<QByteArray, QByteArray> QCReplyResponse::parsedResponseHeaders() const
{
    QMap<QByteArray, QByteArray> headers;
    // 此查询保留旧接口对所有原始块及字段名的语义，最终块查询另由 rawHeaders 提供。
    for (const auto &[name, value] : parseHeaderPairs(m_headerData, false, nullptr)) {
        headers.insert(name, value);
    }
    return headers;
}

bool QCReplyResponse::rangeAlreadyComplete(qint64 existingSize) const
{
    const QByteArray value  = rawHeader(httpheaders::kContentRange).trimmed();
    const QByteArray prefix = QByteArrayLiteral("bytes */");
    if (existingSize < 0 || !value.startsWith(prefix)) {
        return false;
    }
    bool ok            = false;
    const qint64 total = value.sliced(prefix.size()).toLongLong(&ok);
    return ok && total >= 0 && total == existingSize;
}

void QCReplyResponse::applyReplayHeaders(const QMap<QByteArray, QByteArray> &headers,
                                         const std::optional<QByteArray> &rawHeaders)
{
    if (rawHeaders) {
        m_headerData = *rawHeaders;
        return;
    }
    for (auto it = headers.cbegin(); it != headers.cend(); ++it) {
        m_headerData.append(it.key());
        m_headerData.append(": ");
        m_headerData.append(it.value());
        m_headerData.append('\n');
    }
}

void QCReplyResponse::applyResponseHeaders(int status, const QByteArray &headers)
{
    commitStatus(status);
    m_headerData = headers;
    parseHeaders();
}

} // namespace QCurl::Internal
