/**
 * @file
 * @brief 有界、transfer-owned 的 libcurl 诊断流状态。
 */

#ifndef QCNETWORKREPLYTRACE_P_H
#define QCNETWORKREPLYTRACE_P_H

#include "QCNetworkDiagnosticQueryPolicy.h"
#include "private/QCNetworkTraceCallback_p.h"

#include <QByteArray>
#include <QString>
#include <QUrl>

#include <array>

namespace QCurl::Internal {

/// 维护有界诊断流与一次性目标资格；不拥有 reply、logger 或 libcurl handle。
class ReplyTraceState
{
public:
    explicit ReplyTraceState(
        const QUrl &initialUrl                = {},
        QCNetworkDiagnosticQueryPolicy policy = QCNetworkDiagnosticQueryPolicy::Redacted);

    /// 仅真实网络执行入口调用；清掉旧尾段并为新 QCurl attempt 开放一次目标资格。
    void beginNetworkAttempt() noexcept;
    /// 清理、终止或解绑只丢弃尾段并停止输出，不能借清理开放 Full 资格。
    void stop() noexcept;
    /// context 与正文仅借用至调用结束；未完成行不输出，跨调用只保存有界副本与数值 ID。
    /// 最终 message 含每行类型、可靠 ID 与标记，最多 4,096 个 UTF-16 单元。
    [[nodiscard]] QString consume(curl_infotype type, const TraceCallback &callback);

private:
    Q_DISABLE_COPY_MOVE(ReplyTraceState)

    struct LineState
    {
        QByteArray pending;
        std::optional<TraceIds> ids;
        // 与首片显示 ID 分开：首片无 ID 时，后续可靠 ID 仍需彼此一致。
        std::optional<TraceIds> observedIds;
        bool discarding          = false;
        bool desynchronized      = false;
        bool protectContinuation = false;
        bool credentialRecord    = false;
    };

    struct SourceState
    {
        std::array<LineState, 3> channels;
        quintptr token      = 0;
        bool desynchronized = false;
    };

    QString formatOutgoingLine(QByteArrayView line, const QString &safeLine);
    QString formatCompleteLine(curl_infotype type, LineState &channel, bool caller);
    void consumeByte(curl_infotype type,
                     char byte,
                     const TraceCallbackContext &context,
                     LineState &channel,
                     bool caller,
                     QString &message);
    void consumeText(curl_infotype type,
                     const TraceCallback &callback,
                     SourceState &source,
                     QString &message);
    void discardLine(curl_infotype type, LineState &channel, bool caller);
    void clearSource(SourceState &source) noexcept;
    SourceState *selectSource(const TraceCallbackContext &context);

    const QCNetworkDiagnosticQueryPolicy m_policy;
    // 仅用于日志匹配；采用 libcurl 默认路径规范化，绝不回写 request 或 CurlPlan。
    QString m_initialOriginTarget;
    QString m_initialAbsoluteTarget;
    bool m_initialTargetValid = false;
    SourceState m_caller;
    SourceState m_internal;
    // caller/internal 的全部通道共享原始字节预算，不能按来源翻倍。
    qsizetype m_pendingBytes           = 0;
    bool m_initialRequestTargetPending = false;
    bool m_headerOutExpectsTarget      = false;
    bool m_active                      = false;
};

} // namespace QCurl::Internal

#endif // QCNETWORKREPLYTRACE_P_H
