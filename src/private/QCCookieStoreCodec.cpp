#include "QCCookieStoreCodec_p.h"
#include "private/QCCookiePolicyCode_p.h"

#include <QByteArrayView>
#include <QDateTime>
#include <QTimeZone>

namespace QCurl::Internal {

namespace {

constexpr QByteArrayView kHttpOnlyPrefix{"#HttpOnly_"};

bool hasFieldDelimiter(const QByteArray &value)
{
    return value.contains('\t') || value.contains('\r') || value.contains('\n')
           || value.contains('\0');
}

QString normalizedDomain(QString domain)
{
    if (domain.startsWith(QLatin1Char('.'))) {
        domain.remove(0, 1);
    }
    return domain;
}

bool domainAcceptsHost(const QString &domain, bool hostOnly, const QString &host)
{
    const QString normalized = normalizedDomain(domain);
    if (normalized.isEmpty() || host.isEmpty()) {
        return false;
    }
    if (host.compare(normalized, Qt::CaseInsensitive) == 0) {
        return true;
    }
    return !hostOnly && host.size() > normalized.size()
           && host.endsWith(normalized, Qt::CaseInsensitive)
           && host.at(host.size() - normalized.size() - 1) == QLatin1Char('.');
}

bool pathMatches(const QString &cookiePath, const QString &urlPath)
{
    const QString path = cookiePath.isEmpty() ? QStringLiteral("/") : cookiePath;
    const QString url  = urlPath.isEmpty() ? QStringLiteral("/") : urlPath;
    if (!url.startsWith(path)) {
        return false;
    }
    return url.size() == path.size() || path.endsWith(QLatin1Char('/'))
           || url.at(path.size()) == QLatin1Char('/');
}

CookieStoreResult invalidCookie(qsizetype index)
{
    CookieStoreResult result;
    result.status     = CookieStoreStatus::RejectedBeforeMutation;
    result.policyCode = QCurl::Internal::cookiepolicy::kInvalidInput;
    result.message    = QStringLiteral("cookie 输入校验失败（索引 %1）").arg(index);
    return result;
}

QByteArray encodeCookie(const QCCookie &cookie)
{
    const QByteArray domain                 = cookie.domain().toUtf8();
    const QByteArray path                   = cookie.path().toUtf8();
    const bool includeSubdomains            = !cookie.isHostOnly() || domain.startsWith('.');
    const QByteArray includeSubdomainsValue = includeSubdomains ? QByteArrayLiteral("TRUE")
                                                                : QByteArrayLiteral("FALSE");
    const QByteArray secure                 = cookie.isSecure() ? QByteArrayLiteral("TRUE")
                                                                : QByteArrayLiteral("FALSE");
    qint64 expires = cookie.expirationDate().isValid() ? cookie.expirationDate().toSecsSinceEpoch()
                                                       : 0;
    expires        = qMax<qint64>(expires, 0);
    const QByteArray encodedDomain = cookie.isHttpOnly() ? QByteArray(kHttpOnlyPrefix) + domain
                                                         : domain;
    return encodedDomain + '\t' + includeSubdomainsValue + '\t' + path + '\t' + secure + '\t'
           + QByteArray::number(expires) + '\t' + cookie.name() + '\t' + cookie.value();
}

} // namespace

CookieStoreResult prepareCookieImport(const QList<QCCookie> &cookies,
                                      const QUrl &originUrl,
                                      QList<QByteArray> *lines)
{
    Q_ASSERT(lines);
    lines->clear();
    lines->reserve(cookies.size());
    for (qsizetype index = 0; index < cookies.size(); ++index) {
        QCCookie cookie = cookies.at(index);
        if (cookie.domain().isEmpty() && !originUrl.host().isEmpty()) {
            cookie.setDomain(originUrl.host());
            cookie.setHostOnly(true);
        }
        if (cookie.path().isEmpty()) {
            cookie.setPath(QStringLiteral("/"));
        }

        const QByteArray domain = cookie.domain().toUtf8();
        const QByteArray path   = cookie.path().toUtf8();
        if (cookie.name().isEmpty() || domain.isEmpty() || domain.startsWith('#')
            || !cookie.path().startsWith(QLatin1Char('/')) || hasFieldDelimiter(cookie.name())
            || hasFieldDelimiter(cookie.value()) || hasFieldDelimiter(domain)
            || hasFieldDelimiter(path)
            || (!originUrl.host().isEmpty()
                && !domainAcceptsHost(cookie.domain(), cookie.isHostOnly(), originUrl.host()))) {
            return invalidCookie(index);
        }
        lines->append(encodeCookie(cookie));
    }

    CookieStoreResult result;
    result.status     = CookieStoreStatus::Applied;
    result.policyCode = QCurl::Internal::cookiepolicy::kValidated;
    return result;
}

std::optional<QCCookie> parseCurlCookieLine(const QByteArray &line)
{
    const QList<QByteArray> parts = line.split('\t');
    if (parts.size() < 7) {
        return std::nullopt;
    }

    QByteArray domain = parts.at(0);
    bool httpOnly     = false;
    if (domain.startsWith(kHttpOnlyPrefix)) {
        httpOnly = true;
        domain   = domain.mid(kHttpOnlyPrefix.size());
    }
    const bool includeSubdomains = parts.at(1).trimmed().toUpper() == "TRUE";
    QCCookie cookie(parts.at(5), parts.at(6));
    QString domainText = QString::fromUtf8(domain);
    if (includeSubdomains) {
        if (!domainText.startsWith(QLatin1Char('.'))) {
            domainText.prepend(QLatin1Char('.'));
        }
        cookie.setHostOnly(false);
    } else {
        cookie.setDomain(normalizedDomain(domainText));
        cookie.setHostOnly(true);
    }
    if (includeSubdomains) {
        cookie.setDomain(domainText);
    }
    cookie.setPath(QString::fromUtf8(parts.at(2)));
    cookie.setSecure(parts.at(3).trimmed().toUpper() == "TRUE");
    cookie.setHttpOnly(httpOnly);

    bool ok            = false;
    const qint64 epoch = parts.at(4).trimmed().toLongLong(&ok);
    if (ok && epoch > 0) {
        cookie.setExpirationDate(QDateTime::fromSecsSinceEpoch(epoch, QTimeZone::utc()));
    }
    return cookie;
}

bool cookieMatchesUrl(const QCCookie &cookie, const QUrl &filterUrl)
{
    if (filterUrl.host().isEmpty()) {
        return true;
    }
    return domainAcceptsHost(cookie.domain(), cookie.isHostOnly(), filterUrl.host())
           && pathMatches(cookie.path(), filterUrl.path());
}

} // namespace QCurl::Internal
