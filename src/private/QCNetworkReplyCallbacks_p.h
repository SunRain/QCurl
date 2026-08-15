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

size_t writeReplyCurlCallback(char *ptr,
                              size_t size,
                              size_t nmemb,
                              QCNetworkReplyTransferState *state,
                              const QPointer<QCNetworkReply> &observer,
                              CURL *handle);
int progressReplyCurlCallback(QCNetworkReplyTransferState *state,
                              const QPointer<QCNetworkReply> &observer,
                              curl_off_t dltotal,
                              curl_off_t dlnow,
                              curl_off_t ultotal,
                              curl_off_t ulnow);
[[nodiscard]] QString formatReplyDebugTraceMessage(curl_infotype type, const QByteArray &raw);

} // namespace Internal

} // namespace QCurl

#endif // QCNETWORKREPLYCALLBACKS_P_H
