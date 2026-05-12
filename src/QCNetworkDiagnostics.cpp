/**
 * @file QCNetworkDiagnostics.cpp
 * @brief 网络诊断工具实现
 *
 */

#include "QCNetworkDiagnostics.h"

#include <QElapsedTimer>
#include <QEventLoop>
#include <QHostInfo>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QRegularExpression>
#include <QSslCertificate>
#include <QSslSocket>
#include <QTcpSocket>
#include <QTimer>

namespace QCurl {

namespace {

QVariantMap diagResultToVariantMap(const DiagResult &result)
{
    QVariantMap map = result.details;
    map.insert(QStringLiteral("success"), result.success);
    map.insert(QStringLiteral("summary"), result.summary);
    map.insert(QStringLiteral("durationMs"), result.durationMs);
    if (!result.errorString.isEmpty()) {
        map.insert(QStringLiteral("errorString"), result.errorString);
    }
    return map;
}

} // namespace

// ==================
// DiagResult 方法
// ==================

QString DiagResult::toString() const
{
    QString result;
    result += success ? QStringLiteral("✅ ") : QStringLiteral("❌ ");
    result += summary + QStringLiteral("\n");

    if (!details.isEmpty()) {
        result += QStringLiteral("详细信息:\n");
        for (auto it = details.constBegin(); it != details.constEnd(); ++it) {
            result += QStringLiteral("  %1: %2\n").arg(it.key(), it.value().toString());
        }
    }

    result += QStringLiteral("耗时: %1ms\n").arg(durationMs);

    if (!success && !errorString.isEmpty()) {
        result += QStringLiteral("错误: %1\n").arg(errorString);
    }

    return result;
}

// ==================
// DNS 解析
// ==================

DiagResult QCNetworkDiagnostics::resolveDNS(const QString &hostname, int timeout)
{
    DiagResult result;
    result.timestamp = QDateTime::currentDateTime();

    QElapsedTimer timer;
    timer.start();

    QHostInfo info;
    bool lookupCompleted = false;

    if (timeout > 0) {
        QEventLoop loop;
        QTimer timeoutTimer;
        timeoutTimer.setSingleShot(true);
        QObject::connect(&timeoutTimer, &QTimer::timeout, &loop, &QEventLoop::quit);

        QObject context;
        const int lookupId = QHostInfo::lookupHost(hostname,
                                                   &context,
                                                   [&](const QHostInfo &resolvedInfo) {
                                                       info            = resolvedInfo;
                                                       lookupCompleted = true;
                                                       loop.quit();
                                                   });

        timeoutTimer.start(timeout);
        loop.exec();

        if (!lookupCompleted) {
            QHostInfo::abortHostLookup(lookupId);
            result.durationMs           = timer.elapsed();
            result.success              = false;
            result.summary              = QStringLiteral("DNS 解析超时: %1").arg(hostname);
            result.errorString          = QStringLiteral("Timeout");
            result.details[QStringLiteral("hostname")]  = hostname;
            result.details[QStringLiteral("timeoutMs")] = timeout;
            return result;
        }
    } else {
        // 兼容：timeout <= 0 时沿用阻塞式解析
        info            = QHostInfo::fromName(hostname);
        lookupCompleted = true;
    }

    result.durationMs = timer.elapsed();

    if (info.error() != QHostInfo::NoError) {
        result.success     = false;
        result.summary     = QStringLiteral("DNS 解析失败: %1").arg(hostname);
        result.errorString = info.errorString();
        return result;
    }

    QStringList ipv4List, ipv6List;
    for (const QHostAddress &addr : info.addresses()) {
        if (addr.protocol() == QAbstractSocket::IPv4Protocol) {
            ipv4List << addr.toString();
        } else if (addr.protocol() == QAbstractSocket::IPv6Protocol) {
            ipv6List << addr.toString();
        }
    }

    result.success                    = true;
    result.summary                    = QStringLiteral("DNS 解析成功: %1").arg(hostname);
    result.details[QStringLiteral("hostname")]        = hostname;
    result.details[QStringLiteral("ipv4")]            = ipv4List;
    result.details[QStringLiteral("ipv6")]            = ipv6List;
    result.details[QStringLiteral("resolveDuration")] = result.durationMs;

    return result;
}

DiagResult QCNetworkDiagnostics::reverseDNS(const QString &ip, int timeout)
{
    DiagResult result;
    result.timestamp = QDateTime::currentDateTime();

    QElapsedTimer timer;
    timer.start();

    QHostInfo info;
    bool lookupCompleted = false;

    if (timeout > 0) {
        QEventLoop loop;
        QTimer timeoutTimer;
        timeoutTimer.setSingleShot(true);
        QObject::connect(&timeoutTimer, &QTimer::timeout, &loop, &QEventLoop::quit);

        QObject context;
        const int lookupId = QHostInfo::lookupHost(ip, &context, [&](const QHostInfo &resolvedInfo) {
            info            = resolvedInfo;
            lookupCompleted = true;
            loop.quit();
        });

        timeoutTimer.start(timeout);
        loop.exec();

        if (!lookupCompleted) {
            QHostInfo::abortHostLookup(lookupId);
            result.durationMs           = timer.elapsed();
            result.success              = false;
            result.summary              = QStringLiteral("反向 DNS 解析超时: %1").arg(ip);
            result.errorString          = QStringLiteral("Timeout");
            result.details[QStringLiteral("ip")]        = ip;
            result.details[QStringLiteral("timeoutMs")] = timeout;
            return result;
        }
    } else {
        // 兼容：timeout <= 0 时沿用阻塞式解析
        info            = QHostInfo::fromName(ip);
        lookupCompleted = true;
    }

    result.durationMs    = timer.elapsed();
    result.details[QStringLiteral("ip")] = ip;

    if (info.error() != QHostInfo::NoError || info.hostName().isEmpty()) {
        result.success     = false;
        result.summary     = QStringLiteral("反向 DNS 解析失败: %1").arg(ip);
        result.errorString = info.errorString();
        return result;
    }

    result.success = true;
    result.summary = QStringLiteral("反向 DNS 解析成功: %1 -> %2").arg(ip, info.hostName());
    result.details[QStringLiteral("hostname")]        = info.hostName();
    result.details[QStringLiteral("resolveDuration")] = result.durationMs;

    return result;
}

// ==================
// 连接测试
// ==================

DiagResult QCNetworkDiagnostics::testConnection(const QString &host, int port, int timeout)
{
    DiagResult result;
    result.timestamp = QDateTime::currentDateTime();

    QElapsedTimer timer;
    timer.start();

    QTcpSocket socket;
    QEventLoop loop;

    // 连接信号
    QObject::connect(&socket, &QTcpSocket::connected, &loop, &QEventLoop::quit);
    QObject::connect(&socket,
                     QOverload<QAbstractSocket::SocketError>::of(&QTcpSocket::errorOccurred),
                     &loop,
                     &QEventLoop::quit);

    // 超时定时器
    QTimer timeoutTimer;
    timeoutTimer.setSingleShot(true);
    QObject::connect(&timeoutTimer, &QTimer::timeout, &loop, &QEventLoop::quit);

    socket.connectToHost(host, port);
    timeoutTimer.start(timeout);

    loop.exec();

    result.durationMs      = timer.elapsed();
    result.details[QStringLiteral("host")] = host;
    result.details[QStringLiteral("port")] = port;

    if (socket.state() == QAbstractSocket::ConnectedState) {
        result.success                    = true;
        result.summary                    = QStringLiteral("连接成功: %1:%2").arg(host).arg(port);
        result.details[QStringLiteral("connected")]       = true;
        result.details[QStringLiteral("connectDuration")] = result.durationMs;
        result.details[QStringLiteral("resolvedIP")]      = socket.peerAddress().toString();
        socket.close();
    } else {
        result.success              = false;
        result.summary              = QStringLiteral("连接失败: %1:%2").arg(host).arg(port);
        result.details[QStringLiteral("connected")] = false;
        result.errorString          = socket.errorString();
    }

    return result;
}

// ==================
// SSL 检查
// ==================

DiagResult QCNetworkDiagnostics::checkSSL(const QString &host, int port, int timeout)
{
    DiagResult result;
    result.timestamp = QDateTime::currentDateTime();

    QElapsedTimer timer;
    timer.start();

    QSslSocket socket;
    QEventLoop loop;
    QList<QSslError> observedSslErrors;

    // 连接信号
    QObject::connect(&socket, &QSslSocket::encrypted, &loop, &QEventLoop::quit);
    QObject::connect(&socket,
                     QOverload<QAbstractSocket::SocketError>::of(&QSslSocket::errorOccurred),
                     &loop,
                     &QEventLoop::quit);
    QObject::connect(&socket,
                     QOverload<const QList<QSslError> &>::of(&QSslSocket::sslErrors),
                     &loop,
                     [&](const QList<QSslError> &errors) { observedSslErrors = errors; });

    // 超时定时器
    QTimer timeoutTimer;
    timeoutTimer.setSingleShot(true);
    QObject::connect(&timeoutTimer, &QTimer::timeout, &loop, &QEventLoop::quit);

    socket.connectToHostEncrypted(host, port);
    timeoutTimer.start(timeout);

    loop.exec();

    const bool timedOut = !timeoutTimer.isActive();
    timeoutTimer.stop();

    result.durationMs      = timer.elapsed();
    result.details[QStringLiteral("host")] = host;
    result.details[QStringLiteral("port")] = port;

    if (socket.isEncrypted()) {
        QSslCertificate cert = socket.peerCertificate();

        result.success              = true;
        result.summary              = QStringLiteral("SSL 证书有效: %1").arg(host);
        result.details[QStringLiteral("issuer")]    = cert.issuerDisplayName();
        result.details[QStringLiteral("subject")]   = cert.subjectDisplayName();
        result.details[QStringLiteral("notBefore")] = cert.effectiveDate();
        result.details[QStringLiteral("notAfter")]  = cert.expiryDate();

        int daysValid                = QDateTime::currentDateTime().daysTo(cert.expiryDate());
        result.details[QStringLiteral("daysValid")]  = daysValid;
        result.details[QStringLiteral("tlsVersion")]
            = socket.sessionProtocol() == QSsl::TlsV1_3 ? QStringLiteral("TLSv1.3")
                                                        : QStringLiteral("TLSv1.2");
        result.details[QStringLiteral("verified")]   = socket.sslHandshakeErrors().isEmpty();

        socket.close();
    } else if (timedOut) {
        socket.abort();
        result.success              = false;
        result.summary              = QStringLiteral("SSL 握手超时: %1").arg(host);
        result.errorString          = QStringLiteral("Timeout");
        result.details[QStringLiteral("timedOut")]  = true;
        result.details[QStringLiteral("timeoutMs")] = timeout;
    } else {
        result.success     = false;
        result.summary     = QStringLiteral("SSL 握手失败: %1").arg(host);
        result.errorString = socket.errorString();

        const QList<QSslError> allErrors = !observedSslErrors.isEmpty()
                                               ? observedSslErrors
                                               : socket.sslHandshakeErrors();
        if (!allErrors.isEmpty()) {
            QStringList errors;
            for (const QSslError &err : allErrors) {
                errors << err.errorString();
            }
            result.details[QStringLiteral("sslErrors")] = errors;
        }
    }

    return result;
}

// ==================
// HTTP 探测
// ==================

DiagResult QCNetworkDiagnostics::probeHTTP(const QUrl &url, int timeout)
{
    DiagResult result;
    result.timestamp = QDateTime::currentDateTime();

    QElapsedTimer timer;
    timer.start();

    QNetworkAccessManager manager;
    QNetworkRequest request(url);
    request.setTransferTimeout(timeout);

    QNetworkReply *reply = manager.get(request);
    QEventLoop loop;

    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);

