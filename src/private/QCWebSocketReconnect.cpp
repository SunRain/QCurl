#include "QCWebSocket_p.h"

#ifdef QCURL_WEBSOCKET_SUPPORT

#include "QCWebSocketCloseCode_p.h"

#include <QDebug>

namespace QCurl {

namespace WsClose = Internal::WebSocketCloseCode;

Internal::SignalEmissionResult QCWebSocketPrivate::handleDisconnection(
    QCWebSocket::CloseCode closeCode)
{
    lastCloseCode         = closeCode;
    lastWireCloseCode     = static_cast<int>(closeCode);
    hasRetriableCloseCode = true;

    if (options.reconnectPolicy().shouldRetry(closeCode, reconnectAttemptCount + 1)) {
        ++reconnectAttemptCount;
        const auto delay = options.reconnectPolicy().delayForAttempt(reconnectAttemptCount);
        qDebug() << "QCWebSocket: Scheduling reconnect attempt" << reconnectAttemptCount << "in"
                 << delay.count() << "ms, close code:" << WsClose::toWire(closeCode);

        Q_Q(QCWebSocket);
        const int attemptCount = reconnectAttemptCount;
        if (Internal::emitWebSocketSignal(q,
                                          [attemptCount, closeCode](QCWebSocket *socket) {
                                              Q_EMIT socket->reconnectAttempt(attemptCount,
                                                                              closeCode);
                                          })
            == Internal::SignalEmissionResult::Destroyed) {
            return Internal::SignalEmissionResult::Destroyed;
        }
        if (!reconnectTimer) {
            reconnectTimer = new QTimer(q);
            reconnectTimer->setSingleShot(true);
            QObject::connect(reconnectTimer, &QTimer::timeout, q, [this]() { attemptReconnect(); });
        }
        reconnectTimer->start(delay.count());
        return Internal::SignalEmissionResult::Alive;
    }

    qDebug() << "QCWebSocket: Not reconnecting, close code:" << WsClose::toWire(closeCode)
             << "attempts:" << reconnectAttemptCount;
    reconnectAttemptCount = 0;
    lastCloseCode         = QCWebSocket::CloseCode::AbnormalClosure;
    lastWireCloseCode     = static_cast<int>(lastCloseCode);
    hasRetriableCloseCode = false;
    if (setState(QCWebSocket::State::Closed) == Internal::SignalEmissionResult::Destroyed) {
        return Internal::SignalEmissionResult::Destroyed;
    }

    Q_Q(QCWebSocket);
    return Internal::emitWebSocketSignal(q, [](QCWebSocket *socket) {
        Q_EMIT socket->disconnected();
    });
}

void QCWebSocketPrivate::attemptReconnect()
{
    qDebug() << "QCWebSocket: Attempting reconnect, attempt" << reconnectAttemptCount;
    Q_Q(QCWebSocket);
    static_cast<void>(q->open());
}

} // namespace QCurl

#endif // QCURL_WEBSOCKET_SUPPORT
