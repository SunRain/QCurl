// SPDX-License-Identifier: MIT
// Copyright (c) 2025 QCurl Project

#include "QCNetworkProtocolPolicy_p.h"

#include <QSet>

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

QStringList QCNetworkProtocolPolicy::coreProtocols()
{
    return {QStringLiteral("http"), QStringLiteral("https")};
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

bool QCNetworkProtocolPolicy::resolveInitialProtocols(const std::optional<QStringList> &requested,
                                                      QStringList *effective,
                                                      QString *error)
{
    return resolveProtocols(requested, QStringLiteral("initial request"), effective, error);
}

bool QCNetworkProtocolPolicy::resolveRedirectProtocols(const std::optional<QStringList> &requested,
                                                       QStringList *effective,
                                                       QString *error)
{
    return resolveProtocols(requested, QStringLiteral("redirect"), effective, error);
}

bool QCNetworkProtocolPolicy::resolveProtocols(const std::optional<QStringList> &requested,
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

    if (!requested.has_value()) {
        *effective = coreProtocols();
        return true;
    }

    QStringList normalized;
    QSet<QString> seen;
    for (const QString &value : requested.value()) {
        const QString protocol = value.trimmed().toLower();
        if (protocol.isEmpty()) {
            continue;
        }

        if (protocol != QStringLiteral("http") && protocol != QStringLiteral("https")) {
            if (error) {
                *error = QStringLiteral(
                             "QCurl Core %1 protocol allowlist may contain only HTTP/HTTPS")
                             .arg(scope);
            }
            return false;
        }

        if (!seen.contains(protocol)) {
            seen.insert(protocol);
            normalized.append(protocol);
        }
    }

    if (normalized.isEmpty()) {
        if (error) {
            *error = QStringLiteral("QCurl Core %1 protocol allowlist must contain HTTP or HTTPS")
                         .arg(scope);
        }
        return false;
    }

    *effective = normalized;
    return true;
}

} // namespace QCurl::Internal