    loop.exec();

    result.durationMs        = timer.elapsed();
    result.details[QStringLiteral("url")]    = url.toString();
    result.details[QStringLiteral("totalTime")] = result.durationMs;
    result.details[QStringLiteral("finalURL")]  = reply->url().toString();
    result.details[QStringLiteral("networkError")] = static_cast<int>(reply->error());

    const QVariant statusCodeAttr = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute);
    if (statusCodeAttr.isValid()) {
        result.details[QStringLiteral("statusCode")] = statusCodeAttr.toInt();
    }
    const QVariant statusTextAttr = reply->attribute(QNetworkRequest::HttpReasonPhraseAttribute);
    if (statusTextAttr.isValid()) {
        result.details[QStringLiteral("statusText")] = statusTextAttr.toString();
    }

    if (reply->error() == QNetworkReply::NoError) {
        result.success               = true;
        result.summary               = QStringLiteral("HTTP 探测成功: %1").arg(url.toString());
    } else {
        result.success     = false;
        result.summary     = QStringLiteral("HTTP 探测失败: %1").arg(url.toString());
        result.errorString = reply->errorString();
        result.details[QStringLiteral("errorString")] = result.errorString;
    }

    reply->deleteLater();
    return result;
}

// ==================
// 综合诊断
// ==================

