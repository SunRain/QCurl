/**
 * @file QCNetworkDiagnosticsProcess.cpp
 * @brief 异步外部进程网络诊断实现。
 */

#include "QCNetworkDiagnostics.h"
#include "private/QCNetworkDiagnosticKeys_p.h"
#include "private/QCNetworkDiagnosticsOperation_p.h"

#include <QAbstractEventDispatcher>
#include <QHostInfo>
#include <QProcess>
#include <QRegularExpression>
#include <QThread>

#include <algorithm>
#include <limits>
#include <numeric>
#include <utility>

namespace QCurl {

namespace {

using Internal::DiagnosticsOperation;

constexpr int kProcessGraceTimeoutMs           = 5000;
constexpr int kTracerouteProcessGraceTimeoutMs = 10000;

bool derivedTimeout(int timeoutMs, int multiplier, int graceMs, int *result)
{
    const qint64 value = (static_cast<qint64>(timeoutMs) * multiplier) + graceMs;
    if (value > std::numeric_limits<int>::max()) {
        return false;
    }
    *result = static_cast<int>(value);
    return true;
}

QFuture<DiagResult> configurationFailure(const QString &operation, const QString &host)
{
    DiagResult result;
    result.setSuccess(false);
    result.setSummary(QStringLiteral("%1 配置无效: %2").arg(operation, host));
    result.setErrorString(QStringLiteral("派生进程 deadline 超过 int 表达范围"));
    return Internal::finishedDiagnosticsFuture(std::move(result));
}

QFuture<DiagResult> dispatchFailure(const QString &operation, const QString &host)
{
    DiagResult result;
    result.setSuccess(false);
    result.setSummary(QStringLiteral("%1 无法调度: %2").arg(operation, host));
    result.setErrorString(QStringLiteral("DispatchFailed"));
    return Internal::finishedDiagnosticsFuture(std::move(result));
}

void parsePingOutput(DiagResult *result,
                     const QString &host,
                     const QString &resolvedIp,
                     int count,
                     const QString &output)
{
    QList<qint64> roundTripTimes;
#ifdef Q_OS_WIN
    const QRegularExpression expression(QStringLiteral("时间[=<]([0-9]+)ms"),
                                        QRegularExpression::CaseInsensitiveOption);
#else
    const QRegularExpression expression(QStringLiteral("time=([0-9.]+)\\s*ms"));
#endif
    QRegularExpressionMatchIterator matches = expression.globalMatch(output);
    while (matches.hasNext()) {
        const QRegularExpressionMatch match = matches.next();
        roundTripTimes.append(static_cast<qint64>(match.captured(1).toDouble()));
    }

    const int received    = roundTripTimes.size();
    const int lost        = count - received;
    const double lossRate = (lost * 100.0) / count;
    result->setDetail(QStringLiteral("packetsSent"), count);
    result->setDetail(QStringLiteral("packetsReceived"), received);
    result->setDetail(QStringLiteral("packetsLost"), lost);
    result->setDetail(QStringLiteral("lossRate"), lossRate);

    if (roundTripTimes.isEmpty()) {
        result->setSuccess(false);
        result->setSummary(QStringLiteral("Ping 失败: %1, 100% 丢包").arg(host));
        result->setErrorString(QStringLiteral("所有 ICMP 包均丢失"));
        return;
    }

    const qint64 minimum = *std::min_element(roundTripTimes.cbegin(), roundTripTimes.cend());
    const qint64 maximum = *std::max_element(roundTripTimes.cbegin(), roundTripTimes.cend());
    const qint64 average = std::accumulate(roundTripTimes.cbegin(), roundTripTimes.cend(), 0LL)
                           / roundTripTimes.size();
    result->setDetail(QStringLiteral("minRTT"), minimum);
    result->setDetail(QStringLiteral("maxRTT"), maximum);
    result->setDetail(QStringLiteral("avgRTT"), average);
    result->setDetail(QStringLiteral("rttList"), QVariant::fromValue(roundTripTimes));
    result->setSuccess(true);
    result->setSummary(QStringLiteral("Ping 成功: %1 (%2), 平均 %3ms, 丢包率 %4%")
                           .arg(host, resolvedIp)
                           .arg(average)
                           .arg(lossRate, 0, 'f', 1));
}

QList<QVariantMap> parseTracerouteOutput(const QString &output,
                                         const QString &resolvedIp,
                                         bool *reachedDestination)
{
    QList<QVariantMap> hops;
#ifdef Q_OS_WIN
    const QRegularExpression hopExpression(
        QStringLiteral("^\\s*(\\d+)\\s+((?:<1 ms|[*]|\\d+ ms)\\s+){3}\\s*([\\d.]+|\\*)"));
#else
    const QRegularExpression hopExpression(
        QStringLiteral("^\\s*(\\d+)\\s+(?:([\\w.-]+)\\s+)?\\(([\\d.]+)\\)"));
#endif
    for (const QString &line : output.split(QLatin1Char('\n'))) {
        const QRegularExpressionMatch match = hopExpression.match(line);
        if (!match.hasMatch()) {
            continue;
        }

        QVariantMap hop;
        hop.insert(QStringLiteral("hopNumber"), match.captured(1).toInt());
#ifdef Q_OS_WIN
        const QString ip = match.captured(3);
        hop.insert(QStringLiteral("ip"), ip == QStringLiteral("*") ? QStringLiteral("timeout") : ip);
        hop.insert(QStringLiteral("timeout"), ip == QStringLiteral("*"));
#else
        const QString ip = match.captured(3);
        hop.insert(QStringLiteral("hostname"), match.captured(2));
        hop.insert(QStringLiteral("ip"), ip);
        hop.insert(QStringLiteral("timeout"), false);
        const QRegularExpression rttExpression(QStringLiteral("([0-9.]+)\\s*ms"));
        QRegularExpressionMatchIterator rttMatches = rttExpression.globalMatch(line);
        QList<qint64> roundTripTimes;
        while (rttMatches.hasNext()) {
            roundTripTimes.append(static_cast<qint64>(rttMatches.next().captured(1).toDouble()));
        }
        if (roundTripTimes.size() >= 3) {
            hop.insert(QStringLiteral("rtt1"), roundTripTimes.at(0));
            hop.insert(QStringLiteral("rtt2"), roundTripTimes.at(1));
            hop.insert(QStringLiteral("rtt3"), roundTripTimes.at(2));
            hop.insert(QStringLiteral("avgRTT"),
                       (roundTripTimes.at(0) + roundTripTimes.at(1) + roundTripTimes.at(2)) / 3);
        }
#endif
        *reachedDestination = *reachedDestination || ip == resolvedIp;
        hops.append(std::move(hop));
    }
    return hops;
}

class ProcessDiagnostics final : public QObject
{
    Q_OBJECT

public:
    ProcessDiagnostics(DiagnosticsOperation *state,
                       QString operation,
                       QString host,
                       QCNetworkDiagnosticsOptions options,
                       bool traceroute)
        : QObject(state)
        , m_state(state)
        , m_operation(std::move(operation))
        , m_host(std::move(host))
        , m_options(std::move(options))
        , m_traceroute(traceroute)
    {}

