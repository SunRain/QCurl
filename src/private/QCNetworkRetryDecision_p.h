/**
 * @file
 * @brief Internal unified retry-safety decision.
 */

#ifndef QCNETWORKRETRYDECISION_P_H
#define QCNETWORKRETRYDECISION_P_H

#include "QCNetworkError.h"
#include "QCNetworkHttpMethod.h"
#include "QCNetworkRequest.h"
#include "QCNetworkRetryPolicy.h"

#include <QString>

namespace QCurl::Internal {

/**
 * @brief The single gate shared by HTTP-status and transport-error retries.
 */
struct QCNetworkRetryDecision
{
    bool allowed = false;
    QString rejectionReason;

    [[nodiscard]] static QCNetworkRetryDecision evaluate(const QCNetworkRetryPolicy &policy,
                                                         HttpMethod method,
                                                         const QCNetworkRequest &request,
                                                         bool bodyReplayable,
                                                         NetworkError error,
                                                         int attemptCount);
};

} // namespace QCurl::Internal

#endif // QCNETWORKRETRYDECISION_P_H
