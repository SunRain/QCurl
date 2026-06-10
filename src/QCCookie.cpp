#include "QCCookie.h"

#include <QSharedData>

namespace QCurl {

/** QCurl cookie 值类型的共享数据。 */
class QCCookieData : public QSharedData
{
public:
    QByteArray name;
    QByteArray value;
    QString domain;
    QString path;
    QDateTime expirationDate;
    bool secure = false;
    bool httpOnly = false;
    bool hostOnly = true;
};

QCCookie::QCCookie()
    : d(new QCCookieData)
{
}

QCCookie::QCCookie(const QByteArray &name, const QByteArray &value)
    : d(new QCCookieData)
{
    d->name = name;
    d->value = value;
}

QCCookie::QCCookie(const QCCookie &other) = default;

QCCookie::QCCookie(QCCookie &&other) noexcept = default;

QCCookie::~QCCookie() = default;

QCCookie &QCCookie::operator=(const QCCookie &other) = default;

QCCookie &QCCookie::operator=(QCCookie &&other) noexcept = default;

QByteArray QCCookie::name() const
{
    return d->name;
}

void QCCookie::setName(const QByteArray &name)
{
    d->name = name;
}

QByteArray QCCookie::value() const
{
    return d->value;
}

void QCCookie::setValue(const QByteArray &value)
{
    d->value = value;
}

QString QCCookie::domain() const
{
    return d->domain;
}

void QCCookie::setDomain(const QString &domain)
{
    d->domain = domain;
}

QString QCCookie::path() const
{
    return d->path;
}

void QCCookie::setPath(const QString &path)
{
    d->path = path;
}

QDateTime QCCookie::expirationDate() const
{
    return d->expirationDate;
}

void QCCookie::setExpirationDate(const QDateTime &expirationDate)
{
    d->expirationDate = expirationDate;
}

bool QCCookie::isSecure() const noexcept
{
    return d->secure;
}

void QCCookie::setSecure(bool secure) noexcept
{
    d->secure = secure;
}

bool QCCookie::isHttpOnly() const noexcept
{
    return d->httpOnly;
}

void QCCookie::setHttpOnly(bool httpOnly) noexcept
{
    d->httpOnly = httpOnly;
}

bool QCCookie::isHostOnly() const noexcept
{
    return d->hostOnly;
}

void QCCookie::setHostOnly(bool hostOnly) noexcept
{
    d->hostOnly = hostOnly;
}

} // namespace QCurl
