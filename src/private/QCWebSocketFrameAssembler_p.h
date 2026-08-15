/**
 * @file
 * @brief 声明 WebSocket frame/message 拼装入口。
 */

#ifndef QCWEBSOCKETFRAMEASSEMBLER_P_H
#define QCWEBSOCKETFRAMEASSEMBLER_P_H

#include <QByteArray>

struct curl_ws_frame;

namespace QCurl {

class QCWebSocket;
class QCWebSocketPrivate;

namespace Internal {

enum class WebSocketFrameDisposition {
    Continue,
    Stop,
    Destroyed,
};

[[nodiscard]] WebSocketFrameDisposition processWebSocketFrame(QCWebSocketPrivate *d,
                                                              QCWebSocket *q,
                                                              const curl_ws_frame *meta,
                                                              const QByteArray &data);

} // namespace Internal
} // namespace QCurl

#endif // QCWEBSOCKETFRAMEASSEMBLER_P_H
