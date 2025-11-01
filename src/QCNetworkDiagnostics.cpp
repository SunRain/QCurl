/**
 * @file QCNetworkDiagnostics.cpp
 * @brief 网络诊断工具实现
 *
 */

#include "QCNetworkDiagnostics.h"
#include <QHostInfo>
#include <QTcpSocket>
#include <QSslSocket>
#include <QSslCertificate>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QTimer>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QProcess>
#include <QRegularExpression>

QT_BEGIN_NAMESPACE

namespace QCurl {

// ============================================================================
// DiagResult 方法
// ============================================================================

QString DiagResult::toString() const
{
    QString result;
    result += success ? "✅ " : "❌ ";
    result += summary + "\n";
    
    if (!details.isEmpty()) {
        result += "详细信息:\n";
        for (auto it = details.constBegin(); it != details.constEnd(); ++it) {
            result += QString("  %1: %2\n").arg(it.key(), it.value().toString());
        }
    }
    
    result += QString("耗时: %1ms\n").arg(durationMs);
    
    if (!success && !errorString.isEmpty()) {
        result += QString("错误: %1\n").arg(errorString);
    }
    
    return result;
}

// ============================================================================
// DNS 解析
// ============================================================================

DiagResult QCNetworkDiagnostics::resolveDNS(const QString &hostname, int timeout)
{
    DiagResult result;
    result.timestamp = QDateTime::currentDateTime();
    
    QElapsedTimer timer;
    timer.start();
    
    // 使用 QHostInfo 进行解析
    QHostInfo info = QHostInfo::fromName(hostname);
    
    result.durationMs = timer.elapsed();
    
    if (info.error() != QHostInfo::NoError) {
        result.success = false;
        result.summary = QString("DNS 解析失败: %1").arg(hostname);
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
    
    result.success = true;
    result.summary = QString("DNS 解析成功: %1").arg(hostname);
    result.details["hostname"] = hostname;
    result.details["ipv4"] = ipv4List;
    result.details["ipv6"] = ipv6List;
    result.details["resolveDuration"] = result.durationMs;
    
    return result;
}

DiagResult QCNetworkDiagnostics::reverseDNS(const QString &ip, int timeout)
{
    DiagResult result;
    result.timestamp = QDateTime::currentDateTime();
    
    QElapsedTimer timer;
    timer.start();
    
    QHostInfo info = QHostInfo::fromName(ip);
    
    result.durationMs = timer.elapsed();
    result.details["ip"] = ip;
    
    if (info.error() != QHostInfo::NoError || info.hostName().isEmpty()) {
        result.success = false;
        result.summary = QString("反向 DNS 解析失败: %1").arg(ip);
        result.errorString = info.errorString();
        return result;
    }
    
    result.success = true;
    result.summary = QString("反向 DNS 解析成功: %1 -> %2").arg(ip, info.hostName());
    result.details["hostname"] = info.hostName();
    result.details["resolveDuration"] = result.durationMs;
    
    return result;
}

// ============================================================================
// 连接测试
// ============================================================================

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
    QObject::connect(&socket, QOverload<QAbstractSocket::SocketError>::of(&QTcpSocket::errorOccurred),
                     &loop, &QEventLoop::quit);
    
    // 超时定时器
    QTimer timeoutTimer;
    timeoutTimer.setSingleShot(true);
    QObject::connect(&timeoutTimer, &QTimer::timeout, &loop, &QEventLoop::quit);
    
    socket.connectToHost(host, port);
    timeoutTimer.start(timeout);
    
    loop.exec();
    
    result.durationMs = timer.elapsed();
    result.details["host"] = host;
    result.details["port"] = port;
    
    if (socket.state() == QAbstractSocket::ConnectedState) {
        result.success = true;
        result.summary = QString("连接成功: %1:%2").arg(host).arg(port);
        result.details["connected"] = true;
        result.details["connectDuration"] = result.durationMs;
        result.details["resolvedIP"] = socket.peerAddress().toString();
        socket.close();
    } else {
        result.success = false;
        result.summary = QString("连接失败: %1:%2").arg(host).arg(port);
        result.details["connected"] = false;
        result.errorString = socket.errorString();
    }
    
    return result;
}

// ============================================================================
// SSL 检查
// ============================================================================

DiagResult QCNetworkDiagnostics::checkSSL(const QString &host, int port, int timeout)
{
    DiagResult result;
    result.timestamp = QDateTime::currentDateTime();
    
    QElapsedTimer timer;
    timer.start();
    
    QSslSocket socket;
    QEventLoop loop;
    
    // 连接信号
    QObject::connect(&socket, &QSslSocket::encrypted, &loop, &QEventLoop::quit);
    QObject::connect(&socket, QOverload<QAbstractSocket::SocketError>::of(&QSslSocket::errorOccurred),
                     &loop, &QEventLoop::quit);
    QObject::connect(&socket, QOverload<const QList<QSslError>&>::of(&QSslSocket::sslErrors),
                     &loop, &QEventLoop::quit);
    
    // 超时定时器
    QTimer timeoutTimer;
    timeoutTimer.setSingleShot(true);
    QObject::connect(&timeoutTimer, &QTimer::timeout, &loop, &QEventLoop::quit);
    
    socket.connectToHostEncrypted(host, port);
    timeoutTimer.start(timeout);
    
    loop.exec();
    
    result.durationMs = timer.elapsed();
    result.details["host"] = host;
    result.details["port"] = port;
    
    if (socket.isEncrypted()) {
        QSslCertificate cert = socket.peerCertificate();

        result.success = true;
        result.summary = QString("SSL 证书有效: %1").arg(host);
        result.details["issuer"] = cert.issuerDisplayName();
        result.details["subject"] = cert.subjectDisplayName();
        result.details["notBefore"] = cert.effectiveDate();
        result.details["notAfter"] = cert.expiryDate();

        int daysValid = QDateTime::currentDateTime().daysTo(cert.expiryDate());
        result.details["daysValid"] = daysValid;
        result.details["tlsVersion"] = socket.sessionProtocol() == QSsl::TlsV1_3 ? "TLSv1.3" : "TLSv1.2";
        result.details["verified"] = socket.sslHandshakeErrors().isEmpty();

        socket.close();
    } else {
        result.success = false;
        result.summary = QString("SSL 握手失败: %1").arg(host);
        result.errorString = socket.errorString();

        if (!socket.sslHandshakeErrors().isEmpty()) {
            QStringList errors;
            for (const QSslError &err : socket.sslHandshakeErrors()) {
                errors << err.errorString();
            }
            result.details["sslErrors"] = errors;
        }
    }
    
    return result;
}

// ============================================================================
// HTTP 探测
// ============================================================================

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
    
