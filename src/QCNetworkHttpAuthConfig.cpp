// SPDX-License-Identifier: MIT
// Copyright (c) 2025 QCurl Project

#include "QCNetworkRequestConfig.h"

#include <QSharedData>

namespace QCurl {

class QCNetworkHttpAuthConfigData : public QSharedData
{
public:
    QString userName;
    QString password;
    QCNetworkHttpAuthMethod method = QCNetworkHttpAuthMethod::Basic;
    bool allowUnrestrictedAuth = false;
    bool warnIfBasicOverHttp = true;
};

QCNetworkHttpAuthConfig::QCNetworkHttpAuthConfig()
    : d(new QCNetworkHttpAuthConfigData)
{
}

QCNetworkHttpAuthConfig::QCNetworkHttpAuthConfig(const QCNetworkHttpAuthConfig &other) = default;

QCNetworkHttpAuthConfig::QCNetworkHttpAuthConfig(QCNetworkHttpAuthConfig &&other) = default;

QCNetworkHttpAuthConfig::~QCNetworkHttpAuthConfig() = default;

QCNetworkHttpAuthConfig &QCNetworkHttpAuthConfig::operator=(const QCNetworkHttpAuthConfig &other)
    = default;

QCNetworkHttpAuthConfig &QCNetworkHttpAuthConfig::operator=(QCNetworkHttpAuthConfig &&other)
    = default;

QString QCNetworkHttpAuthConfig::userName() const
{
    return d->userName;
}

void QCNetworkHttpAuthConfig::setUserName(const QString &userName)
{
    d->userName = userName;
}

QString QCNetworkHttpAuthConfig::password() const
{
    return d->password;
}

void QCNetworkHttpAuthConfig::setPassword(const QString &password)
{
    d->password = password;
}

QCNetworkHttpAuthMethod QCNetworkHttpAuthConfig::method() const
{
    return d->method;
}

void QCNetworkHttpAuthConfig::setMethod(QCNetworkHttpAuthMethod method)
{
    d->method = method;
}

bool QCNetworkHttpAuthConfig::allowUnrestrictedAuth() const
{
    return d->allowUnrestrictedAuth;
}

void QCNetworkHttpAuthConfig::setAllowUnrestrictedAuth(bool enabled)
{
    d->allowUnrestrictedAuth = enabled;
}

bool QCNetworkHttpAuthConfig::warnIfBasicOverHttp() const
{
    return d->warnIfBasicOverHttp;
}

void QCNetworkHttpAuthConfig::setWarnIfBasicOverHttp(bool enabled)
{
    d->warnIfBasicOverHttp = enabled;
}


} // namespace QCurl
