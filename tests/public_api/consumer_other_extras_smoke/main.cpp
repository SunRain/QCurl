#include <QCNetworkDiagnostics.h>
#include <QCNetworkMiddlewareExtras.h>

#include <QString>
#include <QVariantMap>

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

    return 0;
}
