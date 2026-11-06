/**
 * @file
 * @brief 分离响应消费与有界候选收集，显式重置当前 attempt。
 */

#include "private/QCNetworkReplyResponse_p.h"

namespace QCurl::Internal {

void QCReplyResponse::appendBody(QByteArrayView bytes)
{
    const QByteArray chunk(bytes.data(), bytes.size());
    m_bodyBuffer.append(chunk);
    if (m_cacheLimit >= 0) {
        if (chunk.size() <= m_cacheLimit - m_cacheBody.size()) {
            m_cacheBody.append(chunk);
        } else {
            releaseCacheCandidate();
        }
    }
}

QByteArray QCReplyResponse::takeAvailableBody()
{
    QByteArray body = m_bodyBuffer.readAll();
    if (!body.isEmpty()) {
        m_bodyDelivered = true;
    }
    return body;
}

void QCReplyResponse::beginCacheCollection(qint64 limit)
{
    m_cacheBody  = QByteArray();
    m_cacheLimit = limit;
}

void QCReplyResponse::releaseCacheCandidate()
{
    m_cacheBody  = QByteArray();
    m_cacheLimit = -1;
}

void QCReplyResponse::resetForAttempt()
{
    m_bodyBuffer.clear();
    releaseCacheCandidate();
    m_headerData.clear();
    m_finalHeaders.clear();
    m_finalHeaderMap.clear();
    m_status              = 0;
    m_authoritativeStatus = false;
    m_bodyDelivered       = false;
}

void QCReplyResponse::applyCachedBody(const QByteArray &body, bool replace)
{
    if (replace) {
        m_bodyBuffer.clear();
    }
    m_bodyBuffer.append(body);
    // 缓存回放的候选只替换内容，不重新决定收集资格；资格始终来自请求级缓存规则。
    m_cacheBody = body;
}

} // namespace QCurl::Internal
