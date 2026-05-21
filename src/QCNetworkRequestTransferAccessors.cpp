#include "QCNetworkRequest.h"

#include "private/QCNetworkRequestPrivate_p.h"

#include <QDebug>

#include <chrono>
#include <optional>

namespace QCurl {

namespace {

QStringList cleanedAdvancedStringList(const QStringList &entries)
{
    QStringList cleaned;
    cleaned.reserve(entries.size());
    for (const QString &entry : entries) {
        const QString trimmed = entry.trimmed();
        if (!trimmed.isEmpty()) {
            cleaned.append(trimmed);
        }
    }
    return cleaned;
}

} // namespace

QCNetworkRequest &QCNetworkRequest::setAutoDecompressionEnabled(bool enabled)
{
    d.data()->transferConfig.setAutoDecompressionEnabled(enabled);
    return *this;
}

bool QCNetworkRequest::autoDecompressionEnabled() const
{
    return d.constData()->transferConfig.autoDecompressionEnabled();
}

QCNetworkRequest &QCNetworkRequest::setAcceptedEncodings(const QStringList &encodings)
{
    d.data()->transferConfig.setAcceptedEncodings(encodings);
    return *this;
}

QStringList QCNetworkRequest::acceptedEncodings() const
{
    return d.constData()->transferConfig.acceptedEncodings();
}

QCNetworkRequest &QCNetworkRequest::setMaxDownloadBytesPerSec(qint64 bytesPerSec)
{
    d.data()->transferConfig.setMaxDownloadBytesPerSec(bytesPerSec);
    return *this;
}

std::optional<qint64> QCNetworkRequest::maxDownloadBytesPerSec() const
{
    return d.constData()->transferConfig.maxDownloadBytesPerSec();
}

QCNetworkRequest &QCNetworkRequest::setMaxUploadBytesPerSec(qint64 bytesPerSec)
{
    d.data()->transferConfig.setMaxUploadBytesPerSec(bytesPerSec);
    return *this;
}

std::optional<qint64> QCNetworkRequest::maxUploadBytesPerSec() const
{
    return d.constData()->transferConfig.maxUploadBytesPerSec();
}

QCNetworkRequest &QCNetworkRequest::setBackpressureLimitBytes(qint64 bytes)
{
    d.data()->transferConfig.setBackpressureLimitBytes(bytes);
    return *this;
}

qint64 QCNetworkRequest::backpressureLimitBytes() const noexcept
{
    return d.constData()->transferConfig.backpressureLimitBytes();
}

QCNetworkRequest &QCNetworkRequest::setBackpressureResumeBytes(qint64 bytes)
{
    d.data()->transferConfig.setBackpressureResumeBytes(bytes);
    return *this;
}

qint64 QCNetworkRequest::backpressureResumeBytes() const noexcept
{
    return d.constData()->transferConfig.backpressureResumeBytes();
}

QCNetworkRequest &QCNetworkRequest::setExpect100ContinueTimeout(std::chrono::milliseconds timeout)
{
    d.data()->transferConfig.setExpect100ContinueTimeout(timeout);
    return *this;
}

std::optional<std::chrono::milliseconds> QCNetworkRequest::expect100ContinueTimeout() const
{
    return d.constData()->transferConfig.expect100ContinueTimeout();
}

QCNetworkRequest &QCNetworkRequest::setIpResolve(QCNetworkIpResolve resolve)
{
    d.data()->transferConfig.setIpResolve(resolve);
    return *this;
}

std::optional<QCNetworkIpResolve> QCNetworkRequest::ipResolve() const
{
    return d.constData()->transferConfig.ipResolve();
}

#ifdef QCURL_ENABLE_ADVANCED_REQUEST_NETWORK_PATH_API
QCNetworkRequest &QCNetworkRequest::setHappyEyeballsTimeout(std::chrono::milliseconds timeout)
{
    if (timeout.count() < 0) {
        qWarning() << "QCNetworkRequest: happyEyeballsTimeout must be >= 0, got" << timeout.count()
                   << "(ignored)";
        d.data()->happyEyeballsTimeout.reset();
        return *this;
    }

    d.data()->happyEyeballsTimeout = timeout;
    return *this;
}

std::optional<std::chrono::milliseconds> QCNetworkRequest::happyEyeballsTimeout() const
{
    return d.constData()->happyEyeballsTimeout;
}

QCNetworkRequest &QCNetworkRequest::setNetworkInterface(const QString &interfaceName)
{
    const QString trimmed = interfaceName.trimmed();
    if (trimmed.isEmpty()) {
        d.data()->networkInterface.reset();
        return *this;
    }

    d.data()->networkInterface = trimmed;
    return *this;
}

std::optional<QString> QCNetworkRequest::networkInterface() const
{
    return d.constData()->networkInterface;
}

QCNetworkRequest &QCNetworkRequest::setLocalPortRange(int port, int range)
{
    if (port <= 0 || port > 65535) {
        qWarning() << "QCNetworkRequest: localPort must be in [1, 65535], got" << port
                   << "(ignored)";
        d.data()->localPort.reset();
        d.data()->localPortRange.reset();
        return *this;
    }

    if (range < 0 || range > 65535) {
        qWarning() << "QCNetworkRequest: localPortRange must be in [0, 65535], got" << range
                   << "(ignored)";
        d.data()->localPort.reset();
        d.data()->localPortRange.reset();
        return *this;
    }

    d.data()->localPort = port;
    d.data()->localPortRange = range;
    return *this;
}

std::optional<int> QCNetworkRequest::localPort() const
{
    return d.constData()->localPort;
}

std::optional<int> QCNetworkRequest::localPortRange() const
{
    return d.constData()->localPortRange;
}

QCNetworkRequest &QCNetworkRequest::setResolveOverride(const QStringList &entries)
{
    d.data()->resolveOverride = cleanedAdvancedStringList(entries);
    if (d.constData()->resolveOverride->isEmpty()) {
        d.data()->resolveOverride.reset();
    }
    return *this;
}

std::optional<QStringList> QCNetworkRequest::resolveOverride() const
{
    return d.constData()->resolveOverride;
}

QCNetworkRequest &QCNetworkRequest::setConnectTo(const QStringList &entries)
{
    d.data()->connectTo = cleanedAdvancedStringList(entries);
    if (d.constData()->connectTo->isEmpty()) {
        d.data()->connectTo.reset();
    }
    return *this;
}

std::optional<QStringList> QCNetworkRequest::connectTo() const
{
    return d.constData()->connectTo;
}

QCNetworkRequest &QCNetworkRequest::setDnsServers(const QStringList &servers)
{
    d.data()->dnsServers = cleanedAdvancedStringList(servers);
    if (d.constData()->dnsServers->isEmpty()) {
        d.data()->dnsServers.reset();
    }
    return *this;
}

std::optional<QStringList> QCNetworkRequest::dnsServers() const
{
    return d.constData()->dnsServers;
}

QCNetworkRequest &QCNetworkRequest::setDohUrl(const QUrl &url)
{
    if (url.isEmpty()) {
        d.data()->dohUrl.reset();
        return *this;
    }

    d.data()->dohUrl = url;
    return *this;
}

std::optional<QUrl> QCNetworkRequest::dohUrl() const
{
    return d.constData()->dohUrl;
}
#endif

QCNetworkRequest &QCNetworkRequest::setAllowedProtocols(const QStringList &protocols)
{
    d.data()->transferConfig.setAllowedProtocols(protocols);
    return *this;
}

std::optional<QStringList> QCNetworkRequest::allowedProtocols() const
{
    return d.constData()->transferConfig.allowedProtocols();
}

QCNetworkRequest &QCNetworkRequest::setAllowedRedirectProtocols(const QStringList &protocols)
{
    d.data()->transferConfig.setAllowedRedirectProtocols(protocols);
    return *this;
}

std::optional<QStringList> QCNetworkRequest::allowedRedirectProtocols() const
{
    return d.constData()->transferConfig.allowedRedirectProtocols();
}

QCNetworkRequest &QCNetworkRequest::setUnsupportedSecurityOptionPolicy(
    QCUnsupportedSecurityOptionPolicy policy)
{
    d.data()->transferConfig.setUnsupportedSecurityOptionPolicy(policy);
    return *this;
}

QCUnsupportedSecurityOptionPolicy QCNetworkRequest::unsupportedSecurityOptionPolicy() const
{
    return d.constData()->transferConfig.unsupportedSecurityOptionPolicy();
}

} // namespace QCurl
