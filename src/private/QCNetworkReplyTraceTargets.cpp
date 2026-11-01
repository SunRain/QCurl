/**
 * @file
 * @brief 构造诊断专用初始目标，并维护 caller 的一次性 Full 资格。
 */

#include "private/QCNetworkReplyTrace_p.h"

#include <QRegularExpression>

#include <memory>
#include <optional>

namespace QCurl::Internal {
namespace {

struct RequestTarget
{
    QString method;
    QString target;
    QString protocol;
};

/// nullopt 表示未识别出完整目标；目标位置的失败仍消费资格，不能提升后来目标。
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

/// nullopt 表示无法构造 libcurl 规范化基准；调用方显式标记并保持 Redacted。
[[nodiscard]] std::optional<InitialTargets> normalizedInitialTargets(const QUrl &initial)
{
    const std::unique_ptr<CURLU, decltype(&curl_url_cleanup)> handle(curl_url(), &curl_url_cleanup);
    const QByteArray encoded = initial.toEncoded(QUrl::RemoveUserInfo | QUrl::RemoveFragment);
    if (!handle || !initial.isValid() || initial.isRelative()
        || curl_url_set(handle.get(), CURLUPART_URL, encoded.constData(), 0) != CURLUE_OK) {
        return std::nullopt;
    }
    // 不解码 query，%2B 与 + 不等价；空 query 与无 query 也不混淆。
    const auto path     = getUrlPart(handle.get(), CURLUPART_PATH, 0);
    const auto query    = getUrlPart(handle.get(), CURLUPART_QUERY, CURLU_GET_EMPTY);
    const auto absolute = getUrlPart(handle.get(),
                                     CURLUPART_URL,
                                     CURLU_GET_EMPTY | CURLU_NO_DEFAULT_PORT);
    if (path.status != CURLUE_OK || absolute.status != CURLUE_OK
        || (query.status != CURLUE_OK && query.status != CURLUE_NO_QUERY)) {
        return std::nullopt;
    }
    return InitialTargets{path.text
                              + (query.status == CURLUE_OK ? QLatin1Char('?') + query.text
                                                           : QString()),
                          absolute.text};
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
        return QStringLiteral("[REDACTED: trace line discarded]");
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
    return request->method + QLatin1Char(' ') + request->target + QLatin1Char(' ')
           + request->protocol;
}

} // namespace QCurl::Internal
