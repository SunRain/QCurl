/**
 * @file
 * @brief 声明 WebSocket close code wire/typed 转换助手。
 */

#ifndef QCWEBSOCKETCLOSECODE_P_H
#define QCWEBSOCKETCLOSECODE_P_H

#include "QCWebSocket.h"

#ifdef QCURL_WEBSOCKET_SUPPORT

namespace QCurl::Internal::WebSocketCloseCode {

constexpr qsizetype kControlFrameMaxPayloadBytes = 125;
constexpr qsizetype kCloseCodePayloadBytes       = 2;
constexpr qsizetype kCloseReasonMaxBytes = kControlFrameMaxPayloadBytes - kCloseCodePayloadBytes;
constexpr quint16 kCloseCodeByteShift    = 8;

[[nodiscard]] bool isReserved(QCWebSocket::CloseCode closeCode) noexcept;
[[nodiscard]] bool isApplication(int code) noexcept;
[[nodiscard]] bool tryFromWire(int code, QCWebSocket::CloseCode *out) noexcept;
[[nodiscard]] int toWire(QCWebSocket::CloseCode closeCode) noexcept;

} // namespace QCurl::Internal::WebSocketCloseCode

#endif // QCURL_WEBSOCKET_SUPPORT
#endif // QCWEBSOCKETCLOSECODE_P_H
