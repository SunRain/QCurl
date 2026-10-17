#include "QCNetworkRequest.h"
#include "private/QCNetworkRequestPrivate_p.h"

#include <QDebug>
#include <QUrl>

#include <algorithm>
#include <chrono>
#include <optional>

namespace QCurl {

namespace {

#ifdef QCURL_ENABLE_ADVANCED_REQUEST_NETWORK_PATH_API
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
#endif

bool isDecimal(const QString &value)
{
    return !value.isEmpty() && std::all_of(value.cbegin(), value.cend(), [](QChar ch) {
        return ch >= QLatin1Char('0') && ch <= QLatin1Char('9');
    });
}

std::optional<QString> normalizedResolveHost(const QString &host)
{
    if (host.isEmpty() || host.startsWith(QLatin1Char('-'))
        || std::any_of(host.cbegin(), host.cend(), [](QChar ch) {
               return ch.isSpace() || ch.category() == QChar::Other_Control;
           })) {
        return std::nullopt;
    }
    // 与请求 URL 共用 Qt 的 IDNA 规则，不能把原始 Unicode 交给 libcurl 匹配 ACE 主机。
    const QString aceHost = QString::fromLatin1(QUrl::toAce(host));
    if (aceHost.isEmpty() || aceHost.startsWith(QLatin1Char('-'))
        || std::any_of(aceHost.cbegin(), aceHost.cend(), [](QChar ch) {
               return !((ch.isLetterOrNumber() && ch.unicode() < 128) || ch == QLatin1Char('.')
                        || ch == QLatin1Char('-') || ch == QLatin1Char('_'));
           })) {
        return std::nullopt;
    }
    return aceHost;
}

bool isResolveIpv4Address(const QString &address)
{
    const auto octets = address.split(QLatin1Char('.'));
    if (octets.size() != 4) {
        return false;
    }
    for (const auto &octet : octets) {
        bool ok         = false;
        const int value = octet.toInt(&ok);
        if (!isDecimal(octet) || !ok || value > 255
            || (octet.size() > 1 && octet.startsWith(QLatin1Char('0')))) {
            return false;
        }
    }
    return true;
}

std::optional<QString> normalizedResolveEntry(const QString &entry)
{
    const auto parts = entry.trimmed().split(QLatin1Char(':'));
    if (parts.size() != 3) {
        return std::nullopt;
    }
    const auto host       = normalizedResolveHost(parts[0].trimmed());
    const QString port    = parts[1].trimmed();
    const QString address = parts[2].trimmed();
    bool ok               = false;
    const int number      = port.toInt(&ok);
    if (!host || !isDecimal(port) || !ok || number < 1 || number > 65535
        || !isResolveIpv4Address(address)) {
        return std::nullopt;
    }
    return *host + QLatin1Char(':') + QString::number(number) + QLatin1Char(':') + address;
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

QCNetworkConfigUpdateResult QCNetworkRequest::setMaxDownloadBytesPerSec(qint64 bytesPerSec)
{
    return d.data()->transferConfig.setMaxDownloadBytesPerSec(bytesPerSec);
}

std::optional<qint64> QCNetworkRequest::maxDownloadBytesPerSec() const
{
    return d.constData()->transferConfig.maxDownloadBytesPerSec();
}

QCNetworkConfigUpdateResult QCNetworkRequest::setMaxUploadBytesPerSec(qint64 bytesPerSec)
{
    return d.data()->transferConfig.setMaxUploadBytesPerSec(bytesPerSec);
}

std::optional<qint64> QCNetworkRequest::maxUploadBytesPerSec() const
{
    return d.constData()->transferConfig.maxUploadBytesPerSec();
}

QCNetworkConfigUpdateResult QCNetworkRequest::setBackpressureLimitBytes(qint64 bytes)
{
    return d.data()->transferConfig.setBackpressureLimitBytes(bytes);
}

qint64 QCNetworkRequest::backpressureLimitBytes() const noexcept
{
    return d.constData()->transferConfig.backpressureLimitBytes();
}

QCNetworkConfigUpdateResult QCNetworkRequest::setBackpressureResumeBytes(qint64 bytes)
{
    return d.data()->transferConfig.setBackpressureResumeBytes(bytes);
}

qint64 QCNetworkRequest::backpressureResumeBytes() const noexcept
{
    return d.constData()->transferConfig.backpressureResumeBytes();
}

QCNetworkConfigUpdateResult QCNetworkRequest::setExpect100ContinueTimeout(
    std::chrono::milliseconds timeout)
{
    return d.data()->transferConfig.setExpect100ContinueTimeout(timeout);
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

QCNetworkConfigUpdateResult QCNetworkRequest::setResolveOverride(const QStringList &entries)
{
    QStringList candidate;
    candidate.reserve(entries.size());
    for (const auto &entry : entries) {
        const auto normalized = normalizedResolveEntry(entry);
        if (!normalized) {
            return QCNetworkConfigUpdateResult::InvalidArgument;
        }
        candidate.append(*normalized);
    }
    d.data()->resolveOverride = candidate.isEmpty() ? std::nullopt
                                                    : std::optional<QStringList>(candidate);
    return QCNetworkConfigUpdateResult::Applied;
}

std::optional<QStringList> QCNetworkRequest::resolveOverride() const
{
    return d.constData()->resolveOverride;
}

QCNetworkConfigUpdateResult QCNetworkRequest::setResolveIsolation(QCNetworkResolveIsolation isolation)
{
    if (isolation != QCNetworkResolveIsolation::Isolated
        && isolation != QCNetworkResolveIsolation::NativeShared) {
        return QCNetworkConfigUpdateResult::InvalidArgument;
    }
    d.data()->resolveIsolation = isolation;
    return QCNetworkConfigUpdateResult::Applied;
}

QCNetworkResolveIsolation QCNetworkRequest::resolveIsolation() const noexcept
{
    return d.constData()->resolveIsolation;
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

    d.data()->localPort      = port;
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

QCNetworkRequest &QCNetworkRequest::setAllowedProtocols(QCNetworkProtocols protocols)
{
    d.data()->transferConfig.setAllowedProtocols(protocols);
    return *this;
}

std::optional<QCNetworkProtocols> QCNetworkRequest::allowedProtocols() const
{
    return d.constData()->transferConfig.allowedProtocols();
}

QCNetworkRequest &QCNetworkRequest::setAllowedRedirectProtocols(QCNetworkProtocols protocols)
{
    d.data()->transferConfig.setAllowedRedirectProtocols(protocols);
    return *this;
}

std::optional<QCNetworkProtocols> QCNetworkRequest::allowedRedirectProtocols() const
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