DiagResult QCNetworkDiagnostics::diagnose(const QUrl &url)
{
    DiagResult result;
    result.timestamp = QDateTime::currentDateTime();

    QElapsedTimer timer;
    timer.start();

    QString host = url.host();
    int port     = url.port(url.scheme() == QStringLiteral("https") ? 443 : 80);
    bool isHttps = url.scheme() == QStringLiteral("https");

    bool hasSslWarning = false;

    // 1. DNS 解析
    DiagResult dnsResult  = resolveDNS(host);
    result.details[QStringLiteral("dns")] = QVariant::fromValue(diagResultToVariantMap(dnsResult));

    if (!dnsResult.success) {
        result.success               = false;
        result.summary               = QStringLiteral("诊断失败: DNS 解析失败");
        result.errorString           = dnsResult.errorString;
        result.durationMs            = timer.elapsed();
        result.details[QStringLiteral("failedStep")] = QStringLiteral("dns");
        result.details[QStringLiteral("overallHealth")] = QStringLiteral("error");
        return result;
    }

    // 2. 连接测试
    DiagResult connResult        = testConnection(host, port);
    result.details[QStringLiteral("connection")] = QVariant::fromValue(diagResultToVariantMap(connResult));

    if (!connResult.success) {
        result.success               = false;
        result.summary               = QStringLiteral("诊断失败: 连接测试失败");
        result.errorString           = connResult.errorString;
        result.durationMs            = timer.elapsed();
        result.details[QStringLiteral("failedStep")] = QStringLiteral("connection");
        result.details[QStringLiteral("overallHealth")] = QStringLiteral("error");
        return result;
    }

    // 3. SSL 检查（HTTPS）
    if (isHttps) {
        DiagResult sslResult  = checkSSL(host, port);
        result.details[QStringLiteral("ssl")] = QVariant::fromValue(diagResultToVariantMap(sslResult));

        if (!sslResult.success) {
            hasSslWarning = true;
        }
    }

    // 4. HTTP 探测
    DiagResult httpResult  = probeHTTP(url);
    result.details[QStringLiteral("http")] = QVariant::fromValue(diagResultToVariantMap(httpResult));

    result.durationMs = timer.elapsed();

    if (httpResult.success) {
        result.success                  = true;
        result.summary                  = hasSslWarning
                                              ? QStringLiteral("综合诊断完成（SSL 警告）: %1")
                                                    .arg(url.toString())
                                              : QStringLiteral("综合诊断完成: %1")
                                                    .arg(url.toString());
        result.details[QStringLiteral("overallHealth")] = hasSslWarning
                                                              ? QStringLiteral("warning")
                                                              : QStringLiteral("excellent");
    } else {
        result.success                  = false;
        result.summary                  = QStringLiteral("诊断失败: HTTP 探测失败");
        result.errorString              = httpResult.errorString;
        result.details[QStringLiteral("failedStep")]    = QStringLiteral("http");
        result.details[QStringLiteral("overallHealth")] = QStringLiteral("error");
    }

    return result;
}

