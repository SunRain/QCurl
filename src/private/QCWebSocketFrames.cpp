#include "QCWebSocket_p.h"

#ifdef QCURL_WEBSOCKET_SUPPORT

#include "QCWebSocketCloseCode_p.h"
#include "QCWebSocketFrameAssembler_p.h"

#include <QDebug>

namespace QCurl {

namespace {

namespace WsClose = Internal::WebSocketCloseCode;

constexpr int kReceiveDrainIterationLimit = 1024;
constexpr int kSendDrainIterationLimit    = 1024;
constexpr int kReceiveBufferBytes         = 4096;
constexpr int kPollingIntervalMs          = 50;

enum class FlushAction {
    Continue,
    Break,
    Return,
    Destroyed,
};

[[nodiscard]] FlushAction handleFlushResult(
    QCWebSocketPrivate *socket, const Internal::QCWebSocketSendQueue::FlushResult &result)
{
    using FlushStatus = Internal::QCWebSocketSendQueue::FlushStatus;
    if (result.status == FlushStatus::Progress) {
        return FlushAction::Continue;
    }
    if (result.status == FlushStatus::Idle) {
        return FlushAction::Break;
    }
    if (result.status == FlushStatus::WouldBlock) {
        if (socket->socketWriteNotifier) {
            socket->socketWriteNotifier->setEnabled(true);
        }
        return FlushAction::Return;
    }
    if (result.status == FlushStatus::FrameCompleted) {
        if (result.curlCode != CURLE_AGAIN) {
            return FlushAction::Continue;
        }
    } else if (result.status == FlushStatus::CloseCompleted) {
        socket->closeFrameSent = true;
        if (socket->peerCloseReceived) {
            if (socket->cleanupConnection() == Internal::SignalEmissionResult::Destroyed) {
                return FlushAction::Destroyed;
            }
            return FlushAction::Return;
        }
        if (result.curlCode != CURLE_AGAIN) {
            return FlushAction::Continue;
        }
    } else if (result.status == FlushStatus::Error) {
        if (result.curlCode != CURLE_OK) {
            if (socket->handleError(QString::fromUtf8(curl_easy_strerror(result.curlCode)))
                == Internal::SignalEmissionResult::Destroyed) {
                return FlushAction::Destroyed;
            }
            if (socket->cleanupConnection() == Internal::SignalEmissionResult::Destroyed) {
                return FlushAction::Destroyed;
            }
        } else {
            if (socket->protocolError(QCWebSocket::CloseCode::InternalError, result.error)
                == Internal::SignalEmissionResult::Destroyed) {
                return FlushAction::Destroyed;
            }
        }
        return FlushAction::Return;
    }

    if (socket->socketWriteNotifier) {
        socket->socketWriteNotifier->setEnabled(!socket->sendQueue.isEmpty());
    }
    return FlushAction::Return;
}

} // namespace

Internal::SignalEmissionResult QCWebSocketPrivate::processIncomingData()
{
    if (state != QCWebSocket::State::Connected && state != QCWebSocket::State::Closing) {
        return Internal::SignalEmissionResult::Alive;
    }

    Q_Q(QCWebSocket);
    for (int i = 0; i < kReceiveDrainIterationLimit; ++i) {
        char buffer[kReceiveBufferBytes];
        size_t received           = 0;
        const curl_ws_frame *meta = nullptr;
        const CURLcode result     = curl_ws_recv(transportHandle(),
                                                 buffer,
                                                 sizeof(buffer),
                                                 &received,
                                                 &meta);
        if (result == CURLE_AGAIN) {
            return Internal::SignalEmissionResult::Alive;
        }
        if (result == CURLE_GOT_NOTHING) {
            lastCloseCode         = QCWebSocket::CloseCode::AbnormalClosure;
            lastWireCloseCode     = static_cast<int>(lastCloseCode);
            hasRetriableCloseCode = true;
            if (handleError(QStringLiteral("WebSocket peer closed without a close frame"))
                == Internal::SignalEmissionResult::Destroyed) {
                return Internal::SignalEmissionResult::Destroyed;
            }
            return cleanupConnection();
        }
        if (result != CURLE_OK) {
            if (handleError(QString::fromUtf8(curl_easy_strerror(result)))
                == Internal::SignalEmissionResult::Destroyed) {
                return Internal::SignalEmissionResult::Destroyed;
            }
            return cleanupConnection();
        }
        if (!meta) {
            return protocolError(QCWebSocket::CloseCode::ProtocolError,
                                 QStringLiteral("WebSocket receive 缺少 frame metadata"));
        }
        if (received == 0 && meta->bytesleft > 0) {
            return protocolError(QCWebSocket::CloseCode::InternalError,
                                 QStringLiteral("WebSocket receive 没有推进 frame offset"));
        }

        const QByteArray data(buffer, static_cast<qsizetype>(received));
        const auto disposition = Internal::processWebSocketFrame(this, q, meta, data);
        if (disposition == Internal::WebSocketFrameDisposition::Destroyed) {
            return Internal::SignalEmissionResult::Destroyed;
        }
        if (disposition == Internal::WebSocketFrameDisposition::Stop) {
            return Internal::SignalEmissionResult::Alive;
        }
    }
    return Internal::SignalEmissionResult::Alive;
}

QCWebSocketCommandResult QCWebSocketPrivate::sendFrame(const QByteArray &data, unsigned int flags)
{
    if (state != QCWebSocket::State::Connected) {
        return QCWebSocketCommandResult::rejected(
            QCWebSocketCommandResult::Status::InvalidState,
            QStringLiteral("当前 WebSocket 状态不接受发送命令"));
    }

    const bool controlFrame = flags & (CURLWS_PING | CURLWS_PONG | CURLWS_CLOSE);
    const qint64 maxFrame   = controlFrame ? WsClose::kControlFrameMaxPayloadBytes
                                           : options.maxFrameBytes();
    if (static_cast<qint64>(data.size()) > maxFrame) {
        return QCWebSocketCommandResult::rejected(
            QCWebSocketCommandResult::Status::InvalidArgument,
            QStringLiteral("WebSocket send frame 超过配置上限"));
    }
    return enqueueFrame(data, flags);
}

QCWebSocketCommandResult QCWebSocketPrivate::enqueueFrame(const QByteArray &data,
                                                          unsigned int flags,
                                                          bool closeFrame)
{
    if (state != QCWebSocket::State::Connected
        && !(state == QCWebSocket::State::Closing && closeFrame)) {
        return QCWebSocketCommandResult::rejected(
            QCWebSocketCommandResult::Status::InvalidState,
            QStringLiteral("当前 WebSocket 状态不接受发送命令"));
    }
    QString queueError;
    if (!sendQueue.enqueue(data, flags, closeFrame, options.maxPendingSendBytes(), &queueError)) {
        return QCWebSocketCommandResult::rejected(
            QCWebSocketCommandResult::Status::QueueLimitReached,
            queueError);
    }
    const auto flushResult = flushSendQueue();
    Q_UNUSED(flushResult);
    return QCWebSocketCommandResult::accepted(data.size());
}

Internal::SignalEmissionResult QCWebSocketPrivate::flushSendQueue()
{
    CURL *curl = transportHandle();
    if (!curl || sendQueue.isEmpty()) {
        if (socketWriteNotifier) {
            socketWriteNotifier->setEnabled(!sendQueue.isEmpty());
        }
        return Internal::SignalEmissionResult::Alive;
    }

    for (int i = 0; i < kSendDrainIterationLimit && !sendQueue.isEmpty(); ++i) {
        const auto result = sendQueue.flushOne(
            [curl](const char *data, size_t size, size_t *sent, unsigned int flags) {
                return curl_ws_send(curl, data, size, sent, 0, flags);
            });
        const FlushAction action = handleFlushResult(this, result);
        if (action == FlushAction::Continue) {
            continue;
        }
        if (action == FlushAction::Destroyed) {
            return Internal::SignalEmissionResult::Destroyed;
        }
        if (action == FlushAction::Return) {
            return Internal::SignalEmissionResult::Alive;
        }
        break;
    }

    if (socketWriteNotifier) {
        socketWriteNotifier->setEnabled(!sendQueue.isEmpty());
    }
    return Internal::SignalEmissionResult::Alive;
}

QCWebSocketCommandResult QCWebSocketPrivate::queueCloseFrame(QCWebSocket::CloseCode closeCode,
                                                             const QByteArray &reason)
{
    QByteArray payload;
    const auto code = static_cast<quint16>(closeCode);
    payload.append(static_cast<char>((code >> WsClose::kCloseCodeByteShift) & 0xFF));
    payload.append(static_cast<char>(code & 0xFF));
    payload.append(WsClose::truncateReason(reason));
    if (setState(QCWebSocket::State::Closing) == Internal::SignalEmissionResult::Destroyed) {
        return QCWebSocketCommandResult::accepted(payload.size());
    }
    return queueClosePayload(payload);
}

QCWebSocketCommandResult QCWebSocketPrivate::queueClosePayload(const QByteArray &payload)
{
    if (closeFrameSent) {
        return QCWebSocketCommandResult::accepted(payload.size());
    }
    if (!closeTimer) {
        Q_Q(QCWebSocket);
        closeTimer = new QTimer(q);
        closeTimer->setSingleShot(true);
        QObject::connect(closeTimer, &QTimer::timeout, q, [this]() {
            static_cast<void>(cleanupConnection());
        });
    }
    closeTimer->start(options.closeHandshakeTimeout());
    return enqueueFrame(payload, CURLWS_CLOSE, true);
}

Internal::SignalEmissionResult QCWebSocketPrivate::protocolError(QCWebSocket::CloseCode closeCode,
                                                                 const QString &message)
{
    errorString = message;
    Q_Q(QCWebSocket);
    if (Internal::emitWebSocketSignal(q,
                                      [message](QCWebSocket *socket) {
                                          Q_EMIT socket->errorOccurred(message);
                                      })
        == Internal::SignalEmissionResult::Destroyed) {
        return Internal::SignalEmissionResult::Destroyed;
    }
    if (state == QCWebSocket::State::Connected || state == QCWebSocket::State::Closing) {
        static_cast<void>(queueCloseFrame(closeCode, message.toUtf8()));
        return Internal::SignalEmissionResult::Alive;
    }
    if (setState(QCWebSocket::State::Unconnected) == Internal::SignalEmissionResult::Destroyed) {
        return Internal::SignalEmissionResult::Destroyed;
    }
    return cleanupConnection();
}

curl_socket_t QCWebSocketPrivate::getSocketDescriptor()
{
    CURL *curl = transportHandle();
    if (!curl) {
        return CURL_SOCKET_BAD;
    }

    curl_socket_t socket  = CURL_SOCKET_BAD;
    const CURLcode result = curl_easy_getinfo(curl, CURLINFO_ACTIVESOCKET, &socket);
    if (result != CURLE_OK || socket == CURL_SOCKET_BAD) {
        qWarning() << "QCWebSocket: 无法获取 socket 描述符:" << curl_easy_strerror(result);
        return CURL_SOCKET_BAD;
    }
    return socket;
}

Internal::SignalEmissionResult QCWebSocketPrivate::enableEventDrivenReceive()
{
    Q_Q(QCWebSocket);
    const curl_socket_t socket = getSocketDescriptor();
    if (socket == CURL_SOCKET_BAD) {
        fallbackToPollingMode();
        return Internal::SignalEmissionResult::Alive;
    }

    socketReadNotifier = new QSocketNotifier(socket, QSocketNotifier::Read, q);
    QObject::connect(socketReadNotifier, &QSocketNotifier::activated, q, [this]() {
        if (processIncomingData() == Internal::SignalEmissionResult::Destroyed) {
            return;
        }
        static_cast<void>(flushSendQueue());
    });
    socketReadNotifier->setEnabled(true);

    socketWriteNotifier = new QSocketNotifier(socket, QSocketNotifier::Write, q);
    QObject::connect(socketWriteNotifier, &QSocketNotifier::activated, q, [this]() {
        static_cast<void>(flushSendQueue());
    });
    socketWriteNotifier->setEnabled(!sendQueue.isEmpty());
    eventDrivenMode = true;
    if (processIncomingData() == Internal::SignalEmissionResult::Destroyed) {
        return Internal::SignalEmissionResult::Destroyed;
    }
    return flushSendQueue();
}

void QCWebSocketPrivate::fallbackToPollingMode()
{
    Q_Q(QCWebSocket);
    if (!receiveTimer) {
        receiveTimer = new QTimer(q);
        QObject::connect(receiveTimer, &QTimer::timeout, q, [this]() {
            if (processIncomingData() == Internal::SignalEmissionResult::Destroyed) {
                return;
            }
            static_cast<void>(flushSendQueue());
        });
    }
    receiveTimer->start(kPollingIntervalMs);
    eventDrivenMode = false;
    qWarning() << "QCWebSocket: 降级到轮询模式（50ms）";
}

} // namespace QCurl

#endif // QCURL_WEBSOCKET_SUPPORT
