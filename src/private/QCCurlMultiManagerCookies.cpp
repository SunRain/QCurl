#include "QCCurlMultiManager.h"
#include "QCCurlHandleManager.h"
#include "QCNetworkAccessManager.h"

#include <QByteArrayView>
#include <QDateTime>
#include <QMutexLocker>
#include <QTimeZone>

#include <ctime>
namespace QCurl {

namespace {

enum class NetscapeCookieField : qsizetype {
    Domain            = 0,
    IncludeSubdomains = 1,
    Path              = 2,
    Secure            = 3,
    Expires           = 4,
    Name              = 5,
    Value             = 6,
    Count             = 7,
};

constexpr QByteArrayView kHttpOnlyPrefix{"#HttpOnly_"};
constexpr QByteArrayView kCookieJarTrueValue{"TRUE"};

[[nodiscard]] qsizetype cookieFieldIndex(NetscapeCookieField field)
{
    return static_cast<qsizetype>(field);
}

bool isCapabilityRelatedCurlError(CURLcode code)
{
    return code == CURLE_UNKNOWN_OPTION || code == CURLE_NOT_BUILT_IN;
}

bool domainMatchesHost(const QString &cookieDomain, const QString &host)
{
    if (cookieDomain.isEmpty() || host.isEmpty()) {
        return false;
    }

    QString normalized = cookieDomain;
    if (normalized.startsWith(QStringLiteral("#HttpOnly_"))) {
        normalized = normalized.mid(QStringLiteral("#HttpOnly_").size());
    }
    const bool includeSubdomains = normalized.startsWith(QLatin1Char('.'));
    if (includeSubdomains) {
        normalized = normalized.mid(1);
    }

    if (normalized.isEmpty()) {
        return false;
    }

    if (host.compare(normalized, Qt::CaseInsensitive) == 0) {
        return true;
    }

    if (!includeSubdomains) {
        return false;
    }

    if (host.size() <= normalized.size()) {
        return false;
    }

    if (!host.endsWith(normalized, Qt::CaseInsensitive)) {
        return false;
    }

    const int idx = host.size() - normalized.size() - 1;
    return idx >= 0 && host.at(idx) == QLatin1Char('.');
}

bool pathMatchesUrl(const QString &cookiePath, const QString &urlPath)
{
    const QString p = cookiePath.isEmpty() ? QStringLiteral("/") : cookiePath;
    const QString u = urlPath.isEmpty() ? QStringLiteral("/") : urlPath;
    if (!u.startsWith(p)) {
        return false;
    }

    if (u.size() == p.size()) {
        return true;
    }

    if (p.endsWith(QLatin1Char('/'))) {
        return true;
    }

    return u.at(p.size()) == QLatin1Char('/');
}

std::optional<QCCookie> parseCurlCookieLine(const QByteArray &line)
{
    if (line.isEmpty()) {
        return std::nullopt;
    }

    const QList<QByteArray> parts = line.split('\t');
    if (parts.size() < cookieFieldIndex(NetscapeCookieField::Count)) {
        return std::nullopt;
    }

    QByteArray domainBytes = parts.at(cookieFieldIndex(NetscapeCookieField::Domain));
    const QByteArray includeSubdomainsBytes = parts.at(
        cookieFieldIndex(NetscapeCookieField::IncludeSubdomains));
    const bool includeSubdomains = includeSubdomainsBytes.trimmed().toUpper()
                                   == kCookieJarTrueValue;
    bool httpOnly                = false;
    if (domainBytes.startsWith(kHttpOnlyPrefix)) {
        httpOnly    = true;
        domainBytes = domainBytes.mid(kHttpOnlyPrefix.size());
    }

    const QByteArray pathBytes    = parts.at(cookieFieldIndex(NetscapeCookieField::Path));
    const QByteArray secureBytes  = parts.at(cookieFieldIndex(NetscapeCookieField::Secure));
    const QByteArray expiresBytes = parts.at(cookieFieldIndex(NetscapeCookieField::Expires));
    const QByteArray nameBytes    = parts.at(cookieFieldIndex(NetscapeCookieField::Name));
    const QByteArray valueBytes   = parts.at(cookieFieldIndex(NetscapeCookieField::Value));

    QCCookie cookie(nameBytes, valueBytes);
    QString domain = QString::fromUtf8(domainBytes);
    if (includeSubdomains) {
        if (!domain.startsWith(QLatin1Char('.'))) {
            domain.prepend(QLatin1Char('.'));
        }
        cookie.setHostOnly(false);
    } else {
        if (domain.startsWith(QLatin1Char('.'))) {
            domain.remove(0, 1);
        }
        cookie.setHostOnly(true);
    }
    cookie.setDomain(domain);
    cookie.setPath(QString::fromUtf8(pathBytes));
    cookie.setSecure(secureBytes.trimmed().toUpper() == kCookieJarTrueValue);
    cookie.setHttpOnly(httpOnly);

    bool ok            = false;
    const qint64 epoch = expiresBytes.trimmed().toLongLong(&ok);
    if (ok && epoch > 0) {
        cookie.setExpirationDate(QDateTime::fromSecsSinceEpoch(epoch, QTimeZone::utc()));
    }
    return cookie;
}

} // namespace

bool QCCurlMultiManager::importCookiesForManager(const QCNetworkAccessManager *manager,
                                                 const QList<QCCookie> &cookies,
                                                 const QUrl &originUrl,
                                                 QString *error)
{
    if (!manager) {
        if (error) {
            *error = QStringLiteral("manager 为空");
        }
        return false;
    }

    const ShareConfig desired = toShareConfig(manager);
    if (!desired.cookies) {
        if (error) {
            *error = QStringLiteral("importCookies 需要启用 ShareHandleConfig.shareCookies");
        }
        return false;
    }

    QMutexLocker locker(&m_mutex);
    ShareContext *context = prepareCookieContextLocked(manager, desired, error);
    if (!context) {
        return false;
    }

    QCCurlHandleManager easyHandle;
    CURL *easy = easyHandle.handle();
    if (!easy) {
        if (error) {
            *error = QStringLiteral("curl_easy_init 失败");
        }
        return false;
    }

    curl_easy_setopt(easy, CURLOPT_SHARE, context->share);
    curl_easy_setopt(easy, CURLOPT_COOKIEFILE, "");

    for (const QCCookie &raw : cookies) {
        QCCookie c = raw;
        if (c.domain().isEmpty() && !originUrl.host().isEmpty()) {
            c.setDomain(originUrl.host());
            c.setHostOnly(true);
        }
        if (c.path().isEmpty()) {
            c.setPath(QStringLiteral("/"));
        }

        if (c.domain().isEmpty()) {
            continue;
        }

        const QByteArray domainBytes = c.domain().toUtf8();
        const QByteArray pathBytes   = c.path().toUtf8();

        const bool includeSubdomains            = !c.isHostOnly() || domainBytes.startsWith('.');
        const QByteArray includeSubdomainsBytes = includeSubdomains ? QByteArray("TRUE")
                                                                    : QByteArray("FALSE");
        const QByteArray secureBytes = c.isSecure() ? QByteArray("TRUE") : QByteArray("FALSE");

        qint64 expiresEpoch = 0;
        if (c.expirationDate().isValid()) {
            expiresEpoch = c.expirationDate().toSecsSinceEpoch();
            if (expiresEpoch < 0) {
                expiresEpoch = 0;
            }
        }

        QByteArray cookieLineDomain = domainBytes;
        if (c.isHttpOnly()) {
            cookieLineDomain = QByteArray("#HttpOnly_") + cookieLineDomain;
        }

        const QByteArray cookieLine = cookieLineDomain + '\t' + includeSubdomainsBytes + '\t'
                                      + pathBytes + '\t' + secureBytes + '\t'
                                      + QByteArray::number(expiresEpoch) + '\t' + c.name() + '\t'
                                      + c.value();

        const CURLcode rc = curl_easy_setopt(easy, CURLOPT_COOKIELIST, cookieLine.constData());
        if (rc != CURLE_OK) {
            if (error) {
                *error = QStringLiteral("导入 cookie 失败（%1）")
                             .arg(QString::fromUtf8(curl_easy_strerror(rc)));
            }
            return false;
        }
    }

    static_cast<void>(curl_easy_setopt(easy, CURLOPT_COOKIELIST, "FLUSH"));
    return true;
}

std::optional<QList<QCCookie>> QCCurlMultiManager::exportCookiesForManager(
    const QCNetworkAccessManager *manager, const QUrl &filterUrl, QString *error)
{
    if (!manager) {
        if (error) {
            *error = QStringLiteral("manager 为空");
        }
        return std::nullopt;
    }

    const ShareConfig desired = toShareConfig(manager);
    if (!desired.cookies) {
        if (error) {
            *error = QStringLiteral("exportCookies 需要启用 ShareHandleConfig.shareCookies");
        }
        return std::nullopt;
    }

    QMutexLocker locker(&m_mutex);
    ShareContext *context = prepareCookieContextLocked(manager, desired, error);
    if (!context) {
        return std::nullopt;
    }

    QCCurlHandleManager easyHandle;
    CURL *easy = easyHandle.handle();
    if (!easy) {
        if (error) {
            *error = QStringLiteral("curl_easy_init 失败");
        }
        return std::nullopt;
    }

    curl_easy_setopt(easy, CURLOPT_SHARE, context->share);
    curl_easy_setopt(easy, CURLOPT_COOKIEFILE, "");

    struct curl_slist *cookieList = nullptr;
    const CURLcode getInfoRc      = curl_easy_getinfo(easy, CURLINFO_COOKIELIST, &cookieList);
    if (getInfoRc != CURLE_OK) {
        if (error) {
            *error = QStringLiteral("读取 cookie 列表失败（%1）")
                         .arg(QString::fromUtf8(curl_easy_strerror(getInfoRc)));
        }
        return std::nullopt;
    }

    QList<QCCookie> out;
    const QString host    = filterUrl.host();
    const QString urlPath = filterUrl.path();
    for (auto *it = cookieList; it; it = it->next) {
        if (!it->data) {
            continue;
        }
        const QByteArray line = QByteArray(it->data);
        auto parsed           = parseCurlCookieLine(line);
        if (!parsed.has_value()) {
            continue;
        }
        if (!host.isEmpty()) {
            if (!domainMatchesHost(parsed->domain(), host)) {
                continue;
            }
            if (!pathMatchesUrl(parsed->path(), urlPath)) {
                continue;
            }
        }
        out.append(*parsed);
    }

    curl_slist_free_all(cookieList);
    return out;
}

bool QCCurlMultiManager::clearAllCookiesForManager(const QCNetworkAccessManager *manager,
                                                   QString *error)
{
    if (!manager) {
        if (error) {
            *error = QStringLiteral("manager 为空");
        }
        return false;
    }

    const ShareConfig desired = toShareConfig(manager);
    if (!desired.cookies) {
        if (error) {
            *error = QStringLiteral("clearAllCookies 需要启用 ShareHandleConfig.shareCookies");
        }
        return false;
    }

    QMutexLocker locker(&m_mutex);
    ShareContext *context = prepareCookieContextLocked(manager, desired, error);
    if (!context) {
        return false;
    }

    QCCurlHandleManager easyHandle;
    CURL *easy = easyHandle.handle();
    if (!easy) {
        if (error) {
            *error = QStringLiteral("curl_easy_init 失败");
        }
        return false;
    }

    curl_easy_setopt(easy, CURLOPT_SHARE, context->share);
    curl_easy_setopt(easy, CURLOPT_COOKIEFILE, "");

    const CURLcode rc = curl_easy_setopt(easy, CURLOPT_COOKIELIST, "ALL");
    if (rc != CURLE_OK) {
        if (error) {
            if (isCapabilityRelatedCurlError(rc)) {
                *error = QStringLiteral("libcurl 不支持 CURLOPT_COOKIELIST（%1）")
                             .arg(QString::fromUtf8(curl_easy_strerror(rc)));
            } else {
                *error = QStringLiteral("清空 cookies 失败（%1）")
                             .arg(QString::fromUtf8(curl_easy_strerror(rc)));
            }
        }
        return false;
    }
    static_cast<void>(curl_easy_setopt(easy, CURLOPT_COOKIELIST, "FLUSH"));
    return true;
}

} // namespace QCurl
