#include <QCNetworkDiagnostics.h>
#include <QCNetworkMiddlewareExtras.h>

#ifdef QCURL_WEBSOCKET_SUPPORT
#include <QCWebSocket.h>
#include <QCWebSocketCompressionConfig.h>
#include <QCWebSocketPool.h>
#endif

#include <QMetaObject>
#include <QString>
#include <QUrl>
#include <QVariantMap>

#include <chrono>

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

    QCurl::QCRedactingLoggingMiddleware redactingLog;
    QCurl::QCObservabilityMiddleware observability;
    if (redactingLog.name() != QStringLiteral("QCRedactingLoggingMiddleware")
        || observability.name() != QStringLiteral("QCObservabilityMiddleware")) {
        return 3;
    }

#ifdef QCURL_WEBSOCKET_SUPPORT
    QCurl::QCWebSocketOptions socketOptions;
    if (!socketOptions.setConnectTimeout(std::chrono::seconds{3}, &optionError)) {
        return 4;
    }
    QCurl::QCWebSocketCompressionConfig compression
        = QCurl::QCWebSocketCompressionConfig::defaultConfig();
    socketOptions.setCompressionConfig(compression);
    socketOptions.setAutoPongEnabled(false);
    QCurl::QCWebSocket socket(QUrl(QStringLiteral("wss://example.invalid")), socketOptions);
    if (socket.options().connectTimeout() != std::chrono::seconds{3}
        || socket.options().autoPongEnabled() || !compression.enabled()
        || !socket.options().compressionConfig().enabled()
        || socket.state() != QCurl::QCWebSocket::State::Unconnected) {
        return 5;
    }
    const QMetaObject *socketMeta = socket.metaObject();
    const int closeSignalIndex    = socketMeta->indexOfSignal(
        QMetaObject::normalizedSignature("closeReceived(int,QString)").constData());
    if (closeSignalIndex < 0) {
        return 8;
    }

    QCurl::QCWebSocketPoolConfig poolConfig;
    poolConfig.setMaxPoolSize(4);
    poolConfig.setMaxIdleTime(45);

    QCurl::QCWebSocketPool pool(poolConfig);
    const QCurl::QCWebSocketPoolConfig savedPoolConfig = pool.config();
    const QCurl::QCWebSocketPoolStats poolStats        = pool.statistics();
    if (savedPoolConfig.maxPoolSize() != 4 || savedPoolConfig.maxIdleTime() != 45
        || poolStats.totalConnections() != 0 || poolStats.hitRate() != 0.0) {
        return 6;
    }
#endif

    return 0;
}
