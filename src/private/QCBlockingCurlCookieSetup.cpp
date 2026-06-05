/**
 * @file
 * @brief 实现 Blocking Extras 的 cookie 请求与响应转换辅助函数。
 */

#include "private/QCBlockingCurlRequestSetup_p.h"

#include <QDateTime>
#include <QList>
#include <QString>
#include <QTimeZone>

#include <optional>
#include <utility>

namespace QCurl::Internal {
namespace {

std::pair<QByteArray, QByteArray> splitCookieAttribute(const QByteArray &attribute)
{
    const int equals = attribute.indexOf('=');
    const QByteArray name =
        (equals >= 0 ? attribute.left(equals) : attribute).trimmed().toLower();
    const QByteArray value = equals >= 0 ? attribute.mid(equals + 1).trimmed() : QByteArray();
    return {name, value};
}

bool setCookieMaxAge(QCCookie *cookie, const QByteArray &value)
{
    bool ok = false;
    const qint64 seconds = value.toLongLong(&ok);
    if (!ok) {
        return false;
    }

    if (seconds <= 0) {
        cookie->setExpirationDate(QDateTime::fromSecsSinceEpoch(0, QTimeZone::utc()));
        return true;
    }

    cookie->setExpirationDate(QDateTime::currentDateTimeUtc().addSecs(seconds));
    return true;
}

QDateTime parseCookieExpiresDate(const QByteArray &value)
{
    const QString text = QString::fromUtf8(value).trimmed();
    QDateTime expires = QDateTime::fromString(text, Qt::RFC2822Date);
    if (expires.isValid()) {
        return expires.toTimeZone(QTimeZone::utc());
    }

    if (!text.endsWith(QStringLiteral(" GMT"), Qt::CaseInsensitive)) {
        return {};
    }

    QString normalized = text;
    normalized.chop(4);
    normalized += QStringLiteral(" +0000");
    expires = QDateTime::fromString(normalized, Qt::RFC2822Date);
    return expires.isValid() ? expires.toTimeZone(QTimeZone::utc()) : QDateTime();
}

void applyCookieAttribute(QCCookie *cookie, const QByteArray &attribute, bool *maxAgeSeen)
{
    const auto [name, value] = splitCookieAttribute(attribute.trimmed());
    if (name == "domain") {
        cookie->setDomain(QString::fromUtf8(value));
        cookie->setHostOnly(false);
    } else if (name == "path") {
        cookie->setPath(QString::fromUtf8(value));
    } else if (name == "secure") {
        cookie->setSecure(true);
    } else if (name == "httponly") {
        cookie->setHttpOnly(true);
    } else if (name == "expires") {
        if (maxAgeSeen && *maxAgeSeen) {
            return;
        }
        const QDateTime expires = parseCookieExpiresDate(value);
        if (expires.isValid()) {
            cookie->setExpirationDate(expires);
        }
    } else if (name == "max-age") {
        if (setCookieMaxAge(cookie, value) && maxAgeSeen) {
            *maxAgeSeen = true;
        }
    }
}

std::optional<QCCookie> parseSetCookieHeader(const QByteArray &header)
{
    const QList<QByteArray> parts = header.split(';');
    if (parts.isEmpty()) {
        return std::nullopt;
    }

    const QByteArray nameValue = parts.first().trimmed();
    const int equals = nameValue.indexOf('=');
    if (equals <= 0) {
        return std::nullopt;
    }

    QCCookie cookie(nameValue.left(equals), nameValue.mid(equals + 1));
    bool maxAgeSeen = false;
    for (int i = 1; i < parts.size(); ++i) {
        applyCookieAttribute(&cookie, parts.at(i), &maxAgeSeen);
    }

    return cookie;
}

} // namespace

QByteArray cookieHeaderValue(const QCCookieSnapshot &snapshot)
{
    QList<QByteArray> pairs;
    for (const QCCookie &cookie : snapshot.cookies()) {
        if (!cookie.name().isEmpty()) {
            pairs.append(cookie.name() + QByteArrayLiteral("=") + cookie.value());
        }
    }
    return pairs.join("; ");
}

QCCookieDelta extractCookieDelta(const QCBlockingNetworkResult::HeaderList &headers)
{
    QList<QCCookie> cookies;
    for (const auto &header : headers) {
        if (header.first.compare(QByteArrayLiteral("Set-Cookie"), Qt::CaseInsensitive) != 0) {
            continue;
        }
        auto cookie = parseSetCookieHeader(header.second);
        if (cookie.has_value()) {
            cookies.append(cookie.value());
        }
    }
    return QCCookieDelta(cookies);
}

} // namespace QCurl::Internal
