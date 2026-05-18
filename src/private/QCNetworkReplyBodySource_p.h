/**
 * @file
 * @brief QCNetworkReply request-body source 状态与 curl 回调辅助。
 */

#ifndef QCNETWORKREPLYBODYSOURCE_P_H
#define QCNETWORKREPLYBODYSOURCE_P_H

#include "QCNetworkError.h"
#include "QCNetworkReply.h"
#include "private/QCRequestPipeline_p.h"

#include <QPointer>
#include <QString>

#include <curl/curl.h>

class QIODevice;

namespace QCurl {

class QCNetworkReplyPrivate;

namespace Internal {

struct ReplyBodySourceState
{
    QPointer<QIODevice> device;
    qint64 basePos = 0;
    qint64 sizeBytes = -1;
    qint64 bytesRead = 0;
    bool seekable = false;

    bool hasErrorOverride = false;
    NetworkError errorOverrideCode = NetworkError::NoError;
    QString errorOverrideMessage;
};

void resetReplyBodySource(ReplyBodySourceState &state);
void clearReplyBodySourceError(ReplyBodySourceState &state);
[[nodiscard]] bool hasReplyBodySourceError(const ReplyBodySourceState &state) noexcept;
[[nodiscard]] NetworkError replyBodySourceErrorCode(const ReplyBodySourceState &state) noexcept;
[[nodiscard]] QString replyBodySourceErrorMessage(const ReplyBodySourceState &state);

[[nodiscard]] bool prepareReplyBodySource(QCNetworkReplyPrivate *reply,
                                          const RequestBody &bodySpec,
                                          QString *errorMessage);
[[nodiscard]] bool rewindReplyBodySourceForRetry(QCNetworkReplyPrivate *reply,
                                                 QString *errorMessage);

size_t readReplyBodySourceCallback(char *ptr, size_t size, size_t nmemb, QCNetworkReplyPrivate *reply);
int seekReplyBodySourceCallback(QCNetworkReplyPrivate *reply, curl_off_t offset, int origin);

} // namespace Internal

} // namespace QCurl

#endif // QCNETWORKREPLYBODYSOURCE_P_H
