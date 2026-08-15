#include "QCWebSocket.h"

#ifdef QCURL_WEBSOCKET_SUPPORT

#include "QCWebSocket_p.h"
#include "private/QCWebSocketCloseCode_p.h"

#include <QDebug>
#include <QThread>
#include <QTimer>

namespace QCurl {

namespace {

namespace WsClose = Internal::WebSocketCloseCode;

bool isConfigurationLockedState(QCWebSocket::State state)
{
    return state == QCWebSocket::State::Connecting || state == QCWebSocket::State::Connected
           || state == QCWebSocket::State::Closing;
}

bool failOption(QString *error, const QString &message)
{
    if (error) {
        *error = message;
    }
    return false;
}

QCWebSocketCommandResult rejectWrongThread(const QCWebSocket *socket)
{
    if (QThread::currentThread() == socket->thread()) {
        return QCWebSocketCommandResult::accepted();
    }
    return QCWebSocketCommandResult::rejected(
        QCWebSocketCommandResult::Status::WrongThread,
        QStringLiteral("WebSocket 命令只能在 socket owner thread 调用"));
}

} // namespace

QCWebSocket::QCWebSocket(const QUrl &url, const QCWebSocketOptions &options, QObject *parent)
    : QObject(parent)
    , d_ptr(new QCWebSocketPrivate(this))
{
    Q_D(QCWebSocket);
    d->url     = url;
    d->options = options;
}

QCWebSocket::~QCWebSocket()
{
    Q_D(QCWebSocket);
    d->teardownForDestruction();
}

QCWebSocketCommandResult QCWebSocket::open()
{
    const auto threadResult = rejectWrongThread(this);
    if (!threadResult.isAccepted()) {
        return threadResult;
    }
    Q_D(QCWebSocket);
    if (isConfigurationLockedState(d->state)) {
        return QCWebSocketCommandResult::rejected(
            QCWebSocketCommandResult::Status::InvalidState,
            QStringLiteral("当前 WebSocket 状态不接受 open 命令"));
    }

    d->errorString.clear();
    if (d->setState(State::Connecting) == Internal::SignalEmissionResult::Destroyed) {
        return QCWebSocketCommandResult::accepted();
    }
    Internal::scheduleWebSocketOpen(this, d);
    return QCWebSocketCommandResult::accepted();
}

QCWebSocketCommandResult QCWebSocket::close(CloseCode closeCode, const QString &reason)
{
    const auto threadResult = rejectWrongThread(this);
    if (!threadResult.isAccepted()) {
        return threadResult;
    }
    Q_D(QCWebSocket);
    if (d->state != State::Connected) {
        return QCWebSocketCommandResult::rejected(
            QCWebSocketCommandResult::Status::InvalidState,
            QStringLiteral("当前 WebSocket 状态不接受 close 命令"));
    }

    const int wireCode = WsClose::toWire(closeCode);
    if (!WsClose::isValidWire(wireCode)) {
        return QCWebSocketCommandResult::rejected(
            QCWebSocketCommandResult::Status::InvalidArgument,
            QStringLiteral("不能发送非法 WebSocket close code: %1").arg(wireCode));
    }

    return d->queueCloseFrame(closeCode, reason.toUtf8());
}

void QCWebSocket::abort()
{
    Q_D(QCWebSocket);
    const auto result = d->cleanupConnection();
    Q_UNUSED(result);
}

QCWebSocketCommandResult QCWebSocket::sendTextMessage(const QString &message)
{
    const auto threadResult = rejectWrongThread(this);
    if (!threadResult.isAccepted()) {
        return threadResult;
    }
    Q_D(QCWebSocket);
    return d->sendFrame(message.toUtf8(), CURLWS_TEXT);
}

QCWebSocketCommandResult QCWebSocket::sendBinaryMessage(const QByteArray &data)
{
    const auto threadResult = rejectWrongThread(this);
    if (!threadResult.isAccepted()) {
        return threadResult;
    }
    Q_D(QCWebSocket);
    return d->sendFrame(data, CURLWS_BINARY);
}

QCWebSocketCommandResult QCWebSocket::ping(const QByteArray &payload)
{
    const auto threadResult = rejectWrongThread(this);
    if (!threadResult.isAccepted()) {
        return threadResult;
    }
    Q_D(QCWebSocket);
    if (d->state != State::Connected) {
        return QCWebSocketCommandResult::rejected(
            QCWebSocketCommandResult::Status::InvalidState,
            QStringLiteral("当前 WebSocket 状态不接受 ping 命令"));
    }

    if (payload.size() > WsClose::kControlFrameMaxPayloadBytes) {
        return QCWebSocketCommandResult::rejected(
            QCWebSocketCommandResult::Status::InvalidArgument,
            QStringLiteral("WebSocket ping payload 超过 125 字节"));
    }
    return d->sendFrame(payload, CURLWS_PING);
}

QCWebSocketCommandResult QCWebSocket::pong(const QByteArray &payload)
{
    const auto threadResult = rejectWrongThread(this);
    if (!threadResult.isAccepted()) {
        return threadResult;
    }
    Q_D(QCWebSocket);
    if (d->state != State::Connected) {
        return QCWebSocketCommandResult::rejected(
            QCWebSocketCommandResult::Status::InvalidState,
            QStringLiteral("当前 WebSocket 状态不接受 pong 命令"));
    }

    if (payload.size() > WsClose::kControlFrameMaxPayloadBytes) {
        return QCWebSocketCommandResult::rejected(
            QCWebSocketCommandResult::Status::InvalidArgument,
            QStringLiteral("WebSocket pong payload 超过 125 字节"));
    }
    return d->sendFrame(payload, CURLWS_PONG);
}

QCWebSocketOptions QCWebSocket::options() const
{
    Q_D(const QCWebSocket);
    return d->options;
}

bool QCWebSocket::setOptions(const QCWebSocketOptions &options, QString *error)
{
    Q_D(QCWebSocket);
    if (isConfigurationLockedState(d->state)) {
        return failOption(error, QStringLiteral("活动连接状态不允许修改 WebSocket 配置"));
    }

    d->options = options;
    return true;
}

QCWebSocket::State QCWebSocket::state() const
{
    Q_D(const QCWebSocket);
    return d->state;
}

QUrl QCWebSocket::url() const
{
    Q_D(const QCWebSocket);
    return d->url;
}

QString QCWebSocket::errorString() const
{
    Q_D(const QCWebSocket);
    return d->errorString;
}

bool QCWebSocket::isValid() const
{
    Q_D(const QCWebSocket);
    return d->state == State::Connected;
}

} // namespace QCurl

#endif // QCURL_WEBSOCKET_SUPPORT
