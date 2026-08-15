#include <QCNetworkDiagnostics.h>
#include <QCNetworkMiddlewareExtras.h>

#ifdef QCURL_WEBSOCKET_SUPPORT
#include <QCWebSocketCommandResult.h>
#include <QCWebSocket.h>
#include <QCWebSocketPool.h>
#endif

#include <QFuture>
#include <QMetaObject>
#include <QMetaProperty>
#include <QString>
#include <QUrl>
#include <QVariantMap>

#include <chrono>
#include <type_traits>

int main()
{
    QCurl::DiagResult result;
    result.setSuccess(true);
    result.setSummary(QStringLiteral("other-extras"));
    result.setDetail(QStringLiteral("scope"), QStringLiteral("diagnostics"));
    result.setDurationMs(1);
    result.setTimestamp(QDateTime::currentDateTimeUtc());

    const auto printable = result.toString();
    if (!printable.contains(QStringLiteral("other-extras"))
        || !result.details().contains(QStringLiteral("scope"))) {
        return 1;
    }

    QString optionError;
    QCurl::QCNetworkDiagnosticsOptions diagnosticsOptions;
    if (!diagnosticsOptions.setTimeout(std::chrono::milliseconds{2500}, &optionError)
        || !diagnosticsOptions.setPort(443, &optionError)
        || !diagnosticsOptions.setPingCount(2, &optionError)
        || !diagnosticsOptions.setTracerouteMaxHops(8, &optionError)
        || diagnosticsOptions.timeout() != std::chrono::milliseconds{2500}
        || diagnosticsOptions.port() != 443 || diagnosticsOptions.pingCount() != 2
        || diagnosticsOptions.tracerouteMaxHops() != 8) {
        return 2;
    }
    if (diagnosticsOptions.setTimeout(std::chrono::milliseconds{60001}, &optionError)
        || diagnosticsOptions.setPingCount(101, &optionError)
        || diagnosticsOptions.setTracerouteMaxHops(256, &optionError)) {
        return 7;
    }

    static_assert(
        std::is_same_v<decltype(QCurl::QCNetworkDiagnostics::resolveDNS(QStringLiteral("localhost"),
                                                                        diagnosticsOptions)),
                       QFuture<QCurl::DiagResult>>);

    QCurl::QCRedactingLoggingMiddleware redactingLog;
    QCurl::QCObservabilityMiddleware observability;
    if (redactingLog.name() != QStringLiteral("QCRedactingLoggingMiddleware")
        || observability.name() != QStringLiteral("QCObservabilityMiddleware")) {
        return 3;
    }

#ifdef QCURL_WEBSOCKET_SUPPORT
    QCurl::QCWebSocketOptions socketOptions;
    if (!socketOptions.setConnectTimeout(std::chrono::seconds{3}, &optionError)
        || !socketOptions.setMaxFrameBytes(1024 * 1024, &optionError)
        || !socketOptions.setMaxMessageBytes(2 * 1024 * 1024, &optionError)
        || !socketOptions.setMaxPendingSendBytes(512 * 1024, &optionError)
        || !socketOptions.setMaxReceiveBufferBytes(2 * 1024 * 1024, &optionError)
        || !socketOptions.setCloseHandshakeTimeout(std::chrono::seconds{2}, &optionError)) {
        return 4;
    }
    socketOptions.setAutoPongEnabled(false);
    QCurl::QCWebSocket socket(QUrl(QStringLiteral("wss://example.invalid")), socketOptions);
    static_assert(std::is_same_v<decltype(socket.open()), QCurl::QCWebSocketCommandResult>);
    static_assert(std::is_same_v<decltype(socket.close()), QCurl::QCWebSocketCommandResult>);
    static_assert(std::is_same_v<decltype(socket.sendTextMessage(QString{})),
                                 QCurl::QCWebSocketCommandResult>);
    static_assert(std::is_same_v<decltype(socket.sendBinaryMessage(QByteArray{})),
                                 QCurl::QCWebSocketCommandResult>);
    static_assert(std::is_same_v<decltype(socket.ping()), QCurl::QCWebSocketCommandResult>);
    static_assert(std::is_same_v<decltype(socket.pong()), QCurl::QCWebSocketCommandResult>);
    if (socket.options().connectTimeout() != std::chrono::seconds{3}
        || socket.options().maxFrameBytes() != 1024 * 1024
        || socket.options().maxMessageBytes() != 2 * 1024 * 1024
        || socket.options().maxPendingSendBytes() != 512 * 1024
        || socket.options().maxReceiveBufferBytes() != 2 * 1024 * 1024
        || socket.options().closeHandshakeTimeout() != std::chrono::seconds{2}
        || socket.options().autoPongEnabled()
        || socket.state() != QCurl::QCWebSocket::State::Unconnected) {
        return 5;
    }
    const QMetaObject *socketMeta = socket.metaObject();
    const int closeSignalIndex    = socketMeta->indexOfSignal(
        QMetaObject::normalizedSignature("closeReceived(int,QString)").constData());
    if (closeSignalIndex < 0) {
        return 8;
    }
    const int isValidPropertyIndex = socketMeta->indexOfProperty("isValid");
    if (isValidPropertyIndex < 0) {
        return 9;
    }
    const QMetaProperty isValidProperty = socketMeta->property(isValidPropertyIndex);
    if (!isValidProperty.hasNotifySignal() || !isValidProperty.isFinal()
        || isValidProperty.notifySignal().methodSignature()
               != QByteArrayLiteral("isValidChanged(bool)")) {
        return 9;
    }

    QCurl::QCWebSocketPoolConfig poolConfig;
    poolConfig.setMaxPoolSize(4);
    poolConfig.setMaxIdleTime(45);

    QCurl::QCWebSocketPool pool(poolConfig);
    static_assert(
        std::is_same_v<decltype(pool.acquire(QUrl{})), QFuture<QCurl::QCWebSocketAcquireResult>>);
    static_assert(
        std::is_same_v<decltype(pool.preWarm(QUrl{}, 1)), QFuture<QCurl::QCWebSocketPreWarmResult>>);
    static_assert(std::is_same_v<decltype(pool.release(QCurl::QCWebSocketPool::LeaseId{})),
                                 QCurl::QCWebSocketPool::LeaseResult>);
    QCurl::QCWebSocket *resolvedSocket = nullptr;
    static_assert(std::is_same_v<decltype(pool.resolveLease(QCurl::QCWebSocketPool::LeaseId{},
                                                            &resolvedSocket)),
                                 QCurl::QCWebSocketPool::LeaseResult>);
    static_assert(std::is_same_v<decltype(pool.clearPool()), bool>);
    static_assert(std::is_same_v<decltype(pool.setConfig(poolConfig)), bool>);
    const auto wrongThreadAcquire = QCurl::QCWebSocketAcquireResult::failure(
        QCurl::QCWebSocketAcquireResult::Status::WrongThread,
        QStringLiteral("owner thread required"));
    QString poolError = QStringLiteral("stale error");
    if (!pool.setConfig(poolConfig, &poolError) || !poolError.isEmpty()
        || !pool.clearPool({}, &poolError) || !poolError.isEmpty()
        || pool.release(0) != QCurl::QCWebSocketPool::LeaseResult::UnknownLease
        || pool.resolveLease(0, &resolvedSocket) != QCurl::QCWebSocketPool::LeaseResult::UnknownLease
        || resolvedSocket != nullptr) {
        return 10;
    }
    const QCurl::QCWebSocketPoolConfig savedPoolConfig = pool.config();
    const QCurl::QCWebSocketPoolStats poolStats        = pool.statistics();
    if (savedPoolConfig.maxPoolSize() != 4 || savedPoolConfig.maxIdleTime() != 45
        || poolStats.totalConnections() != 0 || poolStats.hitRate() != 0.0
        || wrongThreadAcquire.status() != QCurl::QCWebSocketAcquireResult::Status::WrongThread
        || wrongThreadAcquire.leaseId() != 0) {
        return 6;
    }
#endif

    return 0;
}
