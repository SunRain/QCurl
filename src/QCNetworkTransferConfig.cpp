// SPDX-License-Identifier: MIT
// Copyright (c) 2025 QCurl Project

#include "QCNetworkRequestConfig.h"

#include <QDebug>
#include <QSharedData>

#include <chrono>
#include <optional>

namespace QCurl {

namespace {

QStringList normalizedStringList(const QStringList &values)
{
    QStringList cleaned;
    cleaned.reserve(values.size());
    for (const QString &value : values) {
        const QString trimmed = value.trimmed();
        if (!trimmed.isEmpty()) {
            cleaned.append(trimmed);
        }
    }
    return cleaned;
}

} // namespace

class QCNetworkTransferConfigData : public QSharedData
{
public:
    bool autoDecompressionEnabled = false;
    QStringList acceptedEncodings;
    std::optional<qint64> maxDownloadBytesPerSec;
    std::optional<qint64> maxUploadBytesPerSec;
    qint64 backpressureLimitBytes = 0;
    qint64 backpressureResumeBytes = 0;
    std::optional<std::chrono::milliseconds> expect100ContinueTimeout;
    std::optional<QCNetworkIpResolve> ipResolve;
    std::optional<QStringList> allowedProtocols;
    std::optional<QStringList> allowedRedirectProtocols;
    QCUnsupportedSecurityOptionPolicy unsupportedSecurityOptionPolicy
        = QCUnsupportedSecurityOptionPolicy::Fail;
};

QCNetworkTransferConfig::QCNetworkTransferConfig()
    : d(new QCNetworkTransferConfigData)
{}

QCNetworkTransferConfig::QCNetworkTransferConfig(const QCNetworkTransferConfig &other) = default;

QCNetworkTransferConfig::QCNetworkTransferConfig(QCNetworkTransferConfig &&other) = default;

QCNetworkTransferConfig::~QCNetworkTransferConfig() = default;

QCNetworkTransferConfig &QCNetworkTransferConfig::operator=(const QCNetworkTransferConfig &other)
    = default;

QCNetworkTransferConfig &QCNetworkTransferConfig::operator=(QCNetworkTransferConfig &&other)
    = default;

bool QCNetworkTransferConfig::autoDecompressionEnabled() const
{
    return d->autoDecompressionEnabled;
}

void QCNetworkTransferConfig::setAutoDecompressionEnabled(bool enabled)
{
    d->autoDecompressionEnabled = enabled;
    if (!enabled) {
        d->acceptedEncodings.clear();
    }
}

QStringList QCNetworkTransferConfig::acceptedEncodings() const
{
    return d->acceptedEncodings;
}

void QCNetworkTransferConfig::setAcceptedEncodings(const QStringList &encodings)
{
    d->acceptedEncodings = normalizedStringList(encodings);
    d->autoDecompressionEnabled = !d->acceptedEncodings.isEmpty();
}

std::optional<qint64> QCNetworkTransferConfig::maxDownloadBytesPerSec() const
{
    return d->maxDownloadBytesPerSec;
}

void QCNetworkTransferConfig::setMaxDownloadBytesPerSec(qint64 bytesPerSec)
{
    if (bytesPerSec < 0) {
        qWarning() << "QCNetworkTransferConfig: maxDownloadBytesPerSec must be >= 0, got"
                   << bytesPerSec << "(ignored)";
        d->maxDownloadBytesPerSec.reset();
        return;
    }
    d->maxDownloadBytesPerSec = bytesPerSec > 0 ? std::optional<qint64>(bytesPerSec) : std::nullopt;
}

std::optional<qint64> QCNetworkTransferConfig::maxUploadBytesPerSec() const
{
    return d->maxUploadBytesPerSec;
}

void QCNetworkTransferConfig::setMaxUploadBytesPerSec(qint64 bytesPerSec)
{
    if (bytesPerSec < 0) {
        qWarning() << "QCNetworkTransferConfig: maxUploadBytesPerSec must be >= 0, got"
                   << bytesPerSec << "(ignored)";
        d->maxUploadBytesPerSec.reset();
        return;
    }
    d->maxUploadBytesPerSec = bytesPerSec > 0 ? std::optional<qint64>(bytesPerSec) : std::nullopt;
}

qint64 QCNetworkTransferConfig::backpressureLimitBytes() const noexcept
{
    return d->backpressureLimitBytes;
}

void QCNetworkTransferConfig::setBackpressureLimitBytes(qint64 bytes)
{
    if (bytes < 0) {
        qWarning() << "QCNetworkTransferConfig: backpressureLimitBytes must be >= 0, got"
                   << bytes << "(ignored)";
        bytes = 0;
    }
    if (bytes > 0 && bytes < 16 * 1024) {
        qWarning() << "QCNetworkTransferConfig: backpressureLimitBytes is very small:" << bytes
                   << "bytes; recommend >= 16384 bytes";
    }
    d->backpressureLimitBytes = bytes;
    if (bytes <= 0) {
        d->backpressureResumeBytes = 0;
    }
}

qint64 QCNetworkTransferConfig::backpressureResumeBytes() const noexcept
{
    return d->backpressureResumeBytes;
}

void QCNetworkTransferConfig::setBackpressureResumeBytes(qint64 bytes)
{
    if (bytes < 0) {
        qWarning() << "QCNetworkTransferConfig: backpressureResumeBytes must be >= 0, got"
                   << bytes << "(ignored)";
        bytes = 0;
    }
    const qint64 limit = d->backpressureLimitBytes;
    if (limit > 0 && bytes > 0 && bytes >= limit) {
        qWarning()
            << "QCNetworkTransferConfig: backpressureResumeBytes must be < backpressureLimitBytes,"
            << "got" << bytes << "(limit=" << limit << "; use default limit/2)";
        bytes = 0;
    }
    d->backpressureResumeBytes = bytes;
}

std::optional<std::chrono::milliseconds> QCNetworkTransferConfig::expect100ContinueTimeout() const
{
    return d->expect100ContinueTimeout;
}

void QCNetworkTransferConfig::setExpect100ContinueTimeout(std::chrono::milliseconds timeout)
{
    if (timeout.count() < 0) {
        qWarning() << "QCNetworkTransferConfig: expect100ContinueTimeout must be >= 0, got"
                   << timeout.count() << "(ignored)";
        d->expect100ContinueTimeout.reset();
        return;
    }
    d->expect100ContinueTimeout = timeout;
}

std::optional<QCNetworkIpResolve> QCNetworkTransferConfig::ipResolve() const
{
    return d->ipResolve;
}

void QCNetworkTransferConfig::setIpResolve(QCNetworkIpResolve resolve)
{
    d->ipResolve = resolve == QCNetworkIpResolve::Any ? std::nullopt
                                                       : std::optional<QCNetworkIpResolve>(resolve);
}

std::optional<QStringList> QCNetworkTransferConfig::allowedProtocols() const
{
    return d->allowedProtocols;
}

void QCNetworkTransferConfig::setAllowedProtocols(const QStringList &protocols)
{
    const QStringList cleaned = normalizedStringList(protocols);
    d->allowedProtocols = cleaned.isEmpty() ? std::nullopt : std::optional<QStringList>(cleaned);
}

std::optional<QStringList> QCNetworkTransferConfig::allowedRedirectProtocols() const
{
    return d->allowedRedirectProtocols;
}

void QCNetworkTransferConfig::setAllowedRedirectProtocols(const QStringList &protocols)
{
    const QStringList cleaned = normalizedStringList(protocols);
    d->allowedRedirectProtocols = cleaned.isEmpty() ? std::nullopt
                                                    : std::optional<QStringList>(cleaned);
}

QCUnsupportedSecurityOptionPolicy QCNetworkTransferConfig::unsupportedSecurityOptionPolicy() const
{
    return d->unsupportedSecurityOptionPolicy;
}

void QCNetworkTransferConfig::setUnsupportedSecurityOptionPolicy(
    QCUnsupportedSecurityOptionPolicy policy)
{
    d->unsupportedSecurityOptionPolicy = policy;
}

} // namespace QCurl
