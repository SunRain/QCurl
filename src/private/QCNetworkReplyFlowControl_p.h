/**
 * @file
 * @brief 封装暂停原因、背压与原生 pause 事务。
 */

#ifndef QCNETWORKREPLYFLOWCONTROL_P_H
#define QCNETWORKREPLYFLOWCONTROL_P_H

#include "QCNetworkReply.h"

#include <curl/curl.h>

namespace QCurl::Internal {

/** @brief 本次流控事务结果；变化只描述本次提交，不跨信号保存后续动作。 */
struct ReplyFlowChange
{
    bool accepted            = true;
    CURLcode error           = CURLE_OK;
    int nativeMask           = 0;
    bool backpressureChanged = false;
    bool uploadPauseChanged  = false;
    bool wake                = false;
};

/**
 * @brief 暂停原因和背压的唯一状态持有者。
 *
 * easy 只在操作期间借用；adapter 先验证 owner thread，再保留同一共享 holder 跨越原生
 * 调用和信号。恢复可能同步触发读写回调，成功返回后不得用旧 mask 覆盖其提交。
 */
class Q_DECL_HIDDEN QCReplyFlowControl
{
public:
    QCReplyFlowControl(qint64 limit = 0, qint64 resume = 0);

    [[nodiscard]] ReplyFlowChange pauseUser(CURL *handle, PauseMode mode, bool inCallback);
    [[nodiscard]] ReplyFlowChange resumeUser(CURL *handle);
    [[nodiscard]] bool pauseWriteIfRequested();
    void observeBufferedBytes(qint64 bytes);
    [[nodiscard]] ReplyFlowChange afterReadyRead(CURL *handle, qint64 bytes, bool terminal);
    [[nodiscard]] ReplyFlowChange resumeAfterRead(CURL *handle, qint64 bytes, bool terminal);
    [[nodiscard]] bool sourceWait();
    [[nodiscard]] ReplyFlowChange resumeReadySource(CURL *handle, bool ready, bool terminal);
    [[nodiscard]] bool leaveBackpressure();
    [[nodiscard]] bool leaveUploadPause();
    void pauseReplay(PauseMode mode);
    void resumeReplay();

    [[nodiscard]] int desiredMask() const noexcept { return m_userMask | m_internalMask; }
    [[nodiscard]] int appliedMask() const noexcept { return m_appliedMask; }
    [[nodiscard]] bool recvResumePending() const noexcept;
    [[nodiscard]] bool backpressureActive() const noexcept { return m_backpressureActive; }
    [[nodiscard]] bool uploadSendPaused() const noexcept;
    [[nodiscard]] qint64 limitBytes() const noexcept { return m_limit; }
    [[nodiscard]] qint64 resumeBytes() const noexcept { return m_resume; }
    [[nodiscard]] qint64 peakBufferedBytes() const noexcept { return m_peak; }

private:
    [[nodiscard]] ReplyFlowChange apply(CURL *handle, int mask);
    [[nodiscard]] static int modeMask(PauseMode mode);

    int m_userMask            = 0;
    int m_internalMask        = 0;
    int m_appliedMask         = 0;
    qint64 m_limit            = 0;
    qint64 m_resume           = 0;
    qint64 m_peak             = 0;
    bool m_backpressureActive = false;
    bool m_uploadSendPaused   = false;
};

#ifdef QCURL_ENABLE_TEST_HOOKS
/// 仅 TestInternals 使用的原生 pause 观测/失败注入，不保存到生产 flow 对象。
using ReplyPauseTestHook = CURLcode (*)(CURL *, int);
void setReplyPauseTestHook(ReplyPauseTestHook hook);
#endif

} // namespace QCurl::Internal

#endif // QCNETWORKREPLYFLOWCONTROL_P_H
