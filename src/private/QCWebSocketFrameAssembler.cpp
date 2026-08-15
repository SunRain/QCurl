#include "QCWebSocketFrameAssembler_p.h"

#ifdef QCURL_WEBSOCKET_SUPPORT

#include "QCWebSocketCloseCode_p.h"
#include "QCWebSocket_p.h"

#include <utility>

namespace QCurl::Internal {

namespace {

namespace WsClose = WebSocketCloseCode;

bool isValidUtf8(const QByteArray &data, QString *decoded = nullptr)
{
    const QString text = QString::fromUtf8(data);
    if (text.toUtf8() != data) {
        return false;
    }
    if (decoded) {
        *decoded = text;
    }
    return true;
}

bool parseCloseFrame(QCWebSocketPrivate *d, const QByteArray &data, QString *reason, QString *error)
{
    if (data.size() > WsClose::kControlFrameMaxPayloadBytes) {
        if (error) {
            *error = QStringLiteral("WebSocket close frame 超过 125 字节");
        }
        return false;
    }
    if (data.size() == 1) {
        if (error) {
            *error = QStringLiteral("WebSocket close frame 不能只有一个状态码字节");
        }
        return false;
    }

    if (data.size() < WsClose::kCloseCodePayloadBytes) {
        d->lastCloseCode         = QCWebSocket::CloseCode::NoStatusReceived;
        d->lastWireCloseCode     = static_cast<int>(d->lastCloseCode);
        d->hasRetriableCloseCode = true;
        if (reason) {
            reason->clear();
        }
        return true;
    }

    const auto high      = static_cast<unsigned char>(data[0]);
    const auto low       = static_cast<unsigned char>(data[1]);
    const int code       = (high << WsClose::kCloseCodeByteShift) | low;
    d->lastWireCloseCode = code;
    if (!WsClose::isValidWire(code)) {
        if (error) {
            *error = QStringLiteral("收到非法 WebSocket close code: %1").arg(code);
        }
        return false;
    }
    if (WsClose::tryFromWire(code, &d->lastCloseCode)) {
        d->hasRetriableCloseCode = true;
    } else {
        d->hasRetriableCloseCode = false;
    }

    const QByteArray reasonBytes = data.mid(WsClose::kCloseCodePayloadBytes);
    QString decodedReason;
    if (!isValidUtf8(reasonBytes, &decodedReason)) {
        if (error) {
            *error = QStringLiteral("WebSocket close reason 不是有效 UTF-8");
        }
        return false;
    }
    if (reason) {
        *reason = decodedReason;
    }
    return true;
}

[[nodiscard]] bool validateMessageFrame(QCWebSocketPrivate *d,
                                        const curl_ws_frame *meta,
                                        qint64 chunkSize)
{
    const bool isText   = (meta->flags & CURLWS_TEXT) != 0;
    const bool isBinary = (meta->flags & CURLWS_BINARY) != 0;
    if (isText == isBinary) {
        static_cast<void>(
            d->protocolError(QCWebSocket::CloseCode::ProtocolError,
                             QStringLiteral("WebSocket data frame 缺少唯一消息类型")));
        return false;
    }
    if (meta->offset == 0) {
        d->currentFrameBytes = 0;
    }
    const qint64 maximum = d->options.maxFrameBytes();
    if (d->currentFrameBytes > maximum - chunkSize
        || static_cast<qint64>(meta->bytesleft) > maximum - d->currentFrameBytes - chunkSize) {
        static_cast<void>(d->protocolError(QCWebSocket::CloseCode::MessageTooBig,
                                           QStringLiteral("WebSocket frame 超过配置上限")));
        return false;
    }
    d->currentFrameBytes += chunkSize;

    const unsigned int type = isText ? CURLWS_TEXT : CURLWS_BINARY;
    if (d->fragmentTypeFlags == 0) {
        d->fragmentTypeFlags = type;
    } else if (d->fragmentTypeFlags != type) {
        static_cast<void>(
            d->protocolError(QCWebSocket::CloseCode::ProtocolError,
                             QStringLiteral("WebSocket fragmented message 类型不一致")));
        return false;
    }
    return true;
}

[[nodiscard]] bool appendMessageChunk(QCWebSocketPrivate *d, const QByteArray &data)
{
    const qint64 chunkSize = static_cast<qint64>(data.size());
    const qint64 buffered  = static_cast<qint64>(d->fragmentBuffer.size());
    if (buffered > d->options.maxMessageBytes() - chunkSize) {
        static_cast<void>(d->protocolError(QCWebSocket::CloseCode::MessageTooBig,
                                           QStringLiteral("WebSocket message 超过配置上限")));
        return false;
    }
    if (buffered > d->options.maxReceiveBufferBytes() - chunkSize) {
        static_cast<void>(
            d->protocolError(QCWebSocket::CloseCode::MessageTooBig,
                             QStringLiteral("WebSocket receive buffer 超过配置上限")));
        return false;
    }
    d->fragmentBuffer.append(data);
    return true;
}

WebSocketFrameDisposition emitCompletedMessage(QCWebSocketPrivate *d, QCWebSocket *q)
{
    const unsigned int messageType = d->fragmentTypeFlags;
    const QByteArray payload       = std::exchange(d->fragmentBuffer, {});
    d->fragmentTypeFlags           = 0;
    d->currentFrameBytes           = 0;

    if (messageType == CURLWS_TEXT) {
        QString text;
        if (!isValidUtf8(payload, &text)) {
            return d->protocolError(QCWebSocket::CloseCode::InvalidPayload,
                                    QStringLiteral("WebSocket text message 不是有效 UTF-8"))
                           == SignalEmissionResult::Destroyed
                       ? WebSocketFrameDisposition::Destroyed
                       : WebSocketFrameDisposition::Stop;
        }
        const auto result = emitWebSocketSignal(q, [text](QCWebSocket *socket) {
            Q_EMIT socket->textMessageReceived(text);
        });
        return result == SignalEmissionResult::Destroyed ? WebSocketFrameDisposition::Destroyed
                                                         : WebSocketFrameDisposition::Continue;
    } else {
        const auto result = emitWebSocketSignal(q, [payload](QCWebSocket *socket) {
            Q_EMIT socket->binaryMessageReceived(payload);
        });
        return result == SignalEmissionResult::Destroyed ? WebSocketFrameDisposition::Destroyed
                                                         : WebSocketFrameDisposition::Continue;
    }
}

WebSocketFrameDisposition processMessageFrame(QCWebSocketPrivate *d,
                                              QCWebSocket *q,
                                              const curl_ws_frame *meta,
                                              const QByteArray &data)
{
    const qint64 chunkSize = static_cast<qint64>(data.size());
    if (!validateMessageFrame(d, meta, chunkSize) || !appendMessageChunk(d, data)) {
        return WebSocketFrameDisposition::Stop;
    }
    if (meta->bytesleft != 0) {
        return WebSocketFrameDisposition::Continue;
    }
    d->currentFrameBytes = 0;
    if (meta->flags & CURLWS_CONT) {
        return WebSocketFrameDisposition::Continue;
    }

    return emitCompletedMessage(d, q);
}

WebSocketFrameDisposition processControlFrame(QCWebSocketPrivate *d,
                                              QCWebSocket *q,
                                              const curl_ws_frame *meta,
                                              const QByteArray &data)
{
    const bool isPing  = (meta->flags & CURLWS_PING) != 0;
    const bool isPong  = (meta->flags & CURLWS_PONG) != 0;
    const bool isClose = (meta->flags & CURLWS_CLOSE) != 0;
    if (static_cast<int>(isPing) + static_cast<int>(isPong) + static_cast<int>(isClose) != 1) {
        static_cast<void>(d->protocolError(QCWebSocket::CloseCode::ProtocolError,
                                           QStringLiteral("WebSocket control frame 缺少唯一类型")));
        return WebSocketFrameDisposition::Stop;
    }

    const unsigned int frameFlags = meta->flags & (CURLWS_PING | CURLWS_PONG | CURLWS_CLOSE);
    if (meta->offset == 0) {
        if (d->controlFrameFlags != 0) {
            static_cast<void>(
                d->protocolError(QCWebSocket::CloseCode::ProtocolError,
                                 QStringLiteral("WebSocket control frame 在前一帧完成前重启")));
            return WebSocketFrameDisposition::Stop;
        }
        d->controlFrameBuffer.clear();
        d->controlFrameFlags = frameFlags;
    } else if (meta->offset < 0 || d->controlFrameFlags != frameFlags
               || meta->offset != static_cast<curl_off_t>(d->controlFrameBuffer.size())) {
        static_cast<void>(
            d->protocolError(QCWebSocket::CloseCode::ProtocolError,
                             QStringLiteral("WebSocket control frame offset 或类型不连续")));
        return WebSocketFrameDisposition::Stop;
    }

    const qint64 buffered = d->controlFrameBuffer.size();
    const qint64 chunk    = data.size();
    const qint64 maximum  = WsClose::kControlFrameMaxPayloadBytes;
    if (meta->bytesleft < 0 || chunk > maximum - buffered
        || meta->bytesleft > static_cast<curl_off_t>(maximum - buffered - chunk)) {
        static_cast<void>(
            d->protocolError(QCWebSocket::CloseCode::ProtocolError,
                             QStringLiteral("WebSocket control frame 超过 125 字节")));
        return WebSocketFrameDisposition::Stop;
    }
    d->controlFrameBuffer.append(data);
    if (meta->bytesleft != 0) {
        return WebSocketFrameDisposition::Continue;
    }

    const QByteArray payload          = d->controlFrameBuffer;
    const unsigned int completedFlags = d->controlFrameFlags;
    d->controlFrameBuffer.clear();
    d->controlFrameFlags = 0;

    if (completedFlags & CURLWS_PONG) {
        const auto result = emitWebSocketSignal(q, [payload](QCWebSocket *socket) {
            Q_EMIT socket->pongReceived(payload);
        });
        return result == SignalEmissionResult::Destroyed ? WebSocketFrameDisposition::Destroyed
                                                         : WebSocketFrameDisposition::Continue;
    }
    if (completedFlags & CURLWS_PING) {
        const auto result = emitWebSocketSignal(q, [payload](QCWebSocket *socket) {
            Q_EMIT socket->pingReceived(payload);
        });
        if (result == SignalEmissionResult::Destroyed) {
            return WebSocketFrameDisposition::Destroyed;
        }
        if (d->options.autoPongEnabled()
            && !d->sendFrame(payload, CURLWS_PONG).isAccepted()) {
            return WebSocketFrameDisposition::Stop;
        }
        return WebSocketFrameDisposition::Continue;
    }

    QString reason;
    QString error;
    if (!parseCloseFrame(d, payload, &reason, &error)) {
        static_cast<void>(d->protocolError(QCWebSocket::CloseCode::ProtocolError, error));
        return WebSocketFrameDisposition::Stop;
    }

    const int wireCloseCode = d->lastWireCloseCode;
    d->peerCloseReceived    = true;
    if (d->setState(QCWebSocket::State::Closing) == SignalEmissionResult::Destroyed) {
        return WebSocketFrameDisposition::Destroyed;
    }
    const auto closeResult = emitWebSocketSignal(q, [wireCloseCode, reason](QCWebSocket *socket) {
        Q_EMIT socket->closeReceived(wireCloseCode, reason);
    });
    if (closeResult == SignalEmissionResult::Destroyed) {
        return WebSocketFrameDisposition::Destroyed;
    }
    if (!d->closeFrameSent) {
        static_cast<void>(d->queueClosePayload(payload));
        return WebSocketFrameDisposition::Stop;
    } else {
        return d->cleanupConnection() == SignalEmissionResult::Destroyed
                   ? WebSocketFrameDisposition::Destroyed
                   : WebSocketFrameDisposition::Stop;
    }
}

} // namespace

WebSocketFrameDisposition processWebSocketFrame(QCWebSocketPrivate *d,
                                                QCWebSocket *q,
                                                const curl_ws_frame *meta,
                                                const QByteArray &data)
{
    const QPointer<QCWebSocket> guard(q);
    if (meta->flags & (CURLWS_TEXT | CURLWS_BINARY)) {
        const auto disposition = processMessageFrame(d, q, meta, data);
        if (!guard) {
            return WebSocketFrameDisposition::Destroyed;
        }
        if (disposition != WebSocketFrameDisposition::Continue) {
            return disposition;
        }
        return d->state == QCWebSocket::State::Closing ? WebSocketFrameDisposition::Stop
                                                       : WebSocketFrameDisposition::Continue;
    }
    const auto disposition = processControlFrame(d, q, meta, data);
    return guard ? disposition : WebSocketFrameDisposition::Destroyed;
}

} // namespace QCurl::Internal

#endif // QCURL_WEBSOCKET_SUPPORT
