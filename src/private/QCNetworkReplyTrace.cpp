/**
 * @file
 * @brief 隔离 callback 来源，按逻辑行脱敏并保留可靠关联 ID。
 */

#include "private/QCNetworkLogRedaction_p.h"
#include "private/QCNetworkReplyTrace_p.h"

#include <utility>

namespace QCurl::Internal {
namespace {

// 含 CR、不计结束 LF；第 4,097 字节丢弃整行。全部来源共用 12,288 字节。
constexpr qsizetype kMaximumRawLineBytes = 4096;
constexpr qsizetype kMaximumPendingBytes = 12288;
// QString::size() 计 UTF-16 单元；每行类型、ID 与标记均计入总输出预算。
constexpr qsizetype kMaximumMessageUtf16Units = 4096;
const QString kDroppedLine                    = QStringLiteral("[REDACTED: trace line discarded]");
const QString kAmbiguousLine = QStringLiteral("[REDACTED: ambiguous trace callback]");

QString idField(curl_off_t value)
{
    return value == -1 ? QStringLiteral("x") : QString::number(value);
}

QString tracePrefix(curl_infotype type, const std::optional<TraceIds> &ids)
{
    static const std::array<const char *, 7> kTypeNames{"TEXT",
                                                        "HEADER_IN",
                                                        "HEADER_OUT",
                                                        "DATA_IN",
                                                        "DATA_OUT",
                                                        "SSL_DATA_IN",
                                                        "SSL_DATA_OUT"};
    QString prefix = type >= CURLINFO_TEXT && type <= CURLINFO_SSL_DATA_OUT
                         ? QString::fromLatin1(kTypeNames[static_cast<size_t>(type)])
                         : QStringLiteral("TRACE_%1").arg(static_cast<int>(type));
    prefix += QStringLiteral(": ");
    if (ids) {
        prefix += QStringLiteral("[%1-%2] ").arg(idField(ids->transfer), idField(ids->connection));
    }
    return prefix;
}

void appendSafeLine(QString &message,
                    curl_infotype type,
                    const std::optional<TraceIds> &ids,
                    const QString &line)
{
    if (line.isEmpty()) {
        return;
    }
    const QString prefix      = tracePrefix(type, ids);
    const qsizetype separator = message.isEmpty() ? 0 : 1;
    const qsizetype available = kMaximumMessageUtf16Units - message.size() - separator;
    const QString marker      = QStringLiteral("…(truncated)");
    QString body              = line;
    if (prefix.size() + body.size() > available) {
        if (body.startsWith(QStringLiteral("[REDACTED"))) {
            // 预算不足时以完整安全标记替换此前输出，不截出半个 ID 或标记。
            if (prefix.size() + body.size() <= kMaximumMessageUtf16Units) {
                message = prefix + body;
            }
            return;
        }
        if (available < prefix.size() + marker.size()) {
            return;
        }
        qsizetype end             = available - prefix.size() - marker.size();
        const qsizetype redaction = body.lastIndexOf(QStringLiteral("[REDACTED"), end);
        if (redaction >= 0 && body.indexOf(QLatin1Char(']'), redaction) >= end) {
            end = redaction;
        }
        if (end > 0 && body.at(end - 1).isHighSurrogate()) {
            --end;
        }
        body = body.left(end) + marker;
    }
    if (separator) {
        message += QLatin1Char('\n');
    }
    message += prefix + body;
}

} // namespace

QString ReplyTraceState::formatCompleteLine(curl_infotype type, LineState &channel, bool caller)
{
    QByteArray line = std::move(channel.pending);
    channel.pending.clear();
    channel.observedIds.reset();
    m_pendingBytes -= line.size();
    if (line.endsWith('\r')) {
        line.chop(1);
    }
    if (resemblesTraceIds(line)) {
        channel.protectContinuation = true;
        channel.credentialRecord    = type == CURLINFO_TEXT;
        channel.desynchronized      = true;
        if (caller && type == CURLINFO_HEADER_OUT) {
            m_initialRequestTargetPending = false;
            m_headerOutExpectsTarget      = false;
        }
        return kAmbiguousLine;
    }
    const auto safe = QCNetworkLogRedaction::redactSensitiveTraceLine(line);
    if (type == CURLINFO_TEXT && channel.credentialRecord) {
        // 保护期内仅允许同一分类器生成的凭据安全摘要，绝不回显普通裸尾段。
        return safe.credentialRecord ? safe.text
                                     : QStringLiteral("[REDACTED: credential continuation]");
    }
    if (!line.isEmpty() && (line.front() == ' ' || line.front() == '\t')
        && channel.protectContinuation) {
        return QStringLiteral("[REDACTED]");
    }
    channel.protectContinuation = safe.protectContinuation;
    channel.credentialRecord    = type == CURLINFO_TEXT && safe.credentialRecord;
    return caller && type == CURLINFO_HEADER_OUT ? formatOutgoingLine(line, safe.text) : safe.text;
}

void ReplyTraceState::discardLine(curl_infotype type, LineState &channel, bool caller)
{
    m_pendingBytes -= channel.pending.size();
    channel.pending.clear();
    channel.ids.reset();
    channel.observedIds.reset();
    channel.discarding          = true;
    channel.protectContinuation = true;
    channel.credentialRecord    = type == CURLINFO_TEXT;
    if (caller && type == CURLINFO_HEADER_OUT) {
        m_initialRequestTargetPending = false;
        m_headerOutExpectsTarget      = false;
    }
}

void ReplyTraceState::clearSource(SourceState &source) noexcept
{
    for (const auto &channel : source.channels) {
        m_pendingBytes -= channel.pending.size();
    }
    source = {};
}

ReplyTraceState::SourceState *ReplyTraceState::selectSource(const TraceCallbackContext &context)
{
    if (context.source == TraceSource::Unknown || context.sourceToken == 0) {
        clearSource(m_internal);
        m_internal.desynchronized = true;
        return nullptr;
    }
    auto &source = context.source == TraceSource::Caller ? m_caller : m_internal;
    if (source.token && source.token != context.sourceToken) {
        clearSource(source);
        source.desynchronized = true;
    }
    source.token = context.sourceToken;
    return &source;
}

void ReplyTraceState::consumeByte(curl_infotype type,
                                  char byte,
                                  const TraceCallbackContext &context,
                                  LineState &channel,
                                  bool caller,
                                  QString &message)
{
    if (byte == '\n') {
        if (channel.discarding) {
            channel.discarding = false;
        } else {
            const auto ids = channel.pending.isEmpty() ? context.ids : channel.ids;
            appendSafeLine(message, type, ids, formatCompleteLine(type, channel, caller));
        }
        channel.ids.reset();
        return;
    }
    if (channel.discarding) {
        return;
    }
    if (channel.pending.size() == kMaximumRawLineBytes || m_pendingBytes == kMaximumPendingBytes) {
        const auto ids = channel.ids;
        discardLine(type, channel, caller);
        appendSafeLine(message, type, ids, kDroppedLine);
        return;
    }
    if (channel.pending.isEmpty()) {
        channel.ids         = context.ids;
        channel.observedIds = context.ids;
    } else if (!channel.observedIds && context.ids) {
        channel.observedIds = context.ids;
    }
    channel.pending.append(byte);
    ++m_pendingBytes;
}

void ReplyTraceState::consumeText(curl_infotype type,
                                  const TraceCallback &callback,
                                  SourceState &source,
                                  QString &message)
{
    auto &channel     = source.channels[static_cast<size_t>(type)];
    const bool caller = callback.context.source == TraceSource::Caller;
    if (channel.desynchronized) {
        appendSafeLine(message, type, callback.context.ids, kDroppedLine);
        return;
    }
    if (!channel.pending.isEmpty() && callback.context.ids && channel.observedIds
        && !(*channel.observedIds == *callback.context.ids)) {
        discardLine(type, channel, caller);
        channel.desynchronized = true;
        appendSafeLine(message, type, {}, kAmbiguousLine);
        return;
    }
    qsizetype uncertainStart = callback.body.size();
    if (callback.context.truncated) {
        const qsizetype lastLf     = callback.body.size() - 1;
        const qsizetype previousLf = callback.body.first(lastLf).lastIndexOf('\n');
        uncertainStart             = previousLf + 1;
    }
    for (qsizetype index = 0; index < callback.body.size(); ++index) {
        if (index == uncertainStart) {
            const auto ids = channel.pending.isEmpty() ? callback.context.ids : channel.ids;
            discardLine(type, channel, caller);
            // 可疑补出 LF 不解除保护；同来源的真实协议事件才允许恢复 TEXT。
            channel.discarding     = false;
            channel.desynchronized = type != CURLINFO_TEXT;
            appendSafeLine(message, type, ids, kDroppedLine);
            return;
        }
        consumeByte(type, callback.body[index], callback.context, channel, caller, message);
        if (channel.desynchronized) {
            return;
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
    clearSource(m_caller);
    clearSource(m_internal);
    m_initialRequestTargetPending = false;
    m_headerOutExpectsTarget      = false;
    m_active                      = false;
}

QString ReplyTraceState::consume(curl_infotype type, const TraceCallback &callback)
{
    if (!m_active) {
        return {};
    }
    auto *source = selectSource(callback.context);
    QString message;
    if (!source || source->desynchronized || callback.context.ambiguous) {
        if (type == CURLINFO_HEADER_OUT && callback.context.source != TraceSource::Internal) {
            m_initialRequestTargetPending = false;
            m_headerOutExpectsTarget      = false;
        }
        if (!source && type >= CURLINFO_TEXT && type <= CURLINFO_HEADER_OUT) {
            // 未知文本事件可能是 caller 同通道的一片；不能将下一片当作可信行首。
            auto &channel = m_caller.channels[static_cast<size_t>(type)];
            discardLine(type, channel, true);
            channel.desynchronized = true;
        }
        if (source && type >= CURLINFO_TEXT && type <= CURLINFO_HEADER_OUT) {
            auto &channel = source->channels[static_cast<size_t>(type)];
            discardLine(type, channel, callback.context.source == TraceSource::Caller);
            channel.desynchronized = true;
        }
        appendSafeLine(message, type, callback.context.ids, kAmbiguousLine);
        return message;
    }
    // 只有同一来源的可信非 TEXT 事件且尾段已完整消费，才解除凭据续段保护。
    auto &text = source->channels[CURLINFO_TEXT];
    if (type >= CURLINFO_HEADER_IN && type <= CURLINFO_SSL_DATA_OUT && text.credentialRecord
        && text.pending.isEmpty() && !text.discarding && !text.desynchronized) {
        text = {};
    }
    if (type >= CURLINFO_TEXT && type <= CURLINFO_HEADER_OUT) {
        consumeText(type, callback, *source, message);
    } else {
        appendSafeLine(message,
                       type,
                       callback.context.ids,
                       QStringLiteral("len=%1").arg(static_cast<qulonglong>(callback.body.size())));
    }
    return message;
}

} // namespace QCurl::Internal
