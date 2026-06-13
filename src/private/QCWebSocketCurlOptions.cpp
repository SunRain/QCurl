#include "private/QCWebSocketCurlOptions_p.h"

#ifdef QCURL_WEBSOCKET_SUPPORT

#include <QString>

#include <curl/curl.h>

namespace QCurl::Internal::WebSocketCurlOptions {

bool apply(CURLcode code, QCWebSocketPrivate *d, const char *optionName)
{
    if (code == CURLE_OK) {
        return true;
    }

    const QString message = QStringLiteral("%1 failed: %2")
                                .arg(QString::fromLatin1(optionName),
                                     QString::fromUtf8(curl_easy_strerror(code)));
    d->handleError(message);
    return false;
}

} // namespace QCurl::Internal::WebSocketCurlOptions

#endif // QCURL_WEBSOCKET_SUPPORT
