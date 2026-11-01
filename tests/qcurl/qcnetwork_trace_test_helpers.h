#ifndef QCNETWORK_TRACE_TEST_HELPERS_H
#define QCNETWORK_TRACE_TEST_HELPERS_H

#include "private/QCNetworkReplyTrace_p.h"

namespace QCurl::TraceTest {

/// 未装饰 synthetic 输入也必须显式给出来源，不让生产消费接口假定 caller。
inline Internal::TraceCallback callerBytes(QByteArrayView bytes)
{
    return {{Internal::TraceSource::Caller, 1, {}, false, false}, bytes};
}

inline QString consume(Internal::ReplyTraceState &trace, curl_infotype type, QByteArrayView bytes)
{
    return trace.consume(type, callerBytes(bytes));
}

inline QString decorated(Internal::ReplyTraceState &trace,
                         curl_infotype type,
                         QByteArrayView raw,
                         Internal::TraceIds ids,
                         Internal::TraceSource source = Internal::TraceSource::Caller,
                         quintptr token               = 1)
{
    return trace.consume(type, Internal::splitTraceCallback(raw, source, token, ids));
}

} // namespace QCurl::TraceTest

#endif // QCNETWORK_TRACE_TEST_HELPERS_H
