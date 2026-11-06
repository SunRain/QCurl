/**
 * @file
 * @brief 声明网络 reply 的内部执行与完成编排接口。
 */

#ifndef QCNETWORKREPLYEXECUTION_P_H
#define QCNETWORKREPLYEXECUTION_P_H

#include "QCCurlMultiManager.h"
#include "private/QCNetworkReplySignal_p.h"

namespace QCurl {

class QCNetworkAccessManager;
class QCNetworkReply;

namespace Internal {

class QCNetworkReplyExecution final
{
public:
    static void run(QCNetworkReply *reply);
    /// 在 reply owner thread 创建弱观察 adapter；完成投递负责回收 handle 而非延长 QObject 寿命。
    [[nodiscard]] static QCCurlMultiManager::TransferCompletionHandler completionHandler(
        QCNetworkReply *reply);
    /// 在 reply owner thread 消费 admission；安全失败先恢复 handle，再记录 warning 并排队报错。
    static void consumeAdmission(QCNetworkReply *reply,
                                 QCCurlMultiManager::HttpAdmissionResult result);
    [[nodiscard]] static bool tryPreferNetworkCacheFallback(QCNetworkReply *reply);
    /// 在原缓存检查点借用 manager 的当前服务，跨领域的应用与信号仍留在 adapter。
    static void prepareCacheCollection(QCNetworkReply *reply, QCNetworkAccessManager *manager);
    [[nodiscard]] static SignalEmissionResult restoreRevalidatedCacheResponse(QCNetworkReply *reply);
    static void storeCacheResponse(QCNetworkReply *reply);
#ifdef QCURL_ENABLE_TEST_HOOKS
    /// 仅测试直接进入 adapter 的错误线程拒绝，不经公共入口的排队投递。
    static void pauseOnCurrentThreadForTest(QCNetworkReply *reply, PauseMode mode);
#endif

private:
    [[nodiscard]] static bool completeFromCache(QCNetworkReply *reply,
                                                QCNetworkAccessManager *manager);
    [[nodiscard]] static bool dispatchMock(QCNetworkReply *reply, QCNetworkAccessManager *manager);
    [[nodiscard]] static bool prepareNetwork(QCNetworkReply *reply, QCNetworkAccessManager *manager);
};

} // namespace Internal
} // namespace QCurl

#endif // QCNETWORKREPLYEXECUTION_P_H
