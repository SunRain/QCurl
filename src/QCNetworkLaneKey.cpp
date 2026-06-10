// SPDX-License-Identifier: MIT
// Copyright (c) 2025 QCurl Project

#include "QCNetworkLaneKey.h"

#include <QDebugStateSaver>
#include <QSharedData>

#include <utility>

namespace QCurl {

namespace {

void setError(QString *error, const QString &message)
{
    if (error) {
        *error = message;
    }
}

} // namespace

class QCNetworkLaneKeyData : public QSharedData
{
public:
    QString name;
    bool valid = false;
};

QCNetworkLaneKey::QCNetworkLaneKey()
    : d(new QCNetworkLaneKeyData)
{
}

QCNetworkLaneKey::QCNetworkLaneKey(const QCNetworkLaneKey &other) = default;

QCNetworkLaneKey::QCNetworkLaneKey(QCNetworkLaneKey &&other) noexcept = default;

QCNetworkLaneKey::~QCNetworkLaneKey() = default;

QCNetworkLaneKey &QCNetworkLaneKey::operator=(const QCNetworkLaneKey &other) = default;

QCNetworkLaneKey &QCNetworkLaneKey::operator=(QCNetworkLaneKey &&other) noexcept = default;

QCNetworkLaneKey::QCNetworkLaneKey(QString name, bool valid)
    : d(new QCNetworkLaneKeyData)
{
    d->name = std::move(name);
    d->valid = valid;
}

QString QCNetworkLaneKey::name() const
{
    return d->name;
}

bool QCNetworkLaneKey::isValid() const
{
    return d->valid;
}

bool QCNetworkLaneKey::isDefault() const
{
    return d->valid && d->name.isEmpty();
}

QCNetworkLaneKey QCNetworkLaneKey::defaultLane()
{
    return QCNetworkLaneKey(QString(), true);
}

QCNetworkLaneKey QCNetworkLaneKey::control()
{
    return QCNetworkLaneKey(QStringLiteral("Control"), true);
}

QCNetworkLaneKey QCNetworkLaneKey::transfer()
{
    return QCNetworkLaneKey(QStringLiteral("Transfer"), true);
}

QCNetworkLaneKey QCNetworkLaneKey::background()
{
    return QCNetworkLaneKey(QStringLiteral("Background"), true);
}

bool QCNetworkLaneKey::fromName(QAnyStringView name, QCNetworkLaneKey *out, QString *error)
{
    if (!out) {
        setError(error, QStringLiteral("QCNetworkLaneKey::fromName requires a non-null output key"));
        return false;
    }

    const QString normalizedName = name.toString().trimmed();
    if (normalizedName.isEmpty()) {
        setError(error, QStringLiteral("QCNetworkLaneKey lane name must not be empty"));
        return false;
    }

    *out = QCNetworkLaneKey(normalizedName, true);
    setError(error, QString());
    return true;
}

QDebug operator<<(QDebug dbg, const QCNetworkLaneKey &lane)
{
    QDebugStateSaver saver(dbg);
    dbg.nospace() << "QCNetworkLaneKey(" << lane.name() << ")";
    return dbg;
}

} // namespace QCurl