// ==================
// Ping 测试
// ==================

DiagResult QCNetworkDiagnostics::ping(const QString &host, int count, int timeout)
{
    DiagResult result;
    result.timestamp = QDateTime::currentDateTime();

    QElapsedTimer timer;
    timer.start();

    // 1. 先解析 DNS
    QHostInfo hostInfo = QHostInfo::fromName(host);
    if (hostInfo.error() != QHostInfo::NoError || hostInfo.addresses().isEmpty()) {
        result.success     = false;
        result.summary     = QStringLiteral("Ping 失败: 无法解析主机 %1").arg(host);
        result.errorString = hostInfo.errorString();
        result.durationMs  = timer.elapsed();
        return result;
    }

    QString resolvedIP           = hostInfo.addresses().first().toString();
    result.details[QStringLiteral("host")]       = host;
    result.details[QStringLiteral("resolvedIP")] = resolvedIP;

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

    if (!process.waitForFinished(timeout * count + 5000)) {
        result.success     = false;
        result.summary     = QStringLiteral("Ping 超时: %1").arg(host);
        result.errorString = QStringLiteral("进程执行超时");
        result.durationMs  = timer.elapsed();
        process.kill();
        return result;
    }

    QString output    = QString::fromLocal8Bit(process.readAllStandardOutput());
    result.durationMs = timer.elapsed();

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

    result.details[QStringLiteral("packetsSent")]     = packetsSent;
    result.details[QStringLiteral("packetsReceived")] = packetsReceived;
    result.details[QStringLiteral("packetsLost")]     = packetsLost;
    result.details[QStringLiteral("lossRate")]        = lossRate;

    if (!rttList.isEmpty()) {
        qint64 minRTT = *std::min_element(rttList.begin(), rttList.end());
        qint64 maxRTT = *std::max_element(rttList.begin(), rttList.end());
        qint64 avgRTT = std::accumulate(rttList.begin(), rttList.end(), 0LL) / rttList.size();

        result.details[QStringLiteral("minRTT")]  = minRTT;
        result.details[QStringLiteral("maxRTT")]  = maxRTT;
        result.details[QStringLiteral("avgRTT")]  = avgRTT;
        result.details[QStringLiteral("rttList")] = QVariant::fromValue(rttList);

        result.success = true;
        result.summary = QStringLiteral("Ping 成功: %1 (%2), 平均 %3ms, 丢包率 %4%")
                             .arg(host, resolvedIP)
                             .arg(avgRTT)
                             .arg(lossRate, 0, 'f', 1);
    } else {
        result.success     = false;
        result.summary     = QStringLiteral("Ping 失败: %1, 100% 丢包").arg(host);
        result.errorString = QStringLiteral("所有 ICMP 包均丢失");
    }

    return result;
}

