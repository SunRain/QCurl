#ifndef QCURL_TRACE_CONTRACT_H
#define QCURL_TRACE_CONTRACT_H

#include <QCNetworkDiagnosticQueryPolicy.h>
#include <QCNetworkLogger.h>
#include <QStringList>
#include <QUrl>

struct TraceObservation
{
    QList<QCurl::NetworkLogEntry> entries;
    QStringList targets;
    QStringList requestMarkers;
    QStringList responseMarkers;
    QCurl::QCNetworkDiagnosticQueryPolicy policy;
    bool idsEnabled = false;
};

/// 验证同一 manager 的真实并发、wire/raw 数据及每条 trace 的关联，不读取 private 数据。
[[nodiscard]] bool runTraceRequests(bool http2,
                                    const QUrl &base,
                                    bool idsEnabled,
                                    QCurl::QCNetworkDiagnosticQueryPolicy policy);
[[nodiscard]] bool verifyTrace(const TraceObservation &observation);
[[nodiscard]] bool verifyLoadedLibraries();

#endif // QCURL_TRACE_CONTRACT_H