    void start()
    {
        m_lookupId = QHostInfo::lookupHost(m_host, this, [this](const QHostInfo &info) {
            m_lookupId = -1;
            if (m_state->isFinished()) {
                return;
            }
            if (info.error() != QHostInfo::NoError || info.addresses().isEmpty()) {
                m_state->fail(QStringLiteral("%1 失败: 无法解析主机 %2").arg(m_operation, m_host),
                              info.errorString());
                return;
            }
            m_resolvedIp = info.addresses().first().toString();
            startProcess();
        });
    }

    void stop()
    {
        if (m_lookupId >= 0) {
            QHostInfo::abortHostLookup(m_lookupId);
            m_lookupId = -1;
        }
        if (m_process && m_process->state() != QProcess::NotRunning) {
            m_process->kill();
        }
    }

private:
    Q_DISABLE_COPY_MOVE(ProcessDiagnostics)

    void startProcess()
    {
        m_process = new QProcess(this);
        QObject::connect(m_process,
                         &QProcess::errorOccurred,
                         this,
                         [this](QProcess::ProcessError error) {
                             if (m_state->isFinished() || error != QProcess::FailedToStart) {
                                 return;
                             }
                             m_state->fail(QStringLiteral("%1 失败: %2").arg(m_operation, m_host),
                                           m_process->errorString());
                         });
        QObject::connect(m_process,
                         qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
                         this,
                         [this](int exitCode, QProcess::ExitStatus exitStatus) {
                             if (m_state->isFinished()) {
                                 return;
                             }
                             finishProcess(exitCode, exitStatus);
                         });

        QString program;
        QStringList arguments;
        buildCommand(&program, &arguments);
        m_process->start(program, arguments);
    }

    void buildCommand(QString *program, QStringList *arguments) const
    {
        const int timeoutMs = Internal::diagnosticsTimeoutMs(m_options);
#ifdef Q_OS_WIN
        *program = m_traceroute ? QStringLiteral("tracert") : QStringLiteral("ping");
        if (m_traceroute) {
            *arguments << QStringLiteral("-h") << QString::number(m_options.tracerouteMaxHops());
        } else {
            *arguments << QStringLiteral("-n") << QString::number(m_options.pingCount());
        }
        *arguments << QStringLiteral("-w") << QString::number(timeoutMs) << m_resolvedIp;
#else
        *program = m_traceroute ? QStringLiteral("traceroute") : QStringLiteral("ping");
        if (m_traceroute) {
            *arguments << QStringLiteral("-m") << QString::number(m_options.tracerouteMaxHops());
        } else {
            *arguments << QStringLiteral("-c") << QString::number(m_options.pingCount());
        }
        *arguments << QStringLiteral("-w") << QString::number(timeoutMs / 1000.0, 'f', 1)
                   << m_resolvedIp;
#endif
    }