    result.durationMs = timer.elapsed();
    result.details["url"] = url.toString();
    
    if (reply->error() == QNetworkReply::NoError) {
        result.success = true;
        result.summary = QString("HTTP 探测成功: %1").arg(url.toString());
        result.details["statusCode"] = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        result.details["statusText"] = reply->attribute(QNetworkRequest::HttpReasonPhraseAttribute).toString();
        result.details["totalTime"] = result.durationMs;
        result.details["finalURL"] = reply->url().toString();
    } else {
        result.success = false;
        result.summary = QString("HTTP 探测失败: %1").arg(url.toString());
        result.errorString = reply->errorString();
    }
    
    reply->deleteLater();
    return result;
}

// ============================================================================
// 综合诊断
// ============================================================================

DiagResult QCNetworkDiagnostics::diagnose(const QUrl &url)
{
    DiagResult result;
    result.timestamp = QDateTime::currentDateTime();
    
    QElapsedTimer timer;
    timer.start();
    
    QString host = url.host();
    int port = url.port(url.scheme() == "https" ? 443 : 80);
    bool isHttps = url.scheme() == "https";
    
    // 1. DNS 解析
    DiagResult dnsResult = resolveDNS(host);
    result.details["dns"] = QVariant::fromValue(dnsResult.details);
    
    if (!dnsResult.success) {
        result.success = false;
        result.summary = "诊断失败: DNS 解析失败";
        result.durationMs = timer.elapsed();
        return result;
    }
    
    // 2. 连接测试
    DiagResult connResult = testConnection(host, port);
    result.details["connection"] = QVariant::fromValue(connResult.details);
    
    if (!connResult.success) {
        result.success = false;
        result.summary = "诊断失败: 连接测试失败";
        result.durationMs = timer.elapsed();
        return result;
    }
    
    // 3. SSL 检查（HTTPS）
    if (isHttps) {
        DiagResult sslResult = checkSSL(host, port);
        result.details["ssl"] = QVariant::fromValue(sslResult.details);
        
        if (!sslResult.success) {
            result.details["overallHealth"] = "warning";
            result.summary = "诊断完成（SSL 警告）";
        }
    }
    
    // 4. HTTP 探测
    DiagResult httpResult = probeHTTP(url);
    result.details["http"] = QVariant::fromValue(httpResult.details);
    
    result.durationMs = timer.elapsed();
    
    if (httpResult.success) {
        result.success = true;
        result.summary = QString("综合诊断完成: %1").arg(url.toString());
        result.details["overallHealth"] = "excellent";
    } else {
        result.success = false;
        result.summary = "诊断失败: HTTP 探测失败";
        result.details["overallHealth"] = "error";
    }
    
    return result;
}

