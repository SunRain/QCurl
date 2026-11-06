/**
 * @file
 * @brief QCNetworkReply curl callbacks 与传输 pause/backpressure 辅助。
 */

#ifndef QCNETWORKREPLYCALLBACKS_P_H
#define QCNETWORKREPLYCALLBACKS_P_H

#include "QCNetworkReply.h"

#include <QPointer>
#include <QString>

#include <curl/curl.h>

namespace QCurl {

struct QCNetworkReplyTransferState;

namespace Internal {

class ReplyCurlCallbackScope
{
public:
    ReplyCurlCallbackScope();
    ~ReplyCurlCallbackScope();
};

[[nodiscard]] bool isInReplyCurlCallback() noexcept;

size_t headerReplyCurlCallback(char *ptr,
                               size_t size,
                               size_t nmemb,
                               QCNetworkReplyTransferState *state);

size_t writeReplyCurlCallback(char *ptr,
                              size_t size,
                              size_t nmemb,
                              QCNetworkReplyTransferState *state,
                              const QPointer<QCNetworkReply> &observer,
                              CURL *handle);
size_t readReplyBodySourceCallback(char *ptr,
                                   size_t size,
                                   size_t nmemb,
                                   QCNetworkReplyTransferState *state,
                                   const QPointer<QCNetworkReply> &observer);
int seekReplyBodySourceCallback(QCNetworkReplyTransferState *state, curl_off_t offset, int origin);

int progressReplyCurlCallback(QCNetworkReplyTransferState *state,
                              const QPointer<QCNetworkReply> &observer,
                              curl_off_t dltotal,
                              curl_off_t dlnow,
                              curl_off_t ultotal,
                              curl_off_t ulnow);

} // namespace Internal

} // namespace QCurl

#endif // QCNETWORKREPLYCALLBACKS_P_H
