// SPDX-License-Identifier: MIT
// Copyright (c) 2025 QCurl Project

#include "QCNetworkRequestConfig.h"

#include <QDebug>
#include <QSharedData>

#include <optional>

namespace QCurl {

class QCNetworkRedirectConfigData : public QSharedData
{
public:
    bool followLocation = true;
    std::optional<int> maxRedirects;
    QCNetworkPostRedirectPolicy postRedirectPolicy = QCNetworkPostRedirectPolicy::Default;
    bool autoRefererEnabled = false;
    QString referer;
    bool allowUnrestrictedSensitiveHeadersOnRedirect = false;
};

QCNetworkRedirectConfig::QCNetworkRedirectConfig()
    : d(new QCNetworkRedirectConfigData)
{}

QCNetworkRedirectConfig::QCNetworkRedirectConfig(const QCNetworkRedirectConfig &other) = default;

QCNetworkRedirectConfig::QCNetworkRedirectConfig(QCNetworkRedirectConfig &&other) = default;

QCNetworkRedirectConfig::~QCNetworkRedirectConfig() = default;

QCNetworkRedirectConfig &QCNetworkRedirectConfig::operator=(const QCNetworkRedirectConfig &other)
    = default;

QCNetworkRedirectConfig &QCNetworkRedirectConfig::operator=(QCNetworkRedirectConfig &&other)
    = default;

bool QCNetworkRedirectConfig::followLocation() const
{
    return d->followLocation;
}

void QCNetworkRedirectConfig::setFollowLocation(bool enabled)
{
    d->followLocation = enabled;
}

std::optional<int> QCNetworkRedirectConfig::maxRedirects() const
{
    return d->maxRedirects;
}

void QCNetworkRedirectConfig::setMaxRedirects(std::optional<int> maxRedirects)
{
    if (maxRedirects.has_value() && maxRedirects.value() < 0) {
        qWarning() << "QCNetworkRedirectConfig: maxRedirects must be >= 0, got"
                   << maxRedirects.value() << "(ignored)";
        d->maxRedirects.reset();
        return;
    }
    d->maxRedirects = maxRedirects;
}

QCNetworkPostRedirectPolicy QCNetworkRedirectConfig::postRedirectPolicy() const
{
    return d->postRedirectPolicy;
}

void QCNetworkRedirectConfig::setPostRedirectPolicy(QCNetworkPostRedirectPolicy policy)
{
    d->postRedirectPolicy = policy;
}

bool QCNetworkRedirectConfig::autoRefererEnabled() const
{
    return d->autoRefererEnabled;
}

void QCNetworkRedirectConfig::setAutoRefererEnabled(bool enabled)
{
    d->autoRefererEnabled = enabled;
}

QString QCNetworkRedirectConfig::referer() const
{
    return d->referer;
}

void QCNetworkRedirectConfig::setReferer(const QString &referer)
{
    d->referer = referer;
}

bool QCNetworkRedirectConfig::allowUnrestrictedSensitiveHeadersOnRedirect() const
{
    return d->allowUnrestrictedSensitiveHeadersOnRedirect;
}

void QCNetworkRedirectConfig::setAllowUnrestrictedSensitiveHeadersOnRedirect(bool enabled)
{
    d->allowUnrestrictedSensitiveHeadersOnRedirect = enabled;
}

} // namespace QCurl
