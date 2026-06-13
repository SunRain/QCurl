/**
 * @file QCNetworkDiagnosticsProcess.cpp
 * @brief 依赖外部系统命令的网络诊断实现
 */

#include "QCNetworkDiagnostics.h"

#include <QElapsedTimer>
#include <QHostInfo>
#include <QProcess>
#include <QRegularExpression>

#include <limits>

namespace QCurl {

namespace {

constexpr int kProcessGraceTimeoutMs           = 5000;
constexpr int kTracerouteProcessGraceTimeoutMs = 10000;

int timeoutMs(const QCNetworkDiagnosticsOptions &options)
{
    const auto timeout = options.timeout().count();
    return static_cast<int>(std::min<qint64>(timeout, std::numeric_limits<int>::max()));
}

bool tryProcessWaitTimeoutMs(int timeoutMs, int multiplier, int graceMs, int *out)
{
    const qint64 waitMs = (static_cast<qint64>(timeoutMs) * multiplier) + graceMs;
    if (waitMs > std::numeric_limits<int>::max()) {
        return false;
    }
    *out = static_cast<int>(waitMs);
    return true;
}

} // namespace

// ==================
// Ping 测试
// ==================

DiagResult QCNetworkDiagnostics::ping(const QString &host,
                                      const QCNetworkDiagnosticsOptions &options)
{
    DiagResult result;
    result.setTimestamp(QDateTime::currentDateTime());

    QElapsedTimer timer;
    timer.start();

    const int count        = options.pingCount();
    const int timeout      = timeoutMs(options);
    int processWaitTimeout = 0;
    if (!tryProcessWaitTimeoutMs(timeout, count, kProcessGraceTimeoutMs, &processWaitTimeout)) {
        result.setSuccess(false);
        result.setSummary(QStringLiteral("Ping 配置无效: %1").arg(host));
        result.setErrorString(QStringLiteral("派生 waitForFinished 超时超过 int 表达范围"));
        result.setDurationMs(timer.elapsed());
        return result;
    }

    // 1. 先解析 DNS
    QHostInfo hostInfo = QHostInfo::fromName(host);
    if (hostInfo.error() != QHostInfo::NoError || hostInfo.addresses().isEmpty()) {
        result.setSuccess(false);
        result.setSummary(QStringLiteral("Ping 失败: 无法解析主机 %1").arg(host));
        result.setErrorString(hostInfo.errorString());
        result.setDurationMs(timer.elapsed());
        return result;
    }

    QString resolvedIP = hostInfo.addresses().first().toString();
    result.setDetail(QStringLiteral("host"), host);
    result.setDetail(QStringLiteral("resolvedIP"), resolvedIP);

    // 2. 构建 ping 命令（跨平台）
    QString pingCmd;
    QStringList pingArgs;

#ifdef Q_OS_WIN
    pingCmd = QStringLiteral("ping");
    pingArgs << QStringLiteral("-n") << QString::number(count)   // 次数
             << QStringLiteral("-w") << QString::number(timeout) // 超时（毫秒）
             << resolvedIP;
#else // Linux/macOS
    pingCmd = QStringLiteral("ping");
    pingArgs << QStringLiteral("-c") << QString::number(count)                    // 次数
             << QStringLiteral("-W") << QString::number(timeout / 1000.0, 'f', 1) // 超时（秒）
             << resolvedIP;
#endif

    // 3. 执行 ping 命令
    QProcess process;
    process.start(pingCmd, pingArgs);

    if (!process.waitForFinished(processWaitTimeout)) {
        result.setSuccess(false);
        result.setSummary(QStringLiteral("Ping 超时: %1").arg(host));
        result.setErrorString(QStringLiteral("进程执行超时"));
        result.setDurationMs(timer.elapsed());
        process.kill();
        return result;
    }

    QString output = QString::fromLocal8Bit(process.readAllStandardOutput());
    result.setDurationMs(timer.elapsed());

    // 4. 解析输出
    QList<qint64> rttList;
    int packetsSent     = count;
    int packetsReceived = 0;

#ifdef Q_OS_WIN
    // Windows ping 输出格式解析
    // 示例: "来自 8.8.8.8 的回复: 字节=32 时间=14ms TTL=117"
    QRegularExpression timeRegex(QStringLiteral("时间[=<]([0-9]+)ms"),
                                 QRegularExpression::CaseInsensitiveOption);
    QRegularExpressionMatchIterator it = timeRegex.globalMatch(output);
    while (it.hasNext()) {
        QRegularExpressionMatch match = it.next();
        qint64 rtt                    = match.captured(1).toLongLong();
        rttList.append(rtt);
        packetsReceived++;
    }
#else
    // Linux/macOS ping 输出格式解析
    // 示例: "64 bytes from 8.8.8.8: icmp_seq=1 ttl=117 time=14.2 ms"
    QRegularExpression timeRegex(QStringLiteral("time=([0-9.]+)\\s*ms"));
    QRegularExpressionMatchIterator it = timeRegex.globalMatch(output);
    while (it.hasNext()) {
        QRegularExpressionMatch match = it.next();
        qint64 rtt                    = static_cast<qint64>(match.captured(1).toDouble());
        rttList.append(rtt);
        packetsReceived++;
    }
#endif

    int packetsLost = packetsSent - packetsReceived;
    double lossRate = (packetsLost * 100.0) / packetsSent;

    result.setDetail(QStringLiteral("packetsSent"), packetsSent);
    result.setDetail(QStringLiteral("packetsReceived"), packetsReceived);
    result.setDetail(QStringLiteral("packetsLost"), packetsLost);
    result.setDetail(QStringLiteral("lossRate"), lossRate);

    if (!rttList.isEmpty()) {
        qint64 minRTT = *std::min_element(rttList.begin(), rttList.end());
        qint64 maxRTT = *std::max_element(rttList.begin(), rttList.end());
        qint64 avgRTT = std::accumulate(rttList.begin(), rttList.end(), 0LL) / rttList.size();

        result.setDetail(QStringLiteral("minRTT"), minRTT);
        result.setDetail(QStringLiteral("maxRTT"), maxRTT);
        result.setDetail(QStringLiteral("avgRTT"), avgRTT);
        result.setDetail(QStringLiteral("rttList"), QVariant::fromValue(rttList));

        result.setSuccess(true);
        result.setSummary(QStringLiteral("Ping 成功: %1 (%2), 平均 %3ms, 丢包率 %4%")
                              .arg(host, resolvedIP)
                              .arg(avgRTT)
                              .arg(lossRate, 0, 'f', 1));
    } else {
        result.setSuccess(false);
        result.setSummary(QStringLiteral("Ping 失败: %1, 100% 丢包").arg(host));
        result.setErrorString(QStringLiteral("所有 ICMP 包均丢失"));
    }

    return result;
}

// ==================
// Traceroute 路由跟踪
// ==================

DiagResult QCNetworkDiagnostics::traceroute(const QString &host,
                                            const QCNetworkDiagnosticsOptions &options)
{
    DiagResult result;
    result.setTimestamp(QDateTime::currentDateTime());

    QElapsedTimer timer;
    timer.start();

    const int maxHops      = options.tracerouteMaxHops();
    const int timeout      = timeoutMs(options);
    int processWaitTimeout = 0;
    if (!tryProcessWaitTimeoutMs(timeout,
                                 maxHops,
                                 kTracerouteProcessGraceTimeoutMs,
                                 &processWaitTimeout)) {
        result.setSuccess(false);
        result.setSummary(QStringLiteral("Traceroute 配置无效: %1").arg(host));
        result.setErrorString(QStringLiteral("派生 waitForFinished 超时超过 int 表达范围"));
        result.setDurationMs(timer.elapsed());
        return result;
    }

    // 1. 先解析 DNS
    QHostInfo hostInfo = QHostInfo::fromName(host);
    if (hostInfo.error() != QHostInfo::NoError || hostInfo.addresses().isEmpty()) {
        result.setSuccess(false);
        result.setSummary(QStringLiteral("Traceroute 失败: 无法解析主机 %1").arg(host));
        result.setErrorString(hostInfo.errorString());
        result.setDurationMs(timer.elapsed());
        return result;
    }

    QString resolvedIP = hostInfo.addresses().first().toString();
    result.setDetail(QStringLiteral("host"), host);
    result.setDetail(QStringLiteral("resolvedIP"), resolvedIP);

    // 2. 构建 traceroute 命令
    QString traceCmd;
    QStringList traceArgs;

#ifdef Q_OS_WIN
    traceCmd = QStringLiteral("tracert");
    traceArgs << QStringLiteral("-h") << QString::number(maxHops) // 最大跳数
              << QStringLiteral("-w") << QString::number(timeout) // 超时（毫秒）
              << resolvedIP;
#else // Linux/macOS
    traceCmd = QStringLiteral("traceroute");
    traceArgs << QStringLiteral("-m") << QString::number(maxHops)                  // 最大跳数
              << QStringLiteral("-w") << QString::number(timeout / 1000.0, 'f', 1) // 超时（秒）
              << resolvedIP;
#endif

    // 3. 执行 traceroute 命令
    QProcess process;
    process.start(traceCmd, traceArgs);

    if (!process.waitForFinished(processWaitTimeout)) {
        result.setSuccess(false);
        result.setSummary(QStringLiteral("Traceroute 超时: %1").arg(host));
        result.setErrorString(QStringLiteral("进程执行超时"));
        result.setDurationMs(timer.elapsed());
        process.kill();
        return result;
    }

    QString output = QString::fromLocal8Bit(process.readAllStandardOutput());
    result.setDurationMs(timer.elapsed());

    // 4. 解析输出
    QList<QVariantMap> hops;
    bool reachedDestination = false;

#ifdef Q_OS_WIN
    // Windows tracert 输出格式解析
    // 示例: "  1    14 ms    13 ms    14 ms  192.168.1.1"
    QRegularExpression hopRegex(
        QStringLiteral("^\\s*(\\d+)\\s+((?:<1 ms|[*]|\\d+ ms)\\s+){3}\\s*([\\d.]+|\\*)"));
    QStringList lines = output.split(QLatin1Char('\n'));

    for (const QString &line : lines) {
        QRegularExpressionMatch match = hopRegex.match(line);
        if (match.hasMatch()) {
            QVariantMap hop;
            hop[QStringLiteral("hopNumber")] = match.captured(1).toInt();
            QString ipStr                    = match.captured(3);

            if (ipStr == QStringLiteral("*")) {
                hop[QStringLiteral("ip")]      = QStringLiteral("timeout");
                hop[QStringLiteral("timeout")] = true;
            } else {
                hop[QStringLiteral("ip")]      = ipStr;
                hop[QStringLiteral("timeout")] = false;

                // 检查是否到达目标
                if (ipStr == resolvedIP) {
                    reachedDestination = true;
                }
            }

            hops.append(hop);
        }
    }
#else
    // Linux/macOS traceroute 输出格式解析
    // 示例: " 1  192.168.1.1 (192.168.1.1)  1.234 ms  1.123 ms  1.345 ms"
    QRegularExpression hopRegex(
        QStringLiteral("^\\s*(\\d+)\\s+(?:([\\w.-]+)\\s+)?\\(([\\d.]+)\\)"));
    QStringList lines = output.split(QLatin1Char('\n'));

    for (const QString &line : lines) {
        QRegularExpressionMatch match = hopRegex.match(line);
        if (match.hasMatch()) {
            QVariantMap hop;
            hop[QStringLiteral("hopNumber")] = match.captured(1).toInt();
            hop[QStringLiteral("hostname")]  = match.captured(2);
            hop[QStringLiteral("ip")]        = match.captured(3);
            hop[QStringLiteral("timeout")]   = false;

            // 解析 RTT
            QRegularExpression rttRegex(QStringLiteral("([0-9.]+)\\s*ms"));
            QRegularExpressionMatchIterator it = rttRegex.globalMatch(line);
            QList<qint64> rtts;
            while (it.hasNext()) {
                QRegularExpressionMatch rttMatch = it.next();
                rtts.append(static_cast<qint64>(rttMatch.captured(1).toDouble()));
            }

            if (rtts.size() >= 3) {
                hop[QStringLiteral("rtt1")]   = rtts[0];
                hop[QStringLiteral("rtt2")]   = rtts[1];
                hop[QStringLiteral("rtt3")]   = rtts[2];
                hop[QStringLiteral("avgRTT")] = (rtts[0] + rtts[1] + rtts[2]) / 3;
            }

            // 检查是否到达目标
            if (match.captured(3) == resolvedIP) {
                reachedDestination = true;
            }

            hops.append(hop);
        }
    }
#endif

    result.setDetail(QStringLiteral("hops"), QVariant::fromValue(hops));
    result.setDetail(QStringLiteral("totalHops"), hops.size());
    result.setDetail(QStringLiteral("reachedDestination"), reachedDestination);

    if (!hops.isEmpty()) {
        result.setSuccess(true);
        result.setSummary(QStringLiteral("Traceroute 完成: %1 (%2), 共 %3 跳")
                              .arg(host, resolvedIP)
                              .arg(hops.size()));
    } else {
        result.setSuccess(false);
        result.setSummary(QStringLiteral("Traceroute 失败: %1").arg(host));
        result.setErrorString(QStringLiteral("无法解析路由信息"));
    }

    return result;
}

} // namespace QCurl