// ============================================================================
// Ping 测试
// ============================================================================

DiagResult QCNetworkDiagnostics::ping(const QString &host, int count, int timeout)
{
    DiagResult result;
    result.timestamp = QDateTime::currentDateTime();

    QElapsedTimer timer;
    timer.start();

    // 1. 先解析 DNS
    QHostInfo hostInfo = QHostInfo::fromName(host);
    if (hostInfo.error() != QHostInfo::NoError || hostInfo.addresses().isEmpty()) {
        result.success = false;
        result.summary = QString("Ping 失败: 无法解析主机 %1").arg(host);
        result.errorString = hostInfo.errorString();
        result.durationMs = timer.elapsed();
        return result;
    }

    QString resolvedIP = hostInfo.addresses().first().toString();
    result.details["host"] = host;
    result.details["resolvedIP"] = resolvedIP;

    // 2. 构建 ping 命令（跨平台）
    QString pingCmd;
    QStringList pingArgs;

#ifdef Q_OS_WIN
    pingCmd = "ping";
    pingArgs << "-n" << QString::number(count)  // 次数
             << "-w" << QString::number(timeout) // 超时（毫秒）
             << resolvedIP;
#else  // Linux/macOS
    pingCmd = "ping";
    pingArgs << "-c" << QString::number(count)  // 次数
             << "-W" << QString::number(timeout / 1000.0, 'f', 1) // 超时（秒）
             << resolvedIP;
#endif

    // 3. 执行 ping 命令
    QProcess process;
    process.start(pingCmd, pingArgs);

    if (!process.waitForFinished(timeout * count + 5000)) {
        result.success = false;
        result.summary = QString("Ping 超时: %1").arg(host);
        result.errorString = "进程执行超时";
        result.durationMs = timer.elapsed();
        process.kill();
        return result;
    }

    QString output = QString::fromLocal8Bit(process.readAllStandardOutput());
    result.durationMs = timer.elapsed();

    // 4. 解析输出
    QList<qint64> rttList;
    int packetsSent = count;
    int packetsReceived = 0;

#ifdef Q_OS_WIN
    // Windows ping 输出格式解析
    // 示例: "来自 8.8.8.8 的回复: 字节=32 时间=14ms TTL=117"
    QRegularExpression timeRegex("时间[=<]([0-9]+)ms", QRegularExpression::CaseInsensitiveOption);
    QRegularExpressionMatchIterator it = timeRegex.globalMatch(output);
    while (it.hasNext()) {
        QRegularExpressionMatch match = it.next();
        qint64 rtt = match.captured(1).toLongLong();
        rttList.append(rtt);
        packetsReceived++;
    }
#else
    // Linux/macOS ping 输出格式解析
    // 示例: "64 bytes from 8.8.8.8: icmp_seq=1 ttl=117 time=14.2 ms"
    QRegularExpression timeRegex("time=([0-9.]+)\\s*ms");
    QRegularExpressionMatchIterator it = timeRegex.globalMatch(output);
    while (it.hasNext()) {
        QRegularExpressionMatch match = it.next();
        qint64 rtt = static_cast<qint64>(match.captured(1).toDouble());
        rttList.append(rtt);
        packetsReceived++;
    }
#endif

    int packetsLost = packetsSent - packetsReceived;
    double lossRate = (packetsLost * 100.0) / packetsSent;

    result.details["packetsSent"] = packetsSent;
    result.details["packetsReceived"] = packetsReceived;
    result.details["packetsLost"] = packetsLost;
    result.details["lossRate"] = lossRate;

    if (!rttList.isEmpty()) {
        qint64 minRTT = *std::min_element(rttList.begin(), rttList.end());
        qint64 maxRTT = *std::max_element(rttList.begin(), rttList.end());
        qint64 avgRTT = std::accumulate(rttList.begin(), rttList.end(), 0LL) / rttList.size();

        result.details["minRTT"] = minRTT;
        result.details["maxRTT"] = maxRTT;
        result.details["avgRTT"] = avgRTT;
        result.details["rttList"] = QVariant::fromValue(rttList);

        result.success = true;
        result.summary = QString("Ping 成功: %1 (%2), 平均 %3ms, 丢包率 %4%")
                            .arg(host, resolvedIP)
                            .arg(avgRTT)
                            .arg(lossRate, 0, 'f', 1);
    } else {
        result.success = false;
        result.summary = QString("Ping 失败: %1, 100% 丢包").arg(host);
        result.errorString = "所有 ICMP 包均丢失";
    }

    return result;
}

// ============================================================================
// Traceroute 路由跟踪
// ============================================================================

