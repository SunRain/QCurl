#include <QCNetworkDiagnostics.h>
#include <QCNetworkMiddlewareExtras.h>

#ifdef QCURL_WEBSOCKET_SUPPORT
#include <QCWebSocket.h>
#include <QCWebSocketCompressionConfig.h>
#include <QCWebSocketPool.h>
#endif

#include <QString>
#include <QVariantMap>
#include <QUrl>

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

    QCurl::QCRedactingLoggingMiddleware redactingLog;
    QCurl::QCObservabilityMiddleware observability;
    if (redactingLog.name() != QStringLiteral("QCRedactingLoggingMiddleware")
        || observability.name() != QStringLiteral("QCObservabilityMiddleware")) {
        return 2;
    }

#ifdef QCURL_WEBSOCKET_SUPPORT
    QCurl::QCWebSocket socket(QUrl(QStringLiteral("wss://example.invalid")));
    QCurl::QCWebSocketCompressionConfig compression =
        QCurl::QCWebSocketCompressionConfig::defaultConfig();
    socket.setCompressionConfig(compression);
    if (!compression.enabled()
        || socket.state() != QCurl::QCWebSocket::State::Unconnected) {
        return 3;
    }

    QCurl::QCWebSocketPoolConfig poolConfig;
    poolConfig.setMaxPoolSize(4);
    poolConfig.setMaxIdleTime(45);

    QCurl::QCWebSocketPool pool(poolConfig);
    const QCurl::QCWebSocketPoolConfig savedPoolConfig = pool.config();
    const QCurl::QCWebSocketPoolStats poolStats = pool.statistics();
    if (savedPoolConfig.maxPoolSize() != 4
        || savedPoolConfig.maxIdleTime() != 45
        || poolStats.totalConnections() != 0
        || poolStats.hitRate() != 0.0) {
        return 4;
    }
#endif

    return 0;
}
