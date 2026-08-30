/**
 * @file
 * @brief QCNetworkReply response header 与 attempt error 辅助。
 */

#ifndef QCNETWORKREPLYRESPONSE_P_H
#define QCNETWORKREPLYRESPONSE_P_H

#include "QCNetworkError.h"
#include "QCNetworkReply.h"
#include "private/QCNetworkMockProvider_p.h"
#include "private/QCRequestPipeline_p.h"

#include <curl/curl.h>

namespace QCurl {

class QCNetworkReplyPrivate;
struct QCNetworkReplyTransferState;

namespace Internal {

struct ReplyAttemptErrorInfo
{
    NetworkError error = NetworkError::NoError;
    QString message;
};

void parseReplyHeaders(QCNetworkReplyPrivate *reply);
void parseReplyHeaders(QCNetworkReplyTransferState *state);
size_t headerReplyCurlCallback(char *ptr,
                               size_t size,
                               size_t nmemb,
                               QCNetworkReplyTransferState *state);

[[nodiscard]] ReplyAttemptErrorInfo attemptErrorFromCurlAndHttp(QCNetworkReplyPrivate *reply,
                                                                CURLcode curlCode,
                                                                long httpCode);
[[nodiscard]] ReplyAttemptErrorInfo attemptErrorFromMockData(QCNetworkReplyPrivate *reply,
                                                             const QCNetworkMockData &mockData);
void applyMockResponseHeaders(QCNetworkReplyPrivate *reply, const QCNetworkMockData &mockData);

} // namespace Internal

} // namespace QCurl

#endif // QCNETWORKREPLYRESPONSE_P_H
