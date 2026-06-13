/**
 * @file QCNetworkDiagnostics.cpp
 * @brief 网络诊断工具实现
 *
 */

#include "QCNetworkDiagnostics.h"

#include <QElapsedTimer>
#include <QEventLoop>
#include <QHostInfo>
#include <QTimer>

#include <limits>

namespace QCurl {

namespace {

int timeoutMs(const QCNetworkDiagnosticsOptions &options)
{
    const auto timeout = options.timeout().count();
    return static_cast<int>(std::min<qint64>(timeout, std::numeric_limits<int>::max()));
}

} // namespace

// ==================
// DNS 解析
// ==================

DiagResult QCNetworkDiagnostics::resolveDNS(const QString &hostname,
                                            const QCNetworkDiagnosticsOptions &options)
{
    DiagResult result;
    result.setTimestamp(QDateTime::currentDateTime());

    QElapsedTimer timer;
    timer.start();

    QHostInfo info;
    bool lookupCompleted = false;

    const int timeout = timeoutMs(options);
    {
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
            result.setDurationMs(timer.elapsed());
            result.setSuccess(false);
            result.setSummary(QStringLiteral("DNS 解析超时: %1").arg(hostname));
            result.setErrorString(QStringLiteral("Timeout"));
            result.setDetail(QStringLiteral("hostname"), hostname);
            result.setDetail(QStringLiteral("timeoutMs"), timeout);
            return result;
        }
    }

    result.setDurationMs(timer.elapsed());

    if (info.error() != QHostInfo::NoError) {
        result.setSuccess(false);
        result.setSummary(QStringLiteral("DNS 解析失败: %1").arg(hostname));
        result.setErrorString(info.errorString());
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

    result.setSuccess(true);
    result.setSummary(QStringLiteral("DNS 解析成功: %1").arg(hostname));
    result.setDetail(QStringLiteral("hostname"), hostname);
    result.setDetail(QStringLiteral("ipv4"), ipv4List);
    result.setDetail(QStringLiteral("ipv6"), ipv6List);
    result.setDetail(QStringLiteral("resolveDuration"), result.durationMs());

    return result;
}

DiagResult QCNetworkDiagnostics::reverseDNS(const QString &ip,
                                            const QCNetworkDiagnosticsOptions &options)
{
    DiagResult result;
    result.setTimestamp(QDateTime::currentDateTime());

    QElapsedTimer timer;
    timer.start();

    QHostInfo info;
    bool lookupCompleted = false;

    const int timeout = timeoutMs(options);
    {
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
            result.setDurationMs(timer.elapsed());
            result.setSuccess(false);
            result.setSummary(QStringLiteral("反向 DNS 解析超时: %1").arg(ip));
            result.setErrorString(QStringLiteral("Timeout"));
            result.setDetail(QStringLiteral("ip"), ip);
            result.setDetail(QStringLiteral("timeoutMs"), timeout);
            return result;
        }
    }

    result.setDurationMs(timer.elapsed());
    result.setDetail(QStringLiteral("ip"), ip);

    if (info.error() != QHostInfo::NoError || info.hostName().isEmpty()) {
        result.setSuccess(false);
        result.setSummary(QStringLiteral("反向 DNS 解析失败: %1").arg(ip));
        result.setErrorString(info.errorString());
        return result;
    }

    result.setSuccess(true);
    result.setSummary(QStringLiteral("反向 DNS 解析成功: %1 -> %2").arg(ip, info.hostName()));
    result.setDetail(QStringLiteral("hostname"), info.hostName());
    result.setDetail(QStringLiteral("resolveDuration"), result.durationMs());

    return result;
}

} // namespace QCurl
