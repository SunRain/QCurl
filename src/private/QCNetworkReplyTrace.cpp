/**
 * @file
 * @brief 按逻辑行跨 callback 脱敏 libcurl trace；识别失败与超限时关闭可见性。
 */

#include "private/QCNetworkLogRedaction_p.h"
#include "private/QCNetworkReplyTrace_p.h"

#include <QRegularExpression>

#include <memory>
#include <optional>
#include <utility>

namespace QCurl::Internal {
namespace {

// 含可能的行尾 CR，不计结束 LF；第 4,097 字节触发整行丢弃，三通道共最多 12,288 字节。
constexpr qsizetype kMaximumRawLineBytes = 4096;
// QString::size() 计 UTF-16 单元，类型前缀与安全标记均包含在总预算内。
constexpr qsizetype kMaximumMessageUtf16Units = 4096;
const QString kDroppedLine                    = QStringLiteral("[REDACTED: trace line discarded]");

struct RequestTarget
{
    QString method;
    QString target;
    QString protocol;
};

/// nullopt 表示不是受支持的完整目标记录；目标位置的失败仍须消费资格，不能提升后来目标。
[[nodiscard]] std::optional<RequestTarget> parseRequestTarget(QByteArrayView line)
{
    static const QRegularExpression kRequestTargetPattern(
        QStringLiteral(R"(^([!#$%&'*+.^_`|~0-9A-Za-z-]+) ([^\s]+) (HTTP/[0-9.]+)$)"));
    const auto match = kRequestTargetPattern.match(QString::fromUtf8(line));
    if (!match.hasMatch()) {
        return std::nullopt;
    }
    return RequestTarget{match.captured(1), match.captured(2), match.captured(3)};
}

struct CurlUrlPart
{
    CURLUcode status;
    QString text;
};

[[nodiscard]] CurlUrlPart getUrlPart(const CURLU *handle, CURLUPart part, unsigned int flags)
{
    char *raw         = nullptr;
    const auto status = curl_url_get(handle, part, &raw, flags);
    const std::unique_ptr<char, decltype(&curl_free)> storage(raw, &curl_free);
    return {status, status == CURLUE_OK ? QString::fromUtf8(storage.get()) : QString()};
}

struct InitialTargets
{
    QString originForm;
    QString absoluteForm;
};

/// nullopt 表示无法安全构造 libcurl 规范化匹配基准；调用方须显式标记并保持 Redacted。
[[nodiscard]] std::optional<InitialTargets> normalizedInitialTargets(const QUrl &initial)
{
    const std::unique_ptr<CURLU, decltype(&curl_url_cleanup)> handle(curl_url(), &curl_url_cleanup);
    const QByteArray encoded = initial.toEncoded(QUrl::RemoveUserInfo | QUrl::RemoveFragment);
    if (!handle || !initial.isValid() || initial.isRelative()
        || curl_url_set(handle.get(), CURLUPART_URL, encoded.constData(), 0) != CURLUE_OK) {
        return std::nullopt;
    }
    // 不解码 query，%2B 与 + 不能视为同一实际目标；空 query 与无 query 也不混淆。
    const auto path     = getUrlPart(handle.get(), CURLUPART_PATH, 0);
    const auto query    = getUrlPart(handle.get(), CURLUPART_QUERY, CURLU_GET_EMPTY);
    const auto absolute = getUrlPart(handle.get(),
                                     CURLUPART_URL,
                                     CURLU_GET_EMPTY | CURLU_NO_DEFAULT_PORT);
    if (path.status != CURLUE_OK || absolute.status != CURLUE_OK
        || (query.status != CURLUE_OK && query.status != CURLUE_NO_QUERY)) {
        return std::nullopt;
    }
    const QString origin = path.text
                           + (query.status == CURLUE_OK ? QLatin1Char('?') + query.text : QString());
    return InitialTargets{origin, absolute.text};
}

void appendSafeLine(QString &message, const QString &prefix, const QString &line, bool &truncated)
{
    if (line.isEmpty() || truncated) {
        return;
    }
    message += message.isEmpty() ? prefix : QStringLiteral("\n");
    message += line;
    if (message.size() > kMaximumMessageUtf16Units) {
        const QString marker = QStringLiteral("…(truncated)");
        message              = message.left(kMaximumMessageUtf16Units - marker.size()) + marker;
        truncated            = true;
    }
}

} // namespace

ReplyTraceState::ReplyTraceState(const QUrl &initialUrl, QCNetworkDiagnosticQueryPolicy policy)
    : m_policy(policy)
{
    if (const auto targets = normalizedInitialTargets(initialUrl)) {
        m_initialOriginTarget   = targets->originForm;
        m_initialAbsoluteTarget = targets->absoluteForm;
        m_initialTargetValid    = true;
    }
}

QString ReplyTraceState::formatOutgoingLine(QByteArrayView line, const QString &safeLine)
{
    if (line.isEmpty()) {
        m_headerOutExpectsTarget = true;
        return {};
    }
    const auto request = parseRequestTarget(line);
    if (!m_headerOutExpectsTarget) {
        if (request) {
            // 头块内出现目标记录意味着阶段失步；不能将后来的目标提升为初始请求。
            m_initialRequestTargetPending = false;
        }
        return safeLine;
    }
    m_headerOutExpectsTarget = false;
    if (request && request->method == QStringLiteral("CONNECT")) {
        return safeLine;
    }
    const bool initialPending     = m_initialRequestTargetPending;
    m_initialRequestTargetPending = false;
    if (!request) {
        return kDroppedLine;
    }
    if (!initialPending || m_policy != QCNetworkDiagnosticQueryPolicy::Full) {
        return safeLine;
    }
    if (!m_initialTargetValid) {
        return safeLine + QStringLiteral(" [REDACTED: initial target unavailable]");
    }
    if (request->target != m_initialOriginTarget && request->target != m_initialAbsoluteTarget) {
        return safeLine;
    }
    // 已与不含 userinfo/fragment 的编码基准严格相符，不再改写实际目标的路径或 query。
    return request->method + QLatin1Char(' ') + request->target + QLatin1Char(' ')
           + request->protocol;
}

QString ReplyTraceState::formatCompleteLine(curl_infotype type, LineState &channel)
{
    QByteArray line = std::move(channel.pending);
    channel.pending.clear();
    if (line.endsWith('\r')) {
        line.chop(1);
    }
    if (type == CURLINFO_TEXT && channel.credentialRecord) {
        return QStringLiteral("[REDACTED: credential continuation]");
    }
    // 敏感头、Location 和凭据 TEXT 的缩进续行同样隐藏，不能因新 LF 丢失上下文。
    if (!line.isEmpty() && (line.front() == ' ' || line.front() == '\t')
        && channel.protectContinuation) {
        return QStringLiteral("[REDACTED]");
    }
    const auto safe             = QCNetworkLogRedaction::redactSensitiveTraceLine(line);
    channel.protectContinuation = safe.protectContinuation;
    channel.credentialRecord    = type == CURLINFO_TEXT && safe.credentialRecord;
    return type == CURLINFO_HEADER_OUT ? formatOutgoingLine(line, safe.text) : safe.text;
}

void ReplyTraceState::consumeText(curl_infotype type,
                                  QByteArrayView raw,
                                  QString &message,
                                  const QString &prefix)
{
    // consume 已筛选三个文本类型，枚举值分别为 0/1/2；不通过可抛异常的 at() 访问。
    auto &channel  = m_channels[static_cast<size_t>(type)];
    bool truncated = false;
    for (const char byte : raw) {
        if (byte == '\n') {
            if (channel.discarding) {
                channel.discarding = false;
            } else {
                appendSafeLine(message, prefix, formatCompleteLine(type, channel), truncated);
            }
        } else if (!channel.discarding) {
            if (channel.pending.size() == kMaximumRawLineBytes) {
                channel.pending.clear();
                channel.discarding          = true;
                channel.protectContinuation = true;
                // 超限 TEXT 已失去结构，可能是含换行的凭据记录；不能让后续裸行恢复原文。
                channel.credentialRecord = type == CURLINFO_TEXT;
                if (type == CURLINFO_HEADER_OUT && m_headerOutExpectsTarget) {
                    m_initialRequestTargetPending = false;
                    m_headerOutExpectsTarget      = false;
                }
                appendSafeLine(message, prefix, kDroppedLine, truncated);
            } else {
                channel.pending.append(byte);
            }
        }
    }
}

void ReplyTraceState::beginNetworkAttempt() noexcept
{
    stop();
    m_initialRequestTargetPending = true;
    m_headerOutExpectsTarget      = true;
    m_active                      = true;
}

void ReplyTraceState::stop() noexcept
{
    for (auto &channel : m_channels) {
        channel = {};
    }
    m_initialRequestTargetPending = false;
    m_headerOutExpectsTarget      = false;
    m_active                      = false;
}

QString ReplyTraceState::consume(curl_infotype type, QByteArrayView raw)
{
    if (!m_active) {
        return {};
    }
    // 用户名可含 LF 和引号，不能凭文本猜测记录结束。真实非 TEXT 事件才提供可信边界；
    // 在此之前隐藏所有凭据续段；跨类型时尚未完成的尾段仍保留保护，不补写 raw。
    // 只有原始行缓存已完整消费才解除保护，让后续正常 TEXT 继续参与诊断。
    if (type >= CURLINFO_HEADER_IN && type <= CURLINFO_SSL_DATA_OUT
        && m_channels[CURLINFO_TEXT].credentialRecord && m_channels[CURLINFO_TEXT].pending.isEmpty()
        && !m_channels[CURLINFO_TEXT].discarding) {
        m_channels[CURLINFO_TEXT] = {};
    }
    QString prefix;
    switch (type) {
        case CURLINFO_TEXT:
            prefix = QStringLiteral("TEXT: ");
            break;
        case CURLINFO_HEADER_IN:
            prefix = QStringLiteral("HEADER_IN: ");
            break;
        case CURLINFO_HEADER_OUT:
            prefix = QStringLiteral("HEADER_OUT: ");
            break;
        case CURLINFO_DATA_IN:
            prefix = QStringLiteral("DATA_IN: ");
            break;
        case CURLINFO_DATA_OUT:
            prefix = QStringLiteral("DATA_OUT: ");
            break;
        case CURLINFO_SSL_DATA_IN:
            prefix = QStringLiteral("SSL_DATA_IN: ");
            break;
        case CURLINFO_SSL_DATA_OUT:
            prefix = QStringLiteral("SSL_DATA_OUT: ");
            break;
        default:
            prefix = QStringLiteral("TRACE_%1: ").arg(static_cast<int>(type));
            break;
    }
    if (type != CURLINFO_TEXT && type != CURLINFO_HEADER_IN && type != CURLINFO_HEADER_OUT) {
        return prefix + QStringLiteral("len=%1").arg(static_cast<qulonglong>(raw.size()));
    }
    QString message;
    consumeText(type, raw, message, prefix);
    return message;
}

} // namespace QCurl::Internal
