/**
 * @file
 * @brief QCNetworkReply curl callbacks 与传输 pause/backpressure 辅助。
 */

#ifndef QCNETWORKREPLYCALLBACKS_P_H
#define QCNETWORKREPLYCALLBACKS_P_H

#include <curl/curl.h>

namespace QCurl {

class QCNetworkReplyPrivate;

namespace Internal {

class ReplyCurlCallbackScope
{
public:
    ReplyCurlCallbackScope();
    ~ReplyCurlCallbackScope();
};

[[nodiscard]] bool isInReplyCurlCallback() noexcept;

size_t writeReplyCurlCallback(char *ptr, size_t size, size_t nmemb, QCNetworkReplyPrivate *reply);
int progressReplyCurlCallback(QCNetworkReplyPrivate *reply,
                              curl_off_t dltotal,
                              curl_off_t dlnow,
                              curl_off_t ultotal,
                              curl_off_t ulnow);

} // namespace Internal

} // namespace QCurl

#endif // QCNETWORKREPLYCALLBACKS_P_H
