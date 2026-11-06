/**
 * @file
 * @brief 声明响应头、消费缓冲和有界缓存候选的行为接口。
 */

#ifndef QCNETWORKREPLYRESPONSE_P_H
#define QCNETWORKREPLYRESPONSE_P_H

#include "QCNetworkReply.h"
#include "qbytedata_p.h"

#include <QByteArrayView>
#include <QMap>

#include <chrono>
#include <ctime>
#include <optional>

namespace QCurl::Internal {

/**
 * @brief 统一持有当前 attempt 的响应和已交付事实。
 *
 * 消费缓冲与缓存候选相互独立；原生 completion 的权威状态不被 CONNECT 头重解析覆盖。
 */
class Q_DECL_HIDDEN QCReplyResponse
{
public:
    void appendHeaders(QByteArrayView bytes);
    void parseHeaders();
    void appendBody(QByteArrayView bytes);
    /// 消费现有数据并记录非空交付，不触碰独立的缓存候选。
    [[nodiscard]] QByteArray takeAvailableBody();
    /// 负上限禁用收集；达到容量后仍可继续接收，超限立即释放候选。
    void beginCacheCollection(qint64 limit);
    void releaseCacheCandidate();
    void resetForAttempt();
    /// 提交 completion 或回放的权威 status，后续头解析不得覆盖，包括 status=0。
    void commitStatus(int status);
    void applyReplayHeaders(const QMap<QByteArray, QByteArray> &headers,
                            const std::optional<QByteArray> &rawHeaders);
    void applyResponseHeaders(int status, const QByteArray &headers);
    /// 按回放检查点追加或替换消费数据；候选内容替换，但不重新开启收集资格。
    void applyCachedBody(const QByteArray &body, bool replace);

    [[nodiscard]] int status() const noexcept { return m_status; }
    [[nodiscard]] QByteArray rawHeaderData() const { return m_headerData; }
    [[nodiscard]] QList<RawHeaderPair> rawHeaders() const { return m_finalHeaders; }
    [[nodiscard]] QByteArray rawHeader(const QByteArray &name) const;
    [[nodiscard]] bool hasRawHeader(const QByteArray &name) const;
    [[nodiscard]] QMap<QByteArray, QByteArray> parsedResponseHeaders() const;
    [[nodiscard]] qint64 bytesAvailable() const noexcept { return m_bodyBuffer.byteAmount(); }
    [[nodiscard]] bool bodyDelivered() const noexcept { return m_bodyDelivered; }
    [[nodiscard]] bool cacheCandidateComplete() const noexcept { return m_cacheLimit >= 0; }
    [[nodiscard]] QByteArray cacheCandidate() const { return m_cacheBody; }
    [[nodiscard]] qint64 retainedCacheBodyBytes() const noexcept { return m_cacheBody.size(); }
    [[nodiscard]] bool rangeAlreadyComplete(qint64 existingSize) const;
    /// 以本次查询时钟解释最终块；不存在或无效的 Retry-After 返回 nullopt。
    [[nodiscard]] std::optional<std::chrono::milliseconds> retryAfterDelay(std::time_t now) const;

private:
    QCByteDataBuffer m_bodyBuffer;
    QByteArray m_cacheBody;
    qint64 m_cacheLimit = -1;
    QByteArray m_headerData;
    QList<RawHeaderPair> m_finalHeaders;
    QMap<QByteArray, QByteArray> m_finalHeaderMap;
    int m_status               = 0;
    bool m_authoritativeStatus = false;
    bool m_bodyDelivered       = false;
};

} // namespace QCurl::Internal

#endif // QCNETWORKREPLYRESPONSE_P_H
