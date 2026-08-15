/**
 * @file
 * @brief 提供测试夹具使用的已校验重试策略构造函数。
 */

#ifndef QCNETWORK_RETRY_POLICY_TEST_HELPER_H
#define QCNETWORK_RETRY_POLICY_TEST_HELPER_H

#include "QCNetworkRetryPolicy.h"

#include <QtGlobal>

#include <chrono>

namespace QCurl::TestSupport {

/**
 * @brief 用已知合法的测试常量创建重试策略。
 * @return 完整配置的策略；任何参数失效都会立即终止当前测试进程。
 */
inline QCNetworkRetryPolicy makeRetryPolicyOrFail(
    int maxRetries,
    std::chrono::milliseconds initialDelay,
    double backoff = 2.0,
    std::chrono::milliseconds maxDelay = std::chrono::milliseconds{30000})
{
    QCNetworkRetryPolicy policy;
    if (QCNetworkRetryPolicy::tryCreate(maxRetries, initialDelay, backoff, &policy)
            != QCNetworkRetryPolicy::UpdateResult::Applied
        || policy.setMaxDelay(maxDelay) != QCNetworkRetryPolicy::UpdateResult::Applied) {
        qFatal("测试重试策略配置无效");
    }
    return policy;
}

} // namespace QCurl::TestSupport

#endif // QCNETWORK_RETRY_POLICY_TEST_HELPER_H
