#include <QCNetworkDiagnostics.h>
#include <QCNetworkMiddlewareExtras.h>

#ifdef QCURL_WEBSOCKET_SUPPORT
#include <QCWebSocket.h>
#include <QCWebSocketCompressionConfig.h>
#endif

#include <QString>
#include <QVariantMap>
#include <QUrl>

int main()
{
    QCurl::DiagResult result;
    result.success = true;
    result.summary = QStringLiteral("other-extras");
    result.details.insert(QStringLiteral("scope"), QStringLiteral("diagnostics"));
    result.durationMs = 1;
    result.timestamp = QDateTime::currentDateTimeUtc();

    const auto printable = result.toString();
    if (!printable.contains(QStringLiteral("other-extras"))
        || !result.details.contains(QStringLiteral("scope"))) {
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
    if (!compression.enabled
        || socket.state() != QCurl::QCWebSocket::State::Unconnected) {
        return 3;
    }
#endif

    return 0;
}
