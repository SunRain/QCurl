// SPDX-License-Identifier: MIT
// Copyright (c) 2025 QCurl Project

#include "QCNetworkLogRedaction.h"

#include <QStringList>

namespace QCurl::QCNetworkLogRedaction {

bool isSensitiveQueryKey(const QString &keyLower)
{
    return keyLower == QStringLiteral("token") || keyLower == QStringLiteral("access_token")
           || keyLower == QStringLiteral("id_token") || keyLower == QStringLiteral("refresh_token")
           || keyLower == QStringLiteral("api_key") || keyLower == QStringLiteral("apikey")
           || keyLower == QStringLiteral("key") || keyLower == QStringLiteral("secret")
           || keyLower == QStringLiteral("signature") || keyLower == QStringLiteral("sig")
           || keyLower == QStringLiteral("password") || keyLower == QStringLiteral("passwd")
           || keyLower == QStringLiteral("pwd");
}

bool isSensitiveHeaderKey(const QByteArray &keyLower)
{
    if (keyLower == "authorization" || keyLower == "proxy-authorization" || keyLower == "cookie"
        || keyLower == "set-cookie") {
        return true;
    }

    if (keyLower == "apikey" || keyLower.contains("api-key")) {
        return true;
    }

    if (keyLower.contains("token") || keyLower.contains("secret") || keyLower.contains("signature")
        || keyLower == "sig" || keyLower.contains("password") || keyLower.contains("passwd")
        || keyLower == "pwd") {
        return true;
    }

    return false;
}

QString redactSensitiveQueryParams(const QString &line)
{
    const int qpos = line.indexOf(QLatin1Char('?'));
    if (qpos < 0) {
        return line;
    }

    int end = -1;
    for (int i = qpos + 1; i < line.size(); ++i) {
        if (line.at(i).isSpace()) {
            end = i;
            break;
        }
    }
    if (end < 0) {
        end = line.size();
    }

    const QString before = line.left(qpos + 1);
    const QString query  = line.mid(qpos + 1, end - qpos - 1);
    const QString after  = line.mid(end);

    QStringList parts = query.split(QLatin1Char('&'));
    for (QString &part : parts) {
        const int eq = part.indexOf(QLatin1Char('='));
        if (eq <= 0) {
            continue;
        }
        const QString key = part.left(eq).trimmed();
        if (key.isEmpty()) {
            continue;
        }
        if (isSensitiveQueryKey(key.toLower())) {
            part = key + QStringLiteral("=[REDACTED]");
        }
    }

    return before + parts.join(QLatin1Char('&')) + after;
}

QString redactSensitiveTraceLine(const QByteArray &line)
{
    QByteArray trimmed = line;
    if (trimmed.endsWith('\n')) {
        trimmed.chop(1);
    }
    if (trimmed.endsWith('\r')) {
        trimmed.chop(1);
    }

    const int colonPos = trimmed.indexOf(':');
    if (colonPos <= 0) {
        return redactSensitiveQueryParams(QString::fromUtf8(trimmed));
    }

    const QByteArray key      = trimmed.left(colonPos).trimmed();
    const QByteArray keyLower = key.toLower();
    if (isSensitiveHeaderKey(keyLower)) {
        return QString::fromUtf8(key) + QStringLiteral(": [REDACTED]");
    }

    return redactSensitiveQueryParams(QString::fromUtf8(trimmed));
}

QString redactUrl(const QUrl &url)
{
    return redactSensitiveQueryParams(url.toString());
}

} // namespace QCurl::QCNetworkLogRedaction