    void finishProcess(int exitCode, QProcess::ExitStatus exitStatus)
    {
        const QString output        = QString::fromLocal8Bit(m_process->readAllStandardOutput());
        const QString standardError = QString::fromLocal8Bit(m_process->readAllStandardError());
        DiagResult result;
        result.setDetail(QCurl::Internal::diagnostickeys::kHost, m_host);
        result.setDetail(QCurl::Internal::diagnostickeys::kResolvedIp, m_resolvedIp);
        result.setDetail(QStringLiteral("processExitCode"), exitCode);
        if (!standardError.isEmpty()) {
            result.setDetail(QStringLiteral("standardError"), standardError);
        }
        if (exitStatus != QProcess::NormalExit) {
            result.setSuccess(false);
            result.setSummary(QStringLiteral("%1 失败: %2").arg(m_operation, m_host));
            result.setErrorString(QStringLiteral("诊断进程异常退出"));
        } else if (m_traceroute) {
            finishTraceroute(&result, output);
        } else {
            parsePingOutput(&result, m_host, m_resolvedIp, m_options.pingCount(), output);
        }
        m_state->finish(std::move(result));
    }

    void finishTraceroute(DiagResult *result, const QString &output) const
    {
        bool reachedDestination       = false;
        const QList<QVariantMap> hops = parseTracerouteOutput(output,
                                                              m_resolvedIp,
                                                              &reachedDestination);
        result->setDetail(QStringLiteral("hops"), QVariant::fromValue(hops));
        result->setDetail(QStringLiteral("totalHops"), hops.size());
        result->setDetail(QStringLiteral("reachedDestination"), reachedDestination);
        result->setSuccess(!hops.isEmpty());
        result->setSummary(!hops.isEmpty() ? QStringLiteral("Traceroute 完成: %1 (%2), 共 %3 跳")
                                                 .arg(m_host, m_resolvedIp)
                                                 .arg(hops.size())
                                           : QStringLiteral("Traceroute 失败: %1").arg(m_host));
        if (hops.isEmpty()) {
            result->setErrorString(QStringLiteral("无法解析路由信息"));
        }
    }

    DiagnosticsOperation *m_state;
    QString m_operation;
    QString m_host;
    QCNetworkDiagnosticsOptions m_options;
    bool m_traceroute = false;
    int m_lookupId    = -1;
    QString m_resolvedIp;
    QProcess *m_process = nullptr;
};

QFuture<DiagResult> startProcessDiagnostics(const QString &operation,
                                            const QString &host,
                                            const QCNetworkDiagnosticsOptions &options,
                                            QCNetworkCancelToken *cancelToken,
                                            bool traceroute)
{
    if (!QAbstractEventDispatcher::instance(QThread::currentThread())) {
        return dispatchFailure(operation, host);
    }

    const int multiplier = traceroute ? options.tracerouteMaxHops() : options.pingCount();
    const int grace      = traceroute ? kTracerouteProcessGraceTimeoutMs : kProcessGraceTimeoutMs;
    int deadline         = 0;
    if (!derivedTimeout(Internal::diagnosticsTimeoutMs(options), multiplier, grace, &deadline)) {
        return configurationFailure(operation, host);
    }

    auto *state = new DiagnosticsOperation(operation, host, deadline, cancelToken);
    const QFuture<DiagResult> future = state->future();
    auto *sequence = new ProcessDiagnostics(state, operation, host, options, traceroute);
    state->setCancelHandler([sequence]() { sequence->stop(); });
    sequence->start();
    return future;
}

} // namespace

QFuture<DiagResult> QCNetworkDiagnostics::ping(const QString &host,
                                               const QCNetworkDiagnosticsOptions &options,
                                               QCNetworkCancelToken *cancelToken)
{
    return startProcessDiagnostics(QStringLiteral("Ping"), host, options, cancelToken, false);
}

QFuture<DiagResult> QCNetworkDiagnostics::traceroute(const QString &host,
                                                     const QCNetworkDiagnosticsOptions &options,
                                                     QCNetworkCancelToken *cancelToken)
{
    return startProcessDiagnostics(QStringLiteral("Traceroute"), host, options, cancelToken, true);
}

} // namespace QCurl

#include "QCNetworkDiagnosticsProcess.moc"