// ==================
// Traceroute 路由跟踪
// ==================

DiagResult QCNetworkDiagnostics::traceroute(const QString &host, int maxHops, int timeout)
{
    DiagResult result;
    result.timestamp = QDateTime::currentDateTime();

    QElapsedTimer timer;
    timer.start();

    // 1. 先解析 DNS
    QHostInfo hostInfo = QHostInfo::fromName(host);
    if (hostInfo.error() != QHostInfo::NoError || hostInfo.addresses().isEmpty()) {
        result.success     = false;
        result.summary     = QStringLiteral("Traceroute 失败: 无法解析主机 %1").arg(host);
        result.errorString = hostInfo.errorString();
        result.durationMs  = timer.elapsed();
        return result;
    }

    QString resolvedIP           = hostInfo.addresses().first().toString();
    result.details[QStringLiteral("host")]       = host;
    result.details[QStringLiteral("resolvedIP")] = resolvedIP;

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

    if (!process.waitForFinished(timeout * maxHops + 10000)) {
        result.success     = false;
        result.summary     = QStringLiteral("Traceroute 超时: %1").arg(host);
        result.errorString = QStringLiteral("进程执行超时");
        result.durationMs  = timer.elapsed();
        process.kill();
        return result;
    }

    QString output    = QString::fromLocal8Bit(process.readAllStandardOutput());
    result.durationMs = timer.elapsed();

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
            QString ipStr    = match.captured(3);

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

    result.details[QStringLiteral("hops")]               = QVariant::fromValue(hops);
    result.details[QStringLiteral("totalHops")]          = hops.size();
    result.details[QStringLiteral("reachedDestination")] = reachedDestination;

    if (!hops.isEmpty()) {
        result.success = true;
        result.summary = QStringLiteral("Traceroute 完成: %1 (%2), 共 %3 跳")
                             .arg(host, resolvedIP)
                             .arg(hops.size());
    } else {
        result.success     = false;
        result.summary     = QStringLiteral("Traceroute 失败: %1").arg(host);
        result.errorString = QStringLiteral("无法解析路由信息");
    }

    return result;
}

} // namespace QCurl
