/**
 * @file
 * @brief 借用型 callback 来源、严格 lib-ids 元信息与正文视图。
 */

#ifndef QCNETWORKTRACECALLBACK_P_H
#define QCNETWORKTRACECALLBACK_P_H

#include <QByteArrayView>
#include <QtGlobal>

#include <curl/curl.h>
#include <optional>

namespace QCurl::Internal {

enum class TraceSource {
    Unknown,
    Caller,
    Internal,
};

struct TraceIds
{
    curl_off_t transfer   = -1;
    curl_off_t connection = -1;

    bool operator==(const TraceIds &other) const noexcept
    {
        return transfer == other.transfer && connection == other.connection;
    }
};

struct TraceCallbackContext
{
    TraceSource source = TraceSource::Unknown;
    // 仅作非拥有来源比较，不跨 callback 解引用或查询 handle。
    quintptr sourceToken = 0;
    std::optional<TraceIds> ids;
    bool ambiguous = false;
    bool truncated = false;
};

struct TraceCallback
{
    TraceCallbackContext context;
    QByteArrayView body;
};

/// 判断完整正文中的疑似 ID 包装；用于拒绝跨片段组成的第二层装饰，不负责剥离。
[[nodiscard]] bool resemblesTraceIds(QByteArrayView raw);

/// actualIds 为空表示 getter 失败；候选装饰未被核对时仅返回歧义状态，不回显原文。
/// 无装饰时不补造 ID；返回的正文 view 仅在原 callback 字节存活期间有效。
[[nodiscard]] TraceCallback splitTraceCallback(QByteArrayView raw,
                                               TraceSource source,
                                               quintptr sourceToken,
                                               std::optional<TraceIds> actualIds);

/// 只在本次 callback 内借用 handle；仅对候选装饰查询 ID，不改变传输或 PRIVATE owner。
[[nodiscard]] TraceCallback inspectTraceCallback(CURL *handle, CURL *primary, QByteArrayView raw);

} // namespace QCurl::Internal

#endif // QCNETWORKTRACECALLBACK_P_H
