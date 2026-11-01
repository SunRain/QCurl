/**
 * @file
 * @brief 在正文分类前严格核对 callback 起始 lib-ids 装饰。
 */

#include "private/QCNetworkTraceCallback_p.h"

#include <limits>

namespace QCurl::Internal {

bool resemblesTraceIds(QByteArrayView raw)
{
    if (raw.isEmpty() || raw.front() != '[') {
        return false;
    }
    if (raw.size() == 1) {
        return true;
    }
    const char next = raw[1];
    return (next >= '0' && next <= '9') || next == 'x' || next == 'X' || next == '+' || next == '-'
           || next == ']' || next == '[' || next == ' ';
}

namespace {

/// nullopt 表示字段不符合小写 x 或非负 curl_off_t；不得用默认 ID 掩盖解析失败。
std::optional<curl_off_t> parseId(QByteArrayView field)
{
    if (field == QByteArrayView("x")) {
        return -1;
    }
    if (field.isEmpty()) {
        return std::nullopt;
    }
    curl_off_t value = 0;
    for (const char byte : field) {
        if (byte < '0' || byte > '9') {
            return std::nullopt;
        }
        const int digit = byte - '0';
        if (value > (std::numeric_limits<curl_off_t>::max() - digit) / 10) {
            return std::nullopt;
        }
        value = value * 10 + digit;
    }
    return value;
}

struct Decoration
{
    TraceIds ids;
    qsizetype bytes;
};

/// nullopt 表示候选装饰畸形或超限；调用方必须丢弃正文，不能恢复原始包装。
std::optional<Decoration> parseDecoration(QByteArrayView raw)
{
    // 两个最大十进制 curl_off_t 加上括号、连字符和空格；不扫描无界的原始包装。
    constexpr qsizetype kMaximumDecorationBytes = 2 * (std::numeric_limits<curl_off_t>::digits10 + 1)
                                                  + 4;
    const auto candidate = raw.first(qMin(raw.size(), kMaximumDecorationBytes));
    const qsizetype end  = candidate.indexOf(']');
    if (end < 0 || end + 1 >= raw.size() || raw[end + 1] != ' ') {
        return std::nullopt;
    }
    const auto fields    = raw.sliced(1, end - 1);
    const qsizetype dash = fields.indexOf('-');
    if (dash < 0) {
        return std::nullopt;
    }
    const auto transfer   = parseId(fields.first(dash));
    const auto connection = parseId(fields.sliced(dash + 1));
    if (!transfer || !connection) {
        return std::nullopt;
    }
    return Decoration{{*transfer, *connection}, end + 2};
}

} // namespace

TraceCallback splitTraceCallback(QByteArrayView raw,
                                 TraceSource source,
                                 quintptr sourceToken,
                                 std::optional<TraceIds> actualIds)
{
    TraceCallback result{{source, sourceToken, {}, false, false}, raw};
    if (source == TraceSource::Unknown || sourceToken == 0) {
        result.context.ambiguous = true;
        result.body              = {};
        return result;
    }
    if (resemblesTraceIds(raw)) {
        const auto decoration = parseDecoration(raw);
        if (!decoration || !actualIds || !(decoration->ids == *actualIds)) {
            result.context.ambiguous = true;
            result.body              = {};
            return result;
        }
        result.context.ids = decoration->ids;
        result.body        = raw.sliced(decoration->bytes);
        // 第二层包装由完整行统一拒绝；此处的正文可能只是 bracketed 头中间的一片。
    }
    // libcurl 的有界格式化可能补出 LF；该 LF 不能证明原记录已经完整结束。
    result.context.truncated = result.body.endsWith("...\n") || result.body.endsWith("...\r\n");
    return result;
}

TraceCallback inspectTraceCallback(CURL *handle, CURL *primary, QByteArrayView raw)
{
    const auto source = !handle || !primary ? TraceSource::Unknown
                        : handle == primary ? TraceSource::Caller
                                            : TraceSource::Internal;
    std::optional<TraceIds> actualIds;
    if (source != TraceSource::Unknown && resemblesTraceIds(raw)) {
        TraceIds ids;
        const auto transferStatus   = curl_easy_getinfo(handle, CURLINFO_XFER_ID, &ids.transfer);
        const auto connectionStatus = curl_easy_getinfo(handle, CURLINFO_CONN_ID, &ids.connection);
        if (transferStatus == CURLE_OK && connectionStatus == CURLE_OK) {
            actualIds = ids;
        }
    }
    return splitTraceCallback(raw, source, reinterpret_cast<quintptr>(handle), actualIds);
}

} // namespace QCurl::Internal
