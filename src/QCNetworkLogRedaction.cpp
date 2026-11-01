// SPDX-License-Identifier: MIT
// Copyright (c) 2025 QCurl Project

#include "QCNetworkHttpHeaders.h"
#include "private/QCNetworkLogRedaction_p.h"

#include <QRegularExpression>

#include <optional>

namespace QCurl {
namespace QCNetworkLogRedaction {
namespace {

struct TraceHeader
{
    QString prefix;
    QString key;
    QString value;
    bool bracketed = false;
    bool malformed = false;
};

/// nullopt 表示不是受支持的头记录；疑似敏感但畸形/歧义时返回 malformed，禁止普通文本回退。
[[nodiscard]] std::optional<TraceHeader> parseTraceHeader(const QString &text)
{
    // 普通敏感头的值是不透明数据；值内出现括号或冒号不能改变外层头名的脱敏规则。
    static const QRegularExpression kPlainHeaderPattern(
        QStringLiteral(R"(^[ \t]*([!#$%&'*+.^_`|~0-9A-Za-z-]+)[ \t]*:[ \t]*)"));
    const auto plain = kPlainHeaderPattern.match(text);
    if (plain.hasMatch()) {
        const QString key      = plain.captured(1);
        const QByteArray lower = key.toLatin1().toLower();
        if (isSensitiveHeaderKey(lower) || lower == QByteArrayLiteral("location")) {
            return TraceHeader{{}, key, text.mid(plain.capturedEnd()).trimmed()};
        }
    }
    // 行首已呈现敏感头名却缺少受支持的分隔符时，也不能按无匹配普通文本输出。
    static const QRegularExpression kPlainKeyPattern(
        QStringLiteral(R"(^[ \t]*([!#$%&'*+.^_`|~0-9A-Za-z-]+)(?=[ \t:]|$))"));
    const auto plainKey = kPlainKeyPattern.match(text);
    if (plainKey.hasMatch()) {
        const QByteArray lower = plainKey.captured(1).toLatin1().toLower();
        if (isSensitiveHeaderKey(lower) || lower == QByteArrayLiteral("location")) {
            return TraceHeader{{}, {}, {}, false, true};
        }
    }
    static const QRegularExpression kBracketedHeaderPattern(
        QStringLiteral(R"(\[(:?[!#$%&'*+.^_`|~0-9A-Za-z-]+):[ \t]*)"));
    auto headers      = kBracketedHeaderPattern.globalMatch(text);
    const auto header = headers.hasNext() ? headers.next() : QRegularExpressionMatch{};

    // 畸形敏感头不能按普通文本回退；多个头记录也不能把后一条秘密当作前一条普通值。
    static const QRegularExpression kBracketedKeyPattern(
        QStringLiteral(R"(\[[ \t]*(:?[!#$%&'*+.^_`|~0-9A-Za-z-]+)(?=[:\]\s]|$))"));
    auto keys = kBracketedKeyPattern.globalMatch(text);
    while (keys.hasNext()) {
        const auto key         = keys.next();
        const QByteArray lower = key.captured(1).toLatin1().toLower();
        if ((isSensitiveHeaderKey(lower) || lower == QByteArrayLiteral("location"))
            && (!header.hasMatch() || key.capturedStart() != header.capturedStart()
                || key.captured(1) != header.captured(1))) {
            return TraceHeader{{}, {}, {}, true, true};
        }
    }
    if (header.hasMatch()) {
        QString value = text.mid(header.capturedEnd());
        if (headers.hasNext() || !value.endsWith(QLatin1Char(']'))) {
            return TraceHeader{{}, {}, {}, true, true};
        }
        value.chop(1);
        return TraceHeader{text.left(header.capturedStart()), header.captured(1), value, true};
    }

    return std::nullopt;
}

/// nullopt 表示普通非凭据 TEXT；疑似 Cookie/认证记录不能安全解析时仍返回固定脱敏结果。
[[nodiscard]] std::optional<RedactedTraceLine> redactCredentialRecord(const QString &text)
{
    static const QRegularExpression kCredentialPrefixPattern(
        QStringLiteral(
            R"(^[ \t]*(?:\[[^\]\r\n]*\][ \t]*)*(?=(?:Added|Replaced)[ \t]+cookie(?:[ \t:=]|$)|(?:Server|Proxy)[ \t]+auth(?:[ \t:=]|$)))"),
        QRegularExpression::CaseInsensitiveOption);
    const auto prefix = kCredentialPrefixPattern.match(text);
    if (!prefix.hasMatch()) {
        return std::nullopt;
    }
    // libcurl 可附带 [transfer-connection] 或 feature 前缀；不将这些原始片段带入安全摘要。
    const QString record = text.mid(prefix.capturedEnd());

    // 只保留操作名，不重用 Cookie 名称、值、域名或路径中可能夹带的凭据片段。
    static const QRegularExpression kCookieRecordPattern(QStringLiteral(
        R"(^(Added|Replaced) cookie [!#$%&'*+.^_`|~0-9A-Za-z-]+="[^\r\n]*" for domain [^,\r\n]+, path [^,\r\n]*, expire -?[0-9]+$)"));
    const auto cookie = kCookieRecordPattern.match(record);
    if (cookie.hasMatch()) {
        return RedactedTraceLine{cookie.captured(1) + QStringLiteral(" cookie [REDACTED]"),
                                 true,
                                 true};
    }

    // 认证方式来自固定集合，用户名整体不透明；未知方式或不闭合记录不附带原始文本。
    static const QRegularExpression kAuthRecordPattern(QStringLiteral(
        R"(^(Server|Proxy) auth using (Basic|Digest|NTLM|Negotiate|Bearer) with user '[^\r\n]*'$)"));
    const auto auth = kAuthRecordPattern.match(record);
    if (auth.hasMatch()) {
        return RedactedTraceLine{auth.captured(1) + QStringLiteral(" auth using ")
                                     + auth.captured(2) + QStringLiteral(" with user [REDACTED]"),
                                 true,
                                 true};
    }
    return RedactedTraceLine{QStringLiteral("[REDACTED: credential record]"), true, true};
}

const QString kAmbiguousUrlRecord = QStringLiteral("[REDACTED: ambiguous URL record]");

/// nullopt 表示不是 libcurl 重定向 URL 记录；疑似但无法完整解析时返回固定标记。
[[nodiscard]] std::optional<QString> redactRedirectUrlRecord(const QString &text)
{
    static const QRegularExpression kRedirectRecordPrefixPattern(
        QStringLiteral(R"(^[ \t]*(?:\[[^\]\r\n]*\][ \t]*)*Issue another request to this URL:)"));
    const auto prefix = kRedirectRecordPrefixPattern.match(text);
    if (!prefix.hasMatch()) {
        return std::nullopt;
    }
    const QString quoted = text.mid(prefix.capturedEnd()).trimmed();
    // 单引号可来自合法 userinfo、path 或 fragment；只有完整记录的末尾引号是外层边界。
    if (quoted.size() < 3 || !quoted.startsWith(QLatin1Char('\''))
        || !quoted.endsWith(QLatin1Char('\''))) {
        return kAmbiguousUrlRecord;
    }
    const QUrl url(quoted.mid(1, quoted.size() - 2), QUrl::StrictMode);
    if (!url.isValid() || url.isRelative()) {
        return kAmbiguousUrlRecord;
    }
    // 不重用可含任意文本的 feature/transfer 前缀；TEXT 的 URL 无条件按 Redacted 输出。
    return QStringLiteral("Issue another request to this URL: '") + redactUrl(url)
           + QLatin1Char('\'');
}

QString removeTextUrlSecrets(const QString &text)
{
    static const QRegularExpression kTextUrlStartPattern(
        QStringLiteral(R"((?:[A-Za-z][A-Za-z0-9+.-]*://|(?<![A-Za-z0-9_])/{1,2}))"));
    const auto match = kTextUrlStartPattern.match(text);
    if (!match.hasMatch()) {
        return text;
    }
    QString candidate = text.mid(match.capturedStart());
    if (!candidate.contains(QLatin1Char('#')) && !candidate.contains(QLatin1Char('@'))) {
        return text;
    }
    // 未知引号/尖括号包装没有可信结束边界，不能把未匹配的秘密尾段原样拼回。
    if (match.capturedStart() > 0) {
        const QChar previous = text.at(match.capturedStart() - 1);
        if (previous == QLatin1Char('\'') || previous == QLatin1Char('"')
            || previous == QLatin1Char('<')) {
            return kAmbiguousUrlRecord;
        }
    }
    static const QRegularExpression kProtocolSuffixPattern(QStringLiteral(R"( HTTP/[0-9.]+$)"));
    const auto protocol  = kProtocolSuffixPattern.match(candidate);
    const QString suffix = protocol.hasMatch() ? protocol.captured() : QString();
    if (protocol.hasMatch()) {
        candidate.truncate(protocol.capturedStart());
    }
    // 未知格式只接受到行尾的完整 URL；空白或额外文本造成歧义时整行 fail-closed。
    const QUrl url(candidate, QUrl::StrictMode);
    if (!url.isValid()) {
        return kAmbiguousUrlRecord;
    }
    return text.left(match.capturedStart())
           + url.toString(QUrl::RemoveUserInfo | QUrl::RemoveFragment | QUrl::FullyEncoded)
           + suffix;
}

QString redactTextLine(const QString &text)
{
    if (const auto redirect = redactRedirectUrlRecord(text)) {
        return *redirect;
    }
    const QString safeLine     = removeTextUrlSecrets(text);
    const qsizetype queryStart = safeLine.indexOf(QLatin1Char('?'));
    if (queryStart < 0) {
        return safeLine;
    }
    QString result = safeLine.left(queryStart + 1) + QStringLiteral("[REDACTED]");
    // 诊断中的 query 可能已被解码为空白；只保留行尾可辨识的 HTTP 协议版本。
    const qsizetype protocol = safeLine.lastIndexOf(QStringLiteral(" HTTP/"));
    bool versionOnly         = protocol > queryStart && protocol + 6 < safeLine.size();
    for (qsizetype index = protocol + 6; versionOnly && index < safeLine.size(); ++index) {
        versionOnly = safeLine.at(index).isDigit() || safeLine.at(index) == QLatin1Char('.');
    }
    if (versionOnly) {
        result += safeLine.mid(protocol);
    }
    return result;
}

QString redactHeaderValue(const QByteArray &keyLower, const QString &value)
{
    if (isSensitiveHeaderKey(keyLower)) {
        return QStringLiteral("[REDACTED]");
    }
    if (keyLower == QByteArrayLiteral("location")) {
        // Location 也允许 redirect#fragment 和 #fragment 这样的相对引用。
        return redactUrl(QUrl(value, QUrl::TolerantMode));
    }
    return redactSensitiveQueryParams(value);
}

} // namespace

bool isSensitiveHeaderKey(const QByteArray &keyLower)
{
    if (keyLower == QCurl::httpheaders::kAuthorization.toLower()
        || keyLower == QCurl::httpheaders::kProxyAuthorization.toLower()
        || keyLower == QCurl::httpheaders::kCookie.toLower()
        || keyLower == QCurl::httpheaders::kSetCookie.toLower()) {
        return true;
    }

    if (keyLower == "apikey" || keyLower.contains("api-key")) {
        return true;
    }

    if (keyLower.contains("token") || keyLower.contains("secret") || keyLower.contains("signature")
        || keyLower == "sig" || keyLower.contains("password") || keyLower.contains("passwd")
        || keyLower == "pwd") {
        return true;
    }

    return false;
}

QString redactSensitiveQueryParams(const QString &line)
{
    QString result;
    qsizetype offset = 0;
    while (offset < line.size()) {
        qsizetype end = line.indexOf(QLatin1Char('\n'), offset);
        if (end < 0) {
            end = line.size();
        }
        const bool crlf = end > offset && line.at(end - 1) == QLatin1Char('\r');
        result += redactTextLine(line.mid(offset, end - offset - (crlf ? 1 : 0)));
        if (crlf) {
            result += QLatin1Char('\r');
        }
        if (end < line.size()) {
            result += QLatin1Char('\n');
        }
        offset = end + 1;
    }
    return result;
}

RedactedTraceLine redactSensitiveTraceLine(QByteArrayView line)
{
    if (line.endsWith('\n')) {
        line = line.chopped(1);
    }
    if (line.endsWith('\r')) {
        line = line.chopped(1);
    }

    const QString text = QString::fromUtf8(line);
    if (const auto credential = redactCredentialRecord(text)) {
        return *credential;
    }
    if (const auto header = parseTraceHeader(text)) {
        if (header->malformed) {
            return {QStringLiteral("[REDACTED: ambiguous header record]"), true};
        }
        const QByteArray keyLower = header->key.toLatin1().toLower();
        // Location 的 query/fragment 也可能折叠到下一行，不能让续段按普通文本输出。
        const bool protectContinuation = isSensitiveHeaderKey(keyLower)
                                         || keyLower == QByteArrayLiteral("location");
        const QString value            = redactHeaderValue(keyLower, header->value);
        if (header->bracketed) {
            return {redactSensitiveQueryParams(header->prefix) + QLatin1Char('[') + header->key
                        + QStringLiteral(": ") + value + QLatin1Char(']'),
                    protectContinuation};
        }
        return {header->key + QStringLiteral(": ") + value, protectContinuation};
    }

    return {redactSensitiveQueryParams(text), false};
}

QString redactUrl(const QUrl &url, QCNetworkDiagnosticQueryPolicy policy)
{
    if (policy == QCNetworkDiagnosticQueryPolicy::Full) {
        return url.toString(QUrl::RemoveUserInfo | QUrl::RemoveFragment | QUrl::FullyEncoded);
    }
    QString result = url.toString(QUrl::RemoveUserInfo | QUrl::RemoveFragment | QUrl::RemoveQuery
                                  | QUrl::FullyEncoded);
    if (url.hasQuery()) {
        result += QStringLiteral("?[REDACTED]");
    }
    return result;
}

} // namespace QCNetworkLogRedaction
} // namespace QCurl
