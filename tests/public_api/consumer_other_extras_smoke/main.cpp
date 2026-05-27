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
    if (!compression.enabled
        || socket.state() != QCurl::QCWebSocket::State::Unconnected) {
        return 3;
    }
#endif

    return 0;
}
