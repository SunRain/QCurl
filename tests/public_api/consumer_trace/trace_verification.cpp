#include "trace_contract.h"

#include <QDebug>
#include <QMap>
#include <QRegularExpression>
#include <QSet>

namespace {

struct Association
{
    QString requestId;
    QString responseId;
    QString targetId;
    QString requestConnection;
    QString responseConnection;
};

bool correlate(const TraceObservation &observation, const QStringList &lines)
{
    static const QRegularExpression kLine(
        QStringLiteral(R"(^(HEADER_IN|HEADER_OUT): \[(\d+)-(\d+)\] (.*)$)"));
    QMap<QString, Association> associations;
    for (const QString &line : lines) {
        const auto match = kLine.match(line);
        if (!match.hasMatch()) {
            continue;
        }
        const QString transfer   = match.captured(2);
        const QString connection = match.captured(3);
        const QString body       = match.captured(4);
        for (qsizetype i = 0; i < observation.requestMarkers.size(); ++i) {
            const auto &marker = observation.requestMarkers.at(i);
            auto &association  = associations[marker];
            if (match.captured(1) == QStringLiteral("HEADER_OUT")) {
                if (body.compare(QStringLiteral("X-Public-Id: ") + marker, Qt::CaseInsensitive)
                    == 0) {
                    association.requestId         = transfer;
                    association.requestConnection = connection;
                }
                if (body.startsWith(QStringLiteral("GET ") + observation.targets.at(i)
                                    + QLatin1Char(' '))) {
                    association.targetId = transfer;
                }
            } else if (body.compare(QStringLiteral("X-Response-Marker: ")
                                        + observation.responseMarkers.at(i),
                                    Qt::CaseInsensitive)
                       == 0) {
                association.responseId         = transfer;
                association.responseConnection = connection;
            }
        }
    }
    QSet<QString> transfers;
    for (const QString &marker : observation.requestMarkers) {
        const auto association = associations.value(marker);
        if (association.requestId.isEmpty() || association.requestId != association.responseId
            || association.requestConnection != association.responseConnection
            || (observation.policy == QCurl::QCNetworkDiagnosticQueryPolicy::Full
                && association.requestId != association.targetId)) {
            qCritical() << "并发请求的 target/安全请求头/响应 marker 与 transfer ID 未一一对应";
            return false;
        }
        transfers.insert(association.requestId);
        qInfo() << "association" << marker << "transfer=" << association.requestId
                << "connection=" << association.requestConnection;
    }
    return transfers.size() == observation.requestMarkers.size();
}

} // namespace

bool verifyTrace(const TraceObservation &observation)
{
    QStringList lines;
    bool hasTarget          = false;
    bool hasSensitiveHeader = false;
    bool hasResponse        = false;
    static const QRegularExpression kIds(QStringLiteral(R"(^[A-Z_]+: \[(\d+|x)-(\d+|x)\] )"));
    for (const auto &entry : observation.entries) {
        if (entry.category() != QStringLiteral("Trace")) {
            continue;
        }
        if (entry.message().size() > 4096) {
            qCritical() << "trace 输出超限";
            return false;
        }
        const auto messageLines = entry.message().split(QLatin1Char('\n'));
        for (const QString &line : messageLines) {
            if (line.contains(QStringLiteral("request-secret"))
                || line.contains(QStringLiteral("response-secret"))
                || line.contains(QStringLiteral("session=response-secret"))) {
                qCritical() << "敏感测试头值进入 trace";
                return false;
            }
            if (!observation.idsEnabled && kIds.match(line).hasMatch()) {
                qCritical() << "关闭 lib-ids 后出现补造的关联 ID";
                return false;
            }
            hasTarget |= line.contains(QStringLiteral("GET "));
            hasSensitiveHeader |= line.contains(QStringLiteral("set-cookie: [REDACTED]"),
                                                Qt::CaseInsensitive);
            hasResponse |= line.contains(QStringLiteral("X-Response-Marker:"), Qt::CaseInsensitive);
            lines.append(line);
        }
    }
    const QString trace = lines.join(QLatin1Char('\n'));
    if (!hasTarget || !hasSensitiveHeader || !hasResponse) {
        qCritical() << "缺少真实 target、脱敏响应头或响应 marker";
        return false;
    }
    for (const QString &target : observation.targets) {
        const QString expected = observation.policy == QCurl::QCNetworkDiagnosticQueryPolicy::Full
                                     ? target
                                     : target.first(target.indexOf(QLatin1Char('?')))
                                           + QStringLiteral("?[REDACTED]");
        if (!trace.contains(QStringLiteral("GET ") + expected + QLatin1Char(' '))) {
            qCritical() << "初始 query 档位不符合预期";
            return false;
        }
    }
    if (observation.policy == QCurl::QCNetworkDiagnosticQueryPolicy::Redacted
        && (trace.contains(QStringLiteral("query-0"))
            || trace.contains(QStringLiteral("query-1")))) {
        qCritical() << "Redacted query 泄漏";
        return false;
    }
    return !observation.idsEnabled || correlate(observation, lines);
}
