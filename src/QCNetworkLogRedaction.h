// SPDX-License-Identifier: MIT
// Copyright (c) 2025 QCurl Project

#ifndef QCNETWORKLOGREDACTION_H
#define QCNETWORKLOGREDACTION_H

#include <QByteArray>
#include <QString>
#include <QUrl>

namespace QCurl::QCNetworkLogRedaction {

[[nodiscard]] bool isSensitiveQueryKey(const QString &keyLower);
[[nodiscard]] bool isSensitiveHeaderKey(const QByteArray &keyLower);

[[nodiscard]] QString redactSensitiveQueryParams(const QString &line);
[[nodiscard]] QString redactSensitiveTraceLine(const QByteArray &line);
[[nodiscard]] QString redactUrl(const QUrl &url);

} // namespace QCurl::QCNetworkLogRedaction

#endif // QCNETWORKLOGREDACTION_H
