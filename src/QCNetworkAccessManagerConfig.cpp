#include "QCNetworkAccessManager.h"

#include <QSharedData>

namespace QCurl {

class ShareHandleConfigData : public QSharedData
{
public:
    bool shareDnsCache = false;
    bool shareCookies = false;
    bool shareSslSession = false;
};

class HstsAltSvcCacheConfigData : public QSharedData
{
public:
    QString hstsFilePath;
    QString altSvcFilePath;
};

QCNetworkAccessManager::ShareHandleConfig::ShareHandleConfig()
    : d(new ShareHandleConfigData)
{}

QCNetworkAccessManager::ShareHandleConfig::ShareHandleConfig(
    const ShareHandleConfig &other) = default;

QCNetworkAccessManager::ShareHandleConfig::ShareHandleConfig(
    ShareHandleConfig &&other) noexcept = default;

QCNetworkAccessManager::ShareHandleConfig::~ShareHandleConfig() = default;

QCNetworkAccessManager::ShareHandleConfig &QCNetworkAccessManager::ShareHandleConfig::operator=(
    const ShareHandleConfig &other) = default;

QCNetworkAccessManager::ShareHandleConfig &QCNetworkAccessManager::ShareHandleConfig::operator=(
    ShareHandleConfig &&other) noexcept = default;

bool QCNetworkAccessManager::ShareHandleConfig::shareDnsCache() const noexcept
{
    return d->shareDnsCache;
}

void QCNetworkAccessManager::ShareHandleConfig::setShareDnsCache(bool enabled)
{
    d->shareDnsCache = enabled;
}

bool QCNetworkAccessManager::ShareHandleConfig::shareCookies() const noexcept
{
    return d->shareCookies;
}

void QCNetworkAccessManager::ShareHandleConfig::setShareCookies(bool enabled)
{
    d->shareCookies = enabled;
}

bool QCNetworkAccessManager::ShareHandleConfig::shareSslSession() const noexcept
{
    return d->shareSslSession;
}

void QCNetworkAccessManager::ShareHandleConfig::setShareSslSession(bool enabled)
{
    d->shareSslSession = enabled;
}

bool QCNetworkAccessManager::ShareHandleConfig::enabled() const noexcept
{
    return d->shareDnsCache || d->shareCookies || d->shareSslSession;
}

QCNetworkAccessManager::HstsAltSvcCacheConfig::HstsAltSvcCacheConfig()
    : d(new HstsAltSvcCacheConfigData)
{}

QCNetworkAccessManager::HstsAltSvcCacheConfig::HstsAltSvcCacheConfig(
    const HstsAltSvcCacheConfig &other) = default;

QCNetworkAccessManager::HstsAltSvcCacheConfig::HstsAltSvcCacheConfig(
    HstsAltSvcCacheConfig &&other) noexcept = default;

QCNetworkAccessManager::HstsAltSvcCacheConfig::~HstsAltSvcCacheConfig() = default;

QCNetworkAccessManager::HstsAltSvcCacheConfig &
QCNetworkAccessManager::HstsAltSvcCacheConfig::operator=(
    const HstsAltSvcCacheConfig &other) = default;

QCNetworkAccessManager::HstsAltSvcCacheConfig &
QCNetworkAccessManager::HstsAltSvcCacheConfig::operator=(
    HstsAltSvcCacheConfig &&other) noexcept = default;

QString QCNetworkAccessManager::HstsAltSvcCacheConfig::hstsFilePath() const
{
    return d->hstsFilePath;
}

void QCNetworkAccessManager::HstsAltSvcCacheConfig::setHstsFilePath(const QString &path)
{
    d->hstsFilePath = path;
}

QString QCNetworkAccessManager::HstsAltSvcCacheConfig::altSvcFilePath() const
{
    return d->altSvcFilePath;
}

void QCNetworkAccessManager::HstsAltSvcCacheConfig::setAltSvcFilePath(const QString &path)
{
    d->altSvcFilePath = path;
}

bool QCNetworkAccessManager::HstsAltSvcCacheConfig::enabled() const noexcept
{
    return !d->hstsFilePath.isEmpty() || !d->altSvcFilePath.isEmpty();
}

} // namespace QCurl
