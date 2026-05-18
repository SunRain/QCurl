#include "QCBlockingCookieStore.h"

#include <QSharedData>

#include <utility>

namespace QCurl {

class QCCookieSnapshotData : public QSharedData
{
public:
    QList<QNetworkCookie> cookies;
};

class QCCookieDeltaData : public QSharedData
{
public:
    QList<QNetworkCookie> cookies;
};

QCCookieSnapshot::QCCookieSnapshot()
    : d(new QCCookieSnapshotData)
{}

QCCookieSnapshot::QCCookieSnapshot(QList<QNetworkCookie> cookies)
    : d(new QCCookieSnapshotData)
{
    d->cookies = std::move(cookies);
}

QCCookieSnapshot::QCCookieSnapshot(const QCCookieSnapshot &other) = default;

QCCookieSnapshot::QCCookieSnapshot(QCCookieSnapshot &&other) noexcept = default;

QCCookieSnapshot::~QCCookieSnapshot() = default;

QCCookieSnapshot &QCCookieSnapshot::operator=(const QCCookieSnapshot &other) = default;

QCCookieSnapshot &QCCookieSnapshot::operator=(QCCookieSnapshot &&other) noexcept = default;

QList<QNetworkCookie> QCCookieSnapshot::cookies() const
{
    return d->cookies;
}

void QCCookieSnapshot::setCookies(const QList<QNetworkCookie> &cookies)
{
    d->cookies = cookies;
}

QCCookieDelta::QCCookieDelta()
    : d(new QCCookieDeltaData)
{}

QCCookieDelta::QCCookieDelta(QList<QNetworkCookie> cookies)
    : d(new QCCookieDeltaData)
{
    d->cookies = std::move(cookies);
}

QCCookieDelta::QCCookieDelta(const QCCookieDelta &other) = default;

QCCookieDelta::QCCookieDelta(QCCookieDelta &&other) noexcept = default;

QCCookieDelta::~QCCookieDelta() = default;

QCCookieDelta &QCCookieDelta::operator=(const QCCookieDelta &other) = default;

QCCookieDelta &QCCookieDelta::operator=(QCCookieDelta &&other) noexcept = default;

QList<QNetworkCookie> QCCookieDelta::cookies() const
{
    return d->cookies;
}

void QCCookieDelta::setCookies(const QList<QNetworkCookie> &cookies)
{
    d->cookies = cookies;
}

bool QCCookieDelta::isEmpty() const
{
    return d->cookies.isEmpty();
}

} // namespace QCurl
