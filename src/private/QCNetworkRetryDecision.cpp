/**
 * @file
 * @brief Unified retry method, body, error, and attempt gate.
 */

#include "QCNetworkHttpHeaders.h"
#include "private/QCNetworkRetryDecision_p.h"

namespace QCurl::Internal {
namespace {

[[nodiscard]] bool hasStableIdempotencyKey(const QCNetworkRequest &request)
{
    QByteArray key;
    bool found = false;
    for (const QByteArray &name : request.rawHeaderList()) {
        if (name.compare(QCurl::httpheaders::kIdempotencyKey, Qt::CaseInsensitive) != 0) {
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

[[nodiscard]] bool methodIsDefaultReplayable(HttpMethod method) noexcept
{
    return method == HttpMethod::Get || method == HttpMethod::Head;
}

} // namespace

QCNetworkRetryDecision QCNetworkRetryDecision::evaluate(const QCNetworkRetryPolicy &policy,
                                                        HttpMethod method,
                                                        const QCNetworkRequest &request,
                                                        bool bodyReplayable,
                                                        NetworkError error,
                                                        int attemptCount)
{
    QCNetworkRetryDecision decision;
    if (!policy.isEnabled()) {
        decision.rejectionReason = QStringLiteral("retry policy is disabled");
        return decision;
    }
    if (!bodyReplayable) {
        decision.rejectionReason = QStringLiteral("request body is not replayable");
        return decision;
    }
    if (!methodIsDefaultReplayable(method)
        && (policy.retryMethodPolicy() != QCNetworkRetryMethodPolicy::AllowExplicitIdempotencyKey
            || !hasStableIdempotencyKey(request))) {
        decision.rejectionReason = QStringLiteral(
            "non-idempotent retry requires explicit policy and stable Idempotency-Key");
        return decision;
    }
    if (!policy.retryableErrors().contains(error)) {
        decision.rejectionReason = QStringLiteral("error is not retryable");
        return decision;
    }
    if (attemptCount < 0 || attemptCount >= policy.maxRetries()) {
        decision.rejectionReason = QStringLiteral("retry limit reached");
        return decision;
    }

    decision.allowed = true;
    return decision;
}

} // namespace QCurl::Internal
