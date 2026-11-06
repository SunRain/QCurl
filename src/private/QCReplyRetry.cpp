/**
 * @file
 * @brief 实现重试资格、预算边界与计数提交。
 */

#include "QCNetworkHttpHeaders.h"
#include "private/QCReplyRetry_p.h"

#include <algorithm>

namespace QCurl::Internal {
namespace {

bool hasStableIdempotencyKey(const QCNetworkRequest &request)
{
    QByteArray key;
    bool found = false;
    for (const QByteArray &name : request.rawHeaderList()) {
        if (name.compare(httpheaders::kIdempotencyKey, Qt::CaseInsensitive) != 0) {
            continue;
        }
        if (found) {
            return false;
        }
        found = true;
        key   = request.rawHeader(name).trimmed();
    }
    return found && !key.isEmpty() && !key.contains('\r') && !key.contains('\n');
}

bool exceedsBudget(std::chrono::milliseconds used,
                   std::chrono::milliseconds next,
                   std::optional<std::chrono::milliseconds> limit)
{
    return limit && (used > *limit || next > *limit - used);
}

} // namespace

std::optional<ReplyRetryProposal> QCReplyRetry::evaluate(const QCNetworkRetryPolicy &policy,
                                                         HttpMethod method,
                                                         const QCNetworkRequest &request,
                                                         const ReplyRetryAttemptFacts &facts) const
{
    if (!policy.shouldRetry(facts.error, m_attemptCount) || !facts.bodyReplayable
        || (facts.bodyDelivered && !facts.outputRestorable)) {
        return std::nullopt;
    }
    if (method != HttpMethod::Get && method != HttpMethod::Head
        && (policy.retryMethodPolicy() != QCNetworkRetryMethodPolicy::AllowExplicitIdempotencyKey
            || !hasStableIdempotencyKey(request))) {
        return std::nullopt;
    }
    const auto delay = policy.delayForAttempt(m_attemptCount, facts.retryAfter);
    if (exceedsBudget(m_cumulativeDelay, delay, policy.maxCumulativeDelay())
        || exceedsBudget(facts.elapsed, delay, policy.maxElapsedTime())) {
        return std::nullopt;
    }
    return ReplyRetryProposal{delay};
}

void QCReplyRetry::commit(const ReplyRetryProposal &proposal)
{
    ++m_attemptCount;
    const auto remaining = std::chrono::milliseconds::max() - m_cumulativeDelay;
    m_cumulativeDelay += std::min(proposal.delay, remaining);
}

} // namespace QCurl::Internal
