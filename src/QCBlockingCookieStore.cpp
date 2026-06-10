#include "QCBlockingCookieStore.h"

#include <QSharedData>

namespace QCurl {

/** 请求 cookie 快照的共享数据。 */
class QCCookieSnapshotData : public QSharedData
{
public:
    QList<QCCookie> cookies;
};

/** 响应 cookie 增量的共享数据。 */
class QCCookieDeltaData : public QSharedData
{
public:
    QList<QCCookie> cookies;
};

QCCookieSnapshot::QCCookieSnapshot()
    : d(new QCCookieSnapshotData)
{
}

QCCookieSnapshot::QCCookieSnapshot(const QList<QCCookie> &cookies)
    : d(new QCCookieSnapshotData)
{
    d->cookies = cookies;
}

QCCookieSnapshot::QCCookieSnapshot(const QCCookieSnapshot &other) = default;

QCCookieSnapshot::QCCookieSnapshot(QCCookieSnapshot &&other) noexcept = default;

QCCookieSnapshot::~QCCookieSnapshot() = default;

QCCookieSnapshot &QCCookieSnapshot::operator=(const QCCookieSnapshot &other) = default;

QCCookieSnapshot &QCCookieSnapshot::operator=(QCCookieSnapshot &&other) noexcept = default;

QList<QCCookie> QCCookieSnapshot::cookies() const
{
    return d->cookies;
}

void QCCookieSnapshot::setCookies(const QList<QCCookie> &cookies)
{
    d->cookies = cookies;
}

QCCookieDelta::QCCookieDelta()
    : d(new QCCookieDeltaData)
{
}

QCCookieDelta::QCCookieDelta(const QList<QCCookie> &cookies)
    : d(new QCCookieDeltaData)
{
    d->cookies = cookies;
}

QCCookieDelta::QCCookieDelta(const QCCookieDelta &other) = default;

QCCookieDelta::QCCookieDelta(QCCookieDelta &&other) noexcept = default;

QCCookieDelta::~QCCookieDelta() = default;

QCCookieDelta &QCCookieDelta::operator=(const QCCookieDelta &other) = default;

QCCookieDelta &QCCookieDelta::operator=(QCCookieDelta &&other) noexcept = default;

QList<QCCookie> QCCookieDelta::cookies() const
{
    return d->cookies;
}

void QCCookieDelta::setCookies(const QList<QCCookie> &cookies)
{
    d->cookies = cookies;
}

bool QCCookieDelta::isEmpty() const
{
    return d->cookies.isEmpty();
}

} // namespace QCurl
