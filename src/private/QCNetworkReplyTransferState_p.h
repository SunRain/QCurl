/**
 * @file
 * @brief 保存 reply、活动 record 与 detached handle 共享的异步传输状态。
 */

#ifndef QCNETWORKREPLYTRANSFERSTATE_P_H
#define QCNETWORKREPLYTRANSFERSTATE_P_H

#include "QCNetworkLogger.h"
#include "QCNetworkReply.h"
#include "private/QCNetworkReceiveIdleExecution_p.h"
#include "private/QCNetworkReplyBodySource_p.h"
#include "private/QCNetworkReplyFlowControl_p.h"
#include "private/QCNetworkReplyResponse_p.h"
#include "private/QCNetworkReplyTrace_p.h"
#include "private/QCReplyCurlConfiguration_p.h"
#include "private/QCReplyLifecycle_p.h"
#include "private/QCTlsConfiguration_p.h"

#include <curl/curl.h>
#include <memory>
#include <utility>

namespace QCurl {

struct QCCurlMultiDomain;

/**
 * @brief 保存活动 easy handle 借用的回调数据与选项存储。
 *
 * Reply 与活动 transfer record 共享本状态；detached handle 守卫继续持有同一状态直到
 * cleanup，防止 queued completion 被丢弃时先释放仍被 libcurl 借用的列表或请求体。
 */
struct Q_DECL_HIDDEN QCNetworkReplyTransferState
{
    explicit QCNetworkReplyTransferState(Internal::CurlPlan plan)
        : configuration(std::move(plan))
        , flow(configuration.plan().normalized.request.backpressureLimitBytes(),
               configuration.plan().normalized.request.backpressureResumeBytes())
        , trace(configuration.plan().normalized.request.url(),
                configuration.plan().normalized.request.diagnosticQueryPolicy())
    {}

    Internal::QCReplyCurlConfiguration configuration;
    // 重试等待期间固定原域；终态归还。活动 record 另持有引用直到原生 detach 成功。
    std::shared_ptr<QCCurlMultiDomain> transportDomain;
    // 仅在 add 提交后、detach 成功前于 owner thread 或允许的 libcurl 回调内借用。
    CURL *activeEasyHandle       = nullptr;
    bool readCallbackConfigured  = false;
    bool debugCallbackConfigured = false;

    Internal::QCNetworkReceiveIdleExecution receiveIdle;
    Internal::QCReplyLifecycle lifecycle;
    Internal::QCReplyResponse response;

    Internal::QCReplyFlowControl flow;
    Internal::QCReplyBodySource bodySource;

    QCNetworkLoggerHandle logger;
    bool debugTraceEnabled = false;
    Internal::ReplyTraceState trace;

    Internal::QCTlsConfiguration tlsConfiguration;
};

} // namespace QCurl

#endif // QCNETWORKREPLYTRANSFERSTATE_P_H
