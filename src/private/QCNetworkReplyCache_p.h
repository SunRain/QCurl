/**
 * @file
 * @brief 封装请求级缓存准备、读取、回退、重验证与存储规则。
 */

#ifndef QCNETWORKREPLYCACHE_P_H
#define QCNETWORKREPLYCACHE_P_H

#include "QCNetworkCache.h"
#include "QCNetworkError.h"
#include "QCNetworkRequest.h"

#include <optional>

namespace QCurl::Internal {

struct Q_DECL_HIDDEN ReplyCachePreparation;

/** @brief 执行检查点的读取模式与未命中处理，不发布 Qt 事件。 */
struct ReplyCacheExecutionDecision
{
    std::optional<QCNetworkCacheReadMode> readMode;
    bool requireHit = false;
};

/** @brief 缓存回放数据；HEAD 重验证不替换响应体。 */
struct ReplyCacheReplay
{
    int status = 0;
    QByteArray rawHeaders;
    std::optional<QByteArray> body;
};

/** @brief 当前网络响应是否已经对消费者可见的窄事实。 */
struct ReplyCacheNetworkFacts
{
    bool hasHeaders               = false;
    qint64 downloadedBytes        = 0;
    qint64 availableBytes         = 0;
    qint64 retainedCandidateBytes = 0;
};

/** @brief 终态存储检查点的响应、候选完整性与耗时。 */
struct ReplyCacheStoreFacts
{
    NetworkError error = NetworkError::NoError;
    int status         = 0;
    QList<RawHeaderPair> rawHeaders;
    QMap<QByteArray, QByteArray> parsedHeaders;
    bool candidateComplete = false;
    QByteArray candidate;
    qint64 durationMs = 0;
};

/**
 * @brief 持有请求上下文而不持有缓存服务；每个操作只借用当次 cache。
 *
 * manager 构造前准备的 key/validator 与 Reply 的执行、回退和存储使用同一上下文。
 */
class Q_DECL_HIDDEN QCReplyCache
{
public:
    /// 在原构造前检查点准备条件请求，返回可一次性移交给 Reply 的上下文。
    [[nodiscard]] static ReplyCachePreparation prepareRequest(const QCNetworkRequest &request,
                                                              HttpMethod method,
                                                              bool managerUsesCookies,
                                                              QCNetworkCache *cache);
    [[nodiscard]] ReplyCacheExecutionDecision lookupForExecution(const QCNetworkRequest &request,
                                                                 bool cacheAvailable);
    [[nodiscard]] std::optional<ReplyCacheReplay> lookup(const QCNetworkRequest &request,
                                                         HttpMethod method,
                                                         bool managerUsesCookies,
                                                         QCNetworkCache *cache,
                                                         QCNetworkCacheReadMode mode) const;
    [[nodiscard]] qint64 collectionLimit(const QCNetworkRequest &request,
                                         HttpMethod method,
                                         bool managerUsesCookies,
                                         const QCNetworkCache *cache) const;
    /// 已有任一响应事实时不允许回退；一次可尝试回退会消费 fallback 资格。
    [[nodiscard]] bool allowFallback(const ReplyCacheNetworkFacts &facts);
    [[nodiscard]] std::optional<ReplyCacheReplay> revalidate(
        HttpMethod method,
        int status,
        const QList<RawHeaderPair> &validationHeaders,
        qint64 durationMs);
    void store(const QCNetworkRequest &request,
               HttpMethod method,
               bool managerUsesCookies,
               QCNetworkCache *cache,
               const ReplyCacheStoreFacts &facts) const;

private:
    [[nodiscard]] QCNetworkCacheRequestKey requestKey(const QCNetworkRequest &request,
                                                      HttpMethod method,
                                                      bool managerUsesCookies) const;
    void storeGet(QCNetworkCache *cache,
                  const QCNetworkCacheRequestKey &key,
                  const ReplyCacheStoreFacts &facts) const;
    void updateGetFromHead(QCNetworkCache *cache,
                           const QCNetworkCacheRequestKey &key,
                           const ReplyCacheStoreFacts &facts) const;

    QCNetworkCacheRequestKey m_key;
    QCNetworkCacheLookupResult m_staleEntry;
    bool m_keyInitialized = false;
    bool m_revalidation   = false;
    bool m_fallback       = false;
};

/** @brief 工厂的有效请求和单个请求级缓存上下文。 */
struct Q_DECL_HIDDEN ReplyCachePreparation
{
    QCNetworkRequest request;
    QCReplyCache cache;
};

} // namespace QCurl::Internal

#endif // QCNETWORKREPLYCACHE_P_H
