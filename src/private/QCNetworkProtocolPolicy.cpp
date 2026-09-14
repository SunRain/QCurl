// SPDX-License-Identifier: MIT
// Copyright (c) 2025 QCurl Project

#include "QCNetworkProtocolPolicy_p.h"

namespace QCurl::Internal {

bool QCNetworkProtocolPolicy::isValidHttpMethodToken(QByteArrayView method)
{
    if (method.isEmpty()) {
        return false;
    }
    for (const char ch : method) {
        const bool alphaNumeric = (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z')
                                  || (ch >= '0' && ch <= '9');
        if (!alphaNumeric && !QByteArrayView("!#$%&'*+-.^_`|~").contains(ch)) {
            return false;
        }
    }
    return true;
}

bool QCNetworkProtocolPolicy::validateCoreUrl(const QUrl &url, QString *error)
{
    const QString scheme = url.scheme().trimmed().toLower();
    if (!url.isValid() || url.isEmpty() || scheme.isEmpty()) {
        if (error) {
            *error = QStringLiteral("QCurl Core requires a valid HTTP/HTTPS URL");
        }
        return false;
    }

    if (scheme != QStringLiteral("http") && scheme != QStringLiteral("https")) {
        if (error) {
            *error = QStringLiteral("QCurl Core only accepts HTTP/HTTPS URLs");
        }
        return false;
    }

    if (url.host().isEmpty()) {
        if (error) {
            *error = QStringLiteral("QCurl Core requires an HTTP/HTTPS URL with a host");
        }
        return false;
    }

    return true;
}

bool QCNetworkProtocolPolicy::resolveInitialProtocols(std::optional<QCNetworkProtocols> requested,
                                                      QStringList *effective,
                                                      QString *error)
{
    return resolveProtocols(requested, QStringLiteral("initial request"), effective, error);
}

bool QCNetworkProtocolPolicy::resolveRedirectProtocols(std::optional<QCNetworkProtocols> requested,
                                                       QStringList *effective,
                                                       QString *error)
{
    return resolveProtocols(requested, QStringLiteral("redirect"), effective, error);
}

bool QCNetworkProtocolPolicy::resolveProtocols(std::optional<QCNetworkProtocols> requested,
                                               const QString &scope,
                                               QStringList *effective,
                                               QString *error)
{
    if (!effective) {
        if (error) {
            *error = QStringLiteral("QCurl Core protocol policy output is null");
        }
        return false;
    }

    constexpr auto kCoreProtocols = QCNetworkProtocol::Http | QCNetworkProtocol::Https;
    const auto protocols          = requested && *requested ? *requested : kCoreProtocols;
    if ((protocols.toInt() & ~kCoreProtocols.toInt()) != 0) {
        if (error) {
            *error = QStringLiteral(
                         "QCurl Core %1 protocol allowlist contains invalid HTTP/HTTPS flags")
                         .arg(scope);
        }
        return false;
    }

    QStringList normalized;
    if (protocols.testFlag(QCNetworkProtocol::Http)) {
        normalized.append(QStringLiteral("http"));
    }
    if (protocols.testFlag(QCNetworkProtocol::Https)) {
        normalized.append(QStringLiteral("https"));
    }
    *effective = normalized;
    return true;
}

} // namespace QCurl::Internal
