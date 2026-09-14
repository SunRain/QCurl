#include "QCWebSocket_p.h"

#ifdef QCURL_WEBSOCKET_SUPPORT

#include "QCCurlMultiManager.h"
#include "QCCurlOptionAdapter_p.h"
#include "QCWebSocketCurlOptions_p.h"
#include "private/QCCurlRequiredOptionAdapter_p.h"

#include <QDebug>
#include <QPointer>

namespace QCurl {

namespace {

namespace CurlOptions = Internal::CurlOptions;
namespace WsCurl      = Internal::WebSocketCurlOptions;

bool applyStringOption(CURL *curl,
                       QCWebSocketPrivate *d,
                       CurlOptions::Option option,
                       const QByteArray &value)
{
    const char *data = value.isEmpty() ? nullptr : value.constData();
    return WsCurl::apply(CurlOptions::setString(curl, option.id, data), d, option.name);
}

bool configureBaseOptions(CURL *curl, QCWebSocketPrivate *d)
{
    const QByteArray url = d->url.toEncoded(QUrl::FullyEncoded);
    if (!WsCurl::apply(CurlOptions::setString(curl, CURLOPT_URL, url.constData()),
                       d,
                       QCURL_CURL_OPTION(CURLOPT_URL).name)
        || !WsCurl::apply(CurlOptions::setConnectOnlyWebSocket(curl),
                          d,
                          QCURL_CURL_OPTION(CURLOPT_CONNECT_ONLY).name)) {
        return false;
    }

    if (!WsCurl::apply(CurlOptions::setWebSocketNoAutoPong(curl),
                       d,
                       QCURL_CURL_OPTION(CURLOPT_WS_OPTIONS).name)) {
        return false;
    }

    return WsCurl::apply(CurlOptions::setConnectTimeout(curl, d->options.connectTimeout()),
                         d,
                         QCURL_CURL_OPTION(CURLOPT_CONNECTTIMEOUT_MS).name);
}

void loadTlsStrings(QCWebSocketPrivate *d, const QCNetworkSslConfig &config)
{
    d->sslCaInfoUtf8      = config.caCertPath().toUtf8();
    d->sslCertUtf8        = config.clientCertPath().toUtf8();
    d->sslKeyUtf8         = config.clientKeyPath().toUtf8();
    d->sslKeyPasswordUtf8 = config.clientKeyPassword().toUtf8();
}

bool applyTlsStrings(CURL *curl, QCWebSocketPrivate *d)
{
    return applyStringOption(curl, d, QCURL_CURL_OPTION(CURLOPT_CAINFO), d->sslCaInfoUtf8)
           && applyStringOption(curl, d, QCURL_CURL_OPTION(CURLOPT_SSLCERT), d->sslCertUtf8)
           && applyStringOption(curl, d, QCURL_CURL_OPTION(CURLOPT_SSLKEY), d->sslKeyUtf8)
           && applyStringOption(curl,
                                d,
                                QCURL_CURL_OPTION(CURLOPT_KEYPASSWD),
                                d->sslKeyPasswordUtf8);
}

bool configureTlsOptions(CURL *curl, QCWebSocketPrivate *d)
{
    if (d->url.scheme() != QStringLiteral("wss")) {
        d->sslCaInfoUtf8.clear();
        d->sslCertUtf8.clear();
        d->sslKeyUtf8.clear();
        d->sslKeyPasswordUtf8.clear();
        return applyTlsStrings(curl, d);
    }

    const QCNetworkSslConfig config = d->options.sslConfig();
    if (!WsCurl::apply(CurlOptions::setSslVerifyPeer(curl, config.verifyPeer()),
                       d,
                       QCURL_CURL_OPTION(CURLOPT_SSL_VERIFYPEER).name)
        || !WsCurl::apply(CurlOptions::setSslVerifyHost(curl, config.verifyHost()),
                          d,
                          QCURL_CURL_OPTION(CURLOPT_SSL_VERIFYHOST).name)) {
        return false;
    }

    loadTlsStrings(d, config);
    return applyTlsStrings(curl, d);
}

bool configureOpen(CURL *curl, QCWebSocketPrivate *d)
{
    return configureBaseOptions(curl, d) && configureTlsOptions(curl, d);
}

bool isSslError(CURLcode code)
{
    return code == CURLE_SSL_CONNECT_ERROR || code == CURLE_PEER_FAILED_VERIFICATION
           || code == CURLE_SSL_CERTPROBLEM || code == CURLE_SSL_CIPHER || code == CURLE_SSL_CACERT;
}

Internal::SignalEmissionResult reportSslError(CURL *curl,
                                              CURLcode code,
                                              const QString &message,
                                              QCWebSocket *q)
{
    if (!isSslError(code)) {
        return Internal::SignalEmissionResult::Alive;
    }

    QStringList errors{message};
    long verifyResult         = 0;
    const CURLcode infoResult = Internal::CurlInfoAdapter::getSslVerifyResult(curl, &verifyResult);
    if (infoResult != CURLE_OK) {
        errors << QStringLiteral("SSL 验证详细诊断不可用");
    } else if (verifyResult != 0) {
        errors << QStringLiteral("SSL 验证结果码: %1").arg(verifyResult);
    }
    return Internal::emitWebSocketSignal(q, [errors](QCWebSocket *socket) {
        Q_EMIT socket->sslErrorsDetailed(errors);
    });
}

void setFailureCloseCode(QCWebSocketPrivate *d, CURLcode code)
{
    d->lastCloseCode         = code == CURLE_COULDNT_CONNECT || code == CURLE_COULDNT_RESOLVE_HOST
                                   ? QCWebSocket::CloseCode::AbnormalClosure
                                   : QCWebSocket::CloseCode::ProtocolError;
    d->lastWireCloseCode     = static_cast<int>(d->lastCloseCode);
    d->hasRetriableCloseCode = true;
}

Internal::SignalEmissionResult handleOpenFailure(CURL *curl,
                                                 CURLcode code,
                                                 QCWebSocketPrivate *d,
                                                 QCWebSocket *q)
{
    const QString message = QString::fromUtf8(curl_easy_strerror(code));
    setFailureCloseCode(d, code);
    if (reportSslError(curl, code, message, q) == Internal::SignalEmissionResult::Destroyed) {
        return Internal::SignalEmissionResult::Destroyed;
    }
    return d->handleError(message);
}

Internal::SignalEmissionResult finishOpen(QCWebSocketPrivate *d, QCWebSocket *q)
{
    d->reconnectAttemptCount = 0;
    d->lastCloseCode         = QCWebSocket::CloseCode::AbnormalClosure;
    d->lastWireCloseCode     = static_cast<int>(d->lastCloseCode);
    d->hasRetriableCloseCode = false;

    if (d->setState(QCWebSocket::State::Connected) == Internal::SignalEmissionResult::Destroyed) {
        return Internal::SignalEmissionResult::Destroyed;
    }
    if (Internal::emitWebSocketSignal(q, [](QCWebSocket *socket) { Q_EMIT socket->connected(); })
        == Internal::SignalEmissionResult::Destroyed) {
        return Internal::SignalEmissionResult::Destroyed;
    }
    return d->enableEventDrivenReceive();
}

void performOpen(QCWebSocketPrivate *d, QCWebSocket *q)
{
    if (d->state != QCWebSocket::State::Connecting) {
        return;
    }

    QPointer<QCWebSocket> safeSocket(q);
    QCCurlMultiManager::TransferToken token = 0;
    QString addError;
    const bool added = registerPersistentTransfer(
        [safeSocket, d](CURL *curl, QString *error) {
            if (!safeSocket) {
                return false;
            }
            d->managedCurlHandle  = curl;
            const bool configured = configureOpen(curl, d);
            if (!safeSocket) {
                return false;
            }
            if (!configured && error && d->errorString.isEmpty()) {
                *error = QStringLiteral("WebSocket easy handle 配置失败");
            }
            return configured && safeSocket;
        },
        [safeSocket, d](quintptr readyToken, CURLcode result, long httpStatus) {
            Q_UNUSED(httpStatus);
            if (!safeSocket) {
                return;
            }
            d->onHandshakeFinished(readyToken, result, httpStatus);
        },
        &token,
        &addError);
    if (!added) {
        if (!safeSocket) {
            return;
        }
        // Core owns and destroys the temporary easy handle when registration
        // fails; clear the borrowed observer before any synchronously reentrant
        // error signal can call cleanupConnection()/abort().
        d->managedCurlHandle = nullptr;
        if (d->errorString.isEmpty()) {
            const auto result = d->handleError(
                addError.isEmpty() ? q->tr("无法注册 WebSocket multi 握手") : addError);
            if (result == Internal::SignalEmissionResult::Destroyed) {
                return;
            }
        }
        d->resetTransport();
        return;
    }

    d->transferToken = token;
}

} // namespace

Internal::SignalEmissionResult QCWebSocketPrivate::setState(QCWebSocket::State newState)
{
    if (state == newState) {
        return Internal::SignalEmissionResult::Alive;
    }

    const bool wasValid = state == QCWebSocket::State::Connected;
    state               = newState;
    const bool valid    = state == QCWebSocket::State::Connected;

    Q_Q(QCWebSocket);
    if (Internal::emitWebSocketSignal(q,
                                      [newState](QCWebSocket *socket) {
                                          Q_EMIT socket->stateChanged(newState);
                                      })
        == Internal::SignalEmissionResult::Destroyed) {
        return Internal::SignalEmissionResult::Destroyed;
    }
    if (wasValid != valid) {
        return Internal::emitWebSocketSignal(q, [valid](QCWebSocket *socket) {
            Q_EMIT socket->isValidChanged(valid);
        });
    }
    return Internal::SignalEmissionResult::Alive;
}

Internal::SignalEmissionResult QCWebSocketPrivate::handleError(const QString &error)
{
    errorString = error;
    if (setState(QCWebSocket::State::Unconnected) == Internal::SignalEmissionResult::Destroyed) {
        return Internal::SignalEmissionResult::Destroyed;
    }
    Q_Q(QCWebSocket);
    return Internal::emitWebSocketSignal(q, [error](QCWebSocket *socket) {
        Q_EMIT socket->errorOccurred(error);
    });
}

void QCWebSocketPrivate::onHandshakeFinished(quintptr token, CURLcode result, long httpStatus)
{
    Q_UNUSED(httpStatus);
    if (token == 0 || transferToken != token) {
        return;
    }
    static_cast<void>(clearRequestHeaders());

    Q_Q(QCWebSocket);
    if (result == CURLE_ABORTED_BY_CALLBACK) {
        transferToken = 0;
        resetTransport();
        if (setState(QCWebSocket::State::Closed) == Internal::SignalEmissionResult::Destroyed) {
            return;
        }
        static_cast<void>(Internal::emitWebSocketSignal(q, [](QCWebSocket *socket) {
            Q_EMIT socket->disconnected();
        }));
        return;
    }
    if (result != CURLE_OK) {
        if (handleOpenFailure(managedCurlHandle, result, this, q)
            == Internal::SignalEmissionResult::Destroyed) {
            return;
        }
        transferToken = 0;
        resetTransport();
        removePersistentTransfer(token);
        if (options.reconnectPolicy().shouldRetry(lastCloseCode, reconnectAttemptCount + 1)) {
            const auto reconnectResult = handleDisconnection(lastCloseCode);
            Q_UNUSED(reconnectResult);
        }
        return;
    }

    finishOpen(this, q);
}

CURL *QCWebSocketPrivate::transportHandle() const noexcept
{
    return managedCurlHandle;
}

Internal::SignalEmissionResult QCWebSocketPrivate::cleanupConnection()
{
    static_cast<void>(clearRequestHeaders());
    const quintptr token = transferToken;
    transferToken        = 0;
    if (socketReadNotifier) {
        socketReadNotifier->setEnabled(false);
        QObject::disconnect(socketReadNotifier, nullptr, q_ptr, nullptr);
        socketReadNotifier->deleteLater();
        socketReadNotifier = nullptr;
    }
    if (socketWriteNotifier) {
        socketWriteNotifier->setEnabled(false);
        QObject::disconnect(socketWriteNotifier, nullptr, q_ptr, nullptr);
        socketWriteNotifier->deleteLater();
        socketWriteNotifier = nullptr;
    }
    if (receiveTimer) {
        receiveTimer->stop();
    }
    if (closeTimer) {
        closeTimer->stop();
    }

    eventDrivenMode = false;
    fragmentBuffer.clear();
    fragmentTypeFlags = 0;
    currentFrameBytes = 0;
    controlFrameBuffer.clear();
    controlFrameFlags = 0;
    sendQueue.clear();
    closeFrameSent    = false;
    peerCloseReceived = false;
    resetTransport();

    if (token != 0) {
        removePersistentTransfer(token);
    }
    static_cast<void>(clearRequestHeaders());

    if (hasRetriableCloseCode
        && options.reconnectPolicy().shouldRetry(lastCloseCode, reconnectAttemptCount + 1)) {
        const QCWebSocket::CloseCode closeCode = lastCloseCode;
        if (setState(QCWebSocket::State::Unconnected) == Internal::SignalEmissionResult::Destroyed) {
            return Internal::SignalEmissionResult::Destroyed;
        }
        return handleDisconnection(closeCode);
    }

    if (setState(QCWebSocket::State::Closed) == Internal::SignalEmissionResult::Destroyed) {
        return Internal::SignalEmissionResult::Destroyed;
    }
    Q_Q(QCWebSocket);
    return Internal::emitWebSocketSignal(q, [](QCWebSocket *socket) {
        Q_EMIT socket->disconnected();
    });
}

namespace Internal {

Q_DECL_HIDDEN void scheduleWebSocketOpen(QCWebSocket *q, QCWebSocketPrivate *d)
{
    QTimer::singleShot(0, q, [q, d]() { performOpen(d, q); });
}

} // namespace Internal

} // namespace QCurl

#endif // QCURL_WEBSOCKET_SUPPORT
