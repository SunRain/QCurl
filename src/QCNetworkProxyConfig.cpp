#include "QCNetworkProxyConfig.h"

#include <QSharedData>

namespace QCurl {

class QCNetworkProxyTlsConfigData : public QSharedData
{
public:
    bool verifyPeer = true;
    bool verifyHost = true;
    QString caCertPath;
    std::optional<QCNetworkTlsVersion> minTlsVersion = std::nullopt;
    QString cipherList;
    QString tls13Ciphers;
    QCUnsupportedSecurityOptionPolicy unsupportedSecurityPolicy
        = QCUnsupportedSecurityOptionPolicy::Fail;
};

class QCNetworkProxyConfigData : public QSharedData
{
public:
    QCNetworkProxyConfig::ProxyType type = QCNetworkProxyConfig::ProxyType::None;
    QString hostName;
    quint16 port = 0;
    QString userName;
    QString password;
    std::optional<QCNetworkProxyConfig::ProxyTlsConfig> tlsConfig = std::nullopt;
};

QCNetworkProxyConfig::ProxyTlsConfig::ProxyTlsConfig()
    : d(new QCNetworkProxyTlsConfigData)
{
}

QCNetworkProxyConfig::ProxyTlsConfig::ProxyTlsConfig(const ProxyTlsConfig &other) = default;

QCNetworkProxyConfig::ProxyTlsConfig::ProxyTlsConfig(ProxyTlsConfig &&other) = default;

QCNetworkProxyConfig::ProxyTlsConfig::~ProxyTlsConfig() = default;

QCNetworkProxyConfig::ProxyTlsConfig &QCNetworkProxyConfig::ProxyTlsConfig::operator=(
    const ProxyTlsConfig &other) = default;

QCNetworkProxyConfig::ProxyTlsConfig &QCNetworkProxyConfig::ProxyTlsConfig::operator=(
    ProxyTlsConfig &&other) = default;

bool QCNetworkProxyConfig::ProxyTlsConfig::verifyPeer() const
{
    return d->verifyPeer;
}

void QCNetworkProxyConfig::ProxyTlsConfig::setVerifyPeer(bool value)
{
    d->verifyPeer = value;
}

bool QCNetworkProxyConfig::ProxyTlsConfig::verifyHost() const
{
    return d->verifyHost;
}

void QCNetworkProxyConfig::ProxyTlsConfig::setVerifyHost(bool value)
{
    d->verifyHost = value;
}

QString QCNetworkProxyConfig::ProxyTlsConfig::caCertPath() const
{
    return d->caCertPath;
}

void QCNetworkProxyConfig::ProxyTlsConfig::setCaCertPath(const QString &path)
{
    d->caCertPath = path;
}

std::optional<QCNetworkTlsVersion> QCNetworkProxyConfig::ProxyTlsConfig::minTlsVersion() const
{
    return d->minTlsVersion;
}

void QCNetworkProxyConfig::ProxyTlsConfig::setMinTlsVersion(
    const std::optional<QCNetworkTlsVersion> &version)
{
    d->minTlsVersion = version;
}

QString QCNetworkProxyConfig::ProxyTlsConfig::cipherList() const
{
    return d->cipherList;
}

void QCNetworkProxyConfig::ProxyTlsConfig::setCipherList(const QString &cipherList)
{
    d->cipherList = cipherList;
}

QString QCNetworkProxyConfig::ProxyTlsConfig::tls13Ciphers() const
{
    return d->tls13Ciphers;
}

void QCNetworkProxyConfig::ProxyTlsConfig::setTls13Ciphers(const QString &cipherList)
{
    d->tls13Ciphers = cipherList;
}

QCUnsupportedSecurityOptionPolicy QCNetworkProxyConfig::ProxyTlsConfig::unsupportedSecurityPolicy()
    const
{
    return d->unsupportedSecurityPolicy;
}

void QCNetworkProxyConfig::ProxyTlsConfig::setUnsupportedSecurityPolicy(
    QCUnsupportedSecurityOptionPolicy policy)
{
    d->unsupportedSecurityPolicy = policy;
}

QCNetworkProxyConfig::QCNetworkProxyConfig()
    : d(new QCNetworkProxyConfigData)
{
}

QCNetworkProxyConfig::QCNetworkProxyConfig(const QCNetworkProxyConfig &other) = default;

QCNetworkProxyConfig::QCNetworkProxyConfig(QCNetworkProxyConfig &&other) = default;

QCNetworkProxyConfig::~QCNetworkProxyConfig() = default;

QCNetworkProxyConfig &QCNetworkProxyConfig::operator=(const QCNetworkProxyConfig &other) = default;

QCNetworkProxyConfig &QCNetworkProxyConfig::operator=(QCNetworkProxyConfig &&other) = default;

QCNetworkProxyConfig::ProxyType QCNetworkProxyConfig::type() const
{
    return d->type;
}

void QCNetworkProxyConfig::setType(ProxyType value)
{
    d->type = value;
}

QString QCNetworkProxyConfig::hostName() const
{
    return d->hostName;
}

void QCNetworkProxyConfig::setHostName(const QString &hostName)
{
    d->hostName = hostName;
}

quint16 QCNetworkProxyConfig::port() const
{
    return d->port;
}

void QCNetworkProxyConfig::setPort(quint16 port)
{
    d->port = port;
}

QString QCNetworkProxyConfig::userName() const
{
    return d->userName;
}

void QCNetworkProxyConfig::setUserName(const QString &userName)
{
    d->userName = userName;
}

QString QCNetworkProxyConfig::password() const
{
    return d->password;
}

void QCNetworkProxyConfig::setPassword(const QString &password)
{
    d->password = password;
}

std::optional<QCNetworkProxyConfig::ProxyTlsConfig> QCNetworkProxyConfig::tlsConfig() const
{
    return d->tlsConfig;
}

void QCNetworkProxyConfig::setTlsConfig(const ProxyTlsConfig &config)
{
    d->tlsConfig = config;
}

void QCNetworkProxyConfig::clearTlsConfig()
{
    d->tlsConfig.reset();
}

bool QCNetworkProxyConfig::isValid() const noexcept
{
    if (d->type == ProxyType::None) {
        return true;
    }

    // 需要代理时，检查主机名和端口
    return !d->hostName.isEmpty() && d->port > 0;
}

} // namespace QCurl
