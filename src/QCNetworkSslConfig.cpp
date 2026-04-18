#include "QCNetworkSslConfig.h"

#include <QSharedData>

namespace QCurl {

class QCNetworkSslConfigData : public QSharedData
{
public:
    bool verifyPeer = true;
    bool verifyHost = true;
    QString caCertPath;
    QString clientCertPath;
    QString clientKeyPath;
    QString clientKeyPassword;
    QString pinnedPublicKey;
    std::optional<QCNetworkTlsVersion> minTlsVersion = std::nullopt;
    QString cipherList;
    QString tls13Ciphers;
    QCUnsupportedSecurityOptionPolicy unsupportedSecurityPolicy
        = QCUnsupportedSecurityOptionPolicy::Fail;
};

QCNetworkSslConfig::QCNetworkSslConfig()
    : d(new QCNetworkSslConfigData)
{
}

QCNetworkSslConfig::QCNetworkSslConfig(const QCNetworkSslConfig &other) = default;

QCNetworkSslConfig::QCNetworkSslConfig(QCNetworkSslConfig &&other) = default;

QCNetworkSslConfig::~QCNetworkSslConfig() = default;

QCNetworkSslConfig &QCNetworkSslConfig::operator=(const QCNetworkSslConfig &other) = default;

QCNetworkSslConfig &QCNetworkSslConfig::operator=(QCNetworkSslConfig &&other) = default;

bool QCNetworkSslConfig::verifyPeer() const
{
    return d->verifyPeer;
}

void QCNetworkSslConfig::setVerifyPeer(bool value)
{
    d->verifyPeer = value;
}

bool QCNetworkSslConfig::verifyHost() const
{
    return d->verifyHost;
}

void QCNetworkSslConfig::setVerifyHost(bool value)
{
    d->verifyHost = value;
}

QString QCNetworkSslConfig::caCertPath() const
{
    return d->caCertPath;
}

void QCNetworkSslConfig::setCaCertPath(const QString &path)
{
    d->caCertPath = path;
}

QString QCNetworkSslConfig::clientCertPath() const
{
    return d->clientCertPath;
}

void QCNetworkSslConfig::setClientCertPath(const QString &path)
{
    d->clientCertPath = path;
}

QString QCNetworkSslConfig::clientKeyPath() const
{
    return d->clientKeyPath;
}

void QCNetworkSslConfig::setClientKeyPath(const QString &path)
{
    d->clientKeyPath = path;
}

QString QCNetworkSslConfig::clientKeyPassword() const
{
    return d->clientKeyPassword;
}

void QCNetworkSslConfig::setClientKeyPassword(const QString &password)
{
    d->clientKeyPassword = password;
}

QString QCNetworkSslConfig::pinnedPublicKey() const
{
    return d->pinnedPublicKey;
}

void QCNetworkSslConfig::setPinnedPublicKey(const QString &pinnedPublicKey)
{
    d->pinnedPublicKey = pinnedPublicKey;
}

std::optional<QCNetworkTlsVersion> QCNetworkSslConfig::minTlsVersion() const
{
    return d->minTlsVersion;
}

void QCNetworkSslConfig::setMinTlsVersion(const std::optional<QCNetworkTlsVersion> &version)
{
    d->minTlsVersion = version;
}

QString QCNetworkSslConfig::cipherList() const
{
    return d->cipherList;
}

void QCNetworkSslConfig::setCipherList(const QString &cipherList)
{
    d->cipherList = cipherList;
}

QString QCNetworkSslConfig::tls13Ciphers() const
{
    return d->tls13Ciphers;
}

void QCNetworkSslConfig::setTls13Ciphers(const QString &cipherList)
{
    d->tls13Ciphers = cipherList;
}

QCUnsupportedSecurityOptionPolicy QCNetworkSslConfig::unsupportedSecurityPolicy() const
{
    return d->unsupportedSecurityPolicy;
}

void QCNetworkSslConfig::setUnsupportedSecurityPolicy(QCUnsupportedSecurityOptionPolicy policy)
{
    d->unsupportedSecurityPolicy = policy;
}

QCNetworkSslConfig QCNetworkSslConfig::defaultConfig()
{
    QCNetworkSslConfig config;
    config.setVerifyPeer(true);
    config.setVerifyHost(true);
    // 其他字段使用默认值（空字符串表示使用系统默认）
    return config;
}

QCNetworkSslConfig QCNetworkSslConfig::insecureConfig()
{
    QCNetworkSslConfig config;
    config.setVerifyPeer(false);
    config.setVerifyHost(false);
    return config;
}

} // namespace QCurl
