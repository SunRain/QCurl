/**
 * @file
 * @brief 封装请求级重试资格、预算与成功恢复后的提交。
 */

#ifndef QCREPLYRETRY_P_H
#define QCREPLYRETRY_P_H

#include "QCNetworkHttpMethod.h"
#include "QCNetworkRequest.h"
#include "QCNetworkRetryPolicy.h"

#include <chrono>
#include <optional>

namespace QCurl::Internal {

/** @brief 当前 attempt 的窄事实，不借用其他领域对象。 */
struct ReplyRetryAttemptFacts
{
    NetworkError error    = NetworkError::NoError;
    bool bodyReplayable   = true;
    bool bodyDelivered    = false;
    bool outputRestorable = false;
    std::chrono::milliseconds elapsed{0};
    std::optional<std::chrono::milliseconds> retryAfter;
};

/** @brief owner thread 内即时使用一次的重试提案；提交前由编排器恢复输出。 */
struct ReplyRetryProposal
{
    std::chrono::milliseconds delay{0};
};

/** @brief 不执行输出 I/O、信号或定时器的请求级重试模块。 */
class Q_DECL_HIDDEN QCReplyRetry
{
public:
    /// 评估不改变预算；拒绝时调用方不得恢复输出。
    [[nodiscard]] std::optional<ReplyRetryProposal> evaluate(
        const QCNetworkRetryPolicy &policy,
        HttpMethod method,
        const QCNetworkRequest &request,
        const ReplyRetryAttemptFacts &facts) const;
    /// 仅在提案允许且输出恢复成功后提交计数与饱和累计等待。
    void commit(const ReplyRetryProposal &proposal);

    [[nodiscard]] int attemptCount() const noexcept { return m_attemptCount; }
    [[nodiscard]] std::chrono::milliseconds cumulativeDelay() const noexcept
    {
        return m_cumulativeDelay;
    }

private:
    int m_attemptCount = 0;
    std::chrono::milliseconds m_cumulativeDelay{0};
};

} // namespace QCurl::Internal

#endif // QCREPLYRETRY_P_H