DiagResult QCNetworkDiagnostics::traceroute(const QString &host, int maxHops, int timeout)
{
    DiagResult result;
    result.timestamp = QDateTime::currentDateTime();

    QElapsedTimer timer;
    timer.start();

    // 1. 先解析 DNS
    QHostInfo hostInfo = QHostInfo::fromName(host);
    if (hostInfo.error() != QHostInfo::NoError || hostInfo.addresses().isEmpty()) {
        result.success = false;
        result.summary = QString("Traceroute 失败: 无法解析主机 %1").arg(host);
        result.errorString = hostInfo.errorString();
        result.durationMs = timer.elapsed();
        return result;
    }

    QString resolvedIP = hostInfo.addresses().first().toString();
    result.details["host"] = host;
    result.details["resolvedIP"] = resolvedIP;

    // 2. 构建 traceroute 命令
    QString traceCmd;
    QStringList traceArgs;

#ifdef Q_OS_WIN
    traceCmd = "tracert";
    traceArgs << "-h" << QString::number(maxHops)  // 最大跳数
              << "-w" << QString::number(timeout)  // 超时（毫秒）
              << resolvedIP;
#else  // Linux/macOS
    traceCmd = "traceroute";
    traceArgs << "-m" << QString::number(maxHops)  // 最大跳数
              << "-w" << QString::number(timeout / 1000.0, 'f', 1)  // 超时（秒）
              << resolvedIP;
#endif

    // 3. 执行 traceroute 命令
    QProcess process;
    process.start(traceCmd, traceArgs);

    if (!process.waitForFinished(timeout * maxHops + 10000)) {
        result.success = false;
        result.summary = QString("Traceroute 超时: %1").arg(host);
        result.errorString = "进程执行超时";
        result.durationMs = timer.elapsed();
        process.kill();
        return result;
    }

    QString output = QString::fromLocal8Bit(process.readAllStandardOutput());
    result.durationMs = timer.elapsed();

    // 4. 解析输出
    QList<QVariantMap> hops;
    bool reachedDestination = false;

#ifdef Q_OS_WIN
    // Windows tracert 输出格式解析
    // 示例: "  1    14 ms    13 ms    14 ms  192.168.1.1"
    QRegularExpression hopRegex("^\\s*(\\d+)\\s+((?:<1 ms|[*]|\\d+ ms)\\s+){3}\\s*([\\d.]+|\\*)");
    QStringList lines = output.split('\n');

    for (const QString &line : lines) {
        QRegularExpressionMatch match = hopRegex.match(line);
        if (match.hasMatch()) {
            QVariantMap hop;
            hop["hopNumber"] = match.captured(1).toInt();
            QString ipStr = match.captured(3);

            if (ipStr == "*") {
                hop["ip"] = "timeout";
                hop["timeout"] = true;
            } else {
                hop["ip"] = ipStr;
                hop["timeout"] = false;

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
    QRegularExpression hopRegex("^\\s*(\\d+)\\s+(?:([\\w.-]+)\\s+)?\\(([\\d.]+)\\)");
    QStringList lines = output.split('\n');

    for (const QString &line : lines) {
        QRegularExpressionMatch match = hopRegex.match(line);
        if (match.hasMatch()) {
            QVariantMap hop;
            hop["hopNumber"] = match.captured(1).toInt();
            hop["hostname"] = match.captured(2);
            hop["ip"] = match.captured(3);
            hop["timeout"] = false;

            // 解析 RTT
            QRegularExpression rttRegex("([0-9.]+)\\s*ms");
            QRegularExpressionMatchIterator it = rttRegex.globalMatch(line);
            QList<qint64> rtts;
            while (it.hasNext()) {
                QRegularExpressionMatch rttMatch = it.next();
                rtts.append(static_cast<qint64>(rttMatch.captured(1).toDouble()));
            }

            if (rtts.size() >= 3) {
                hop["rtt1"] = rtts[0];
                hop["rtt2"] = rtts[1];
                hop["rtt3"] = rtts[2];
                hop["avgRTT"] = (rtts[0] + rtts[1] + rtts[2]) / 3;
            }

            // 检查是否到达目标
            if (match.captured(3) == resolvedIP) {
                reachedDestination = true;
            }

            hops.append(hop);
        }
    }
#endif

    result.details["hops"] = QVariant::fromValue(hops);
    result.details["totalHops"] = hops.size();
    result.details["reachedDestination"] = reachedDestination;

    if (!hops.isEmpty()) {
        result.success = true;
        result.summary = QString("Traceroute 完成: %1 (%2), 共 %3 跳")
                            .arg(host, resolvedIP)
                            .arg(hops.size());
    } else {
        result.success = false;
        result.summary = QString("Traceroute 失败: %1").arg(host);
        result.errorString = "无法解析路由信息";
    }

    return result;
}

} // namespace QCurl

QT_END_NAMESPACE
