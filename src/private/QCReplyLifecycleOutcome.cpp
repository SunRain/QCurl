/**
 * @file
 * @brief 集中分类原生与回放 attempt 的错误，保留来源错误优先级。
 */

#include "private/QCReplyLifecycle_p.h"

namespace QCurl::Internal {
namespace {

ReplyAttemptErrorInfo httpFailure(int status)
{
    return {fromHttpCode(status), QStringLiteral("HTTP error %1").arg(status)};
}

} // namespace

ReplyAttemptErrorInfo QCReplyLifecycle::classifyNetworkResult(
    CURLcode code,
    int httpStatus,
    const std::optional<ReplyAttemptErrorInfo> &sourceFailure,
    bool sourcePresent,
    bool rangeSatisfied,
    bool receiveIdleExpired)
{
    if (isTerminal()) {
        return {m_error, m_errorMessage};
    }
    ReplyAttemptErrorInfo result;
    // 已取得的 total/low-speed 等原生失败保留其来源；仅空闲主动中止走本地分类。
    if (receiveIdleExpired && (code == CURLE_ABORTED_BY_CALLBACK || code == CURLE_OK)) {
        result = {NetworkError::ConnectionTimeout, QStringLiteral("接收空闲时限已到期"), true};
    } else if (code != CURLE_OK) {
        result = {fromCurlCode(static_cast<int>(code)), QString::fromUtf8(curl_easy_strerror(code))};
#if defined(CURLE_SEND_FAIL_REWIND)
        if (code == CURLE_SEND_FAIL_REWIND && sourcePresent && !sourceFailure) {
            result = {NetworkError::InvalidRequest,
                      QStringLiteral("request body source: 无法重发 body（seek/rewind 失败：%1）")
                          .arg(QString::fromUtf8(curl_easy_strerror(code)))};
        }
#else
        Q_UNUSED(sourcePresent);
#endif
    } else if (httpStatus >= 400 && (httpStatus != 416 || !rangeSatisfied)) {
        result = httpFailure(httpStatus);
    }
    return sourceFailure.value_or(result);
}

ReplyAttemptErrorInfo QCReplyLifecycle::classifyMockResult(
    bool explicitError,
    NetworkError error,
    int httpStatus,
    const std::optional<ReplyAttemptErrorInfo> &sourceFailure,
    bool receiveIdleExpired)
{
    if (isTerminal()) {
        return {m_error, m_errorMessage, m_receiveIdleTimeout};
    }
    m_nativeCode = 0;
    ReplyAttemptErrorInfo result;
    if (receiveIdleExpired) {
        result = {NetworkError::ConnectionTimeout, QStringLiteral("接收空闲时限已到期"), true};
    } else if (explicitError && error != NetworkError::NoError) {
        result = {error, QCurl::errorString(error)};
    } else if (httpStatus >= 400) {
        result = httpFailure(httpStatus);
    }
    return sourceFailure.value_or(result);
}

} // namespace QCurl::Internal
