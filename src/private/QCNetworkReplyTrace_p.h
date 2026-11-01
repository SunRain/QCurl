/**
 * @file
 * @brief 有界、transfer-owned 的 libcurl 诊断流状态。
 */

#ifndef QCNETWORKREPLYTRACE_P_H
#define QCNETWORKREPLYTRACE_P_H

#include "QCNetworkDiagnosticQueryPolicy.h"

#include <QByteArray>
#include <QByteArrayView>
#include <QString>
#include <QUrl>

#include <array>
#include <curl/curl.h>

namespace QCurl::Internal {

/// 维护 transfer-owned 的有界诊断流与一次性目标资格；不拥有 reply、logger 或 libcurl handle。
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
    /// 消费借用字节，完整行脱敏后输出；未完成行返回空串，不跨调用保存 view。
    /// 最终 message 含类型前缀和标记，最多 4,096 个 UTF-16 单元。
    [[nodiscard]] QString consume(curl_infotype type, QByteArrayView raw);

private:
    Q_DISABLE_COPY_MOVE(ReplyTraceState)

    struct LineState
    {
        QByteArray pending;
        bool discarding          = false;
        bool protectContinuation = false;
        bool credentialRecord    = false;
    };

    QString formatOutgoingLine(QByteArrayView line, const QString &safeLine);
    QString formatCompleteLine(curl_infotype type, LineState &channel);
    void consumeText(curl_infotype type,
                     QByteArrayView raw,
                     QString &message,
                     const QString &prefix);

    const QCNetworkDiagnosticQueryPolicy m_policy;
    // 仅用于日志匹配；采用 libcurl 默认路径规范化，绝不回写 request 或 CurlPlan。
    QString m_initialOriginTarget;
    QString m_initialAbsoluteTarget;
    bool m_initialTargetValid = false;
    std::array<LineState, 3> m_channels;
    bool m_initialRequestTargetPending = false;
    bool m_headerOutExpectsTarget      = false;
    bool m_active                      = false;
};

} // namespace QCurl::Internal

#endif // QCNETWORKREPLYTRACE_P_H
