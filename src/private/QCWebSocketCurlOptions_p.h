/**
 * @file
 * @brief 声明 WebSocket libcurl option 应用助手。
 */

#ifndef QCWEBSOCKETCURLOPTIONS_P_H
#define QCWEBSOCKETCURLOPTIONS_P_H

#include "QCWebSocket_p.h"

#ifdef QCURL_WEBSOCKET_SUPPORT

namespace QCurl::Internal::WebSocketCurlOptions {

[[nodiscard]] bool apply(CURLcode code, QCWebSocketPrivate *d, const char *optionName);

} // namespace QCurl::Internal::WebSocketCurlOptions

#endif // QCURL_WEBSOCKET_SUPPORT
#endif // QCWEBSOCKETCURLOPTIONS_P_H
