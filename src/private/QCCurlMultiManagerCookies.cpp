#include "QCCurlMultiManager.h"

#include "QCNetworkAccessManager.h"

#include <QDateTime>
#include <QNetworkCookie>
#include <QMutexLocker>
#include <QTimeZone>

#include <ctime>
#include <memory>

namespace QCurl {

namespace {

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

std::optional<QNetworkCookie> parseCurlCookieLine(const QByteArray &line)
{
    if (line.isEmpty()) {
        return std::nullopt;
    }

    const QList<QByteArray> parts = line.split('\t');
    if (parts.size() < 7) {
        return std::nullopt;
    }

    QByteArray domainBytes                  = parts.at(0);
    const QByteArray includeSubdomainsBytes = parts.at(1);
    const bool includeSubdomains            = includeSubdomainsBytes.trimmed().toUpper() == "TRUE";
    bool httpOnly                           = false;
    static const QByteArray kHttpOnlyPrefix = "#HttpOnly_";
    if (domainBytes.startsWith(kHttpOnlyPrefix)) {
        httpOnly    = true;
        domainBytes = domainBytes.mid(kHttpOnlyPrefix.size());
    }

    const QByteArray pathBytes    = parts.at(2);
    const QByteArray secureBytes  = parts.at(3);
    const QByteArray expiresBytes = parts.at(4);
    const QByteArray nameBytes    = parts.at(5);
    const QByteArray valueBytes   = parts.at(6);

    QNetworkCookie cookie(nameBytes, valueBytes);
    QString domain = QString::fromUtf8(domainBytes);
    if (includeSubdomains) {
        if (!domain.startsWith(QLatin1Char('.'))) {
            domain.prepend(QLatin1Char('.'));
        }
    } else {
        if (domain.startsWith(QLatin1Char('.'))) {
            domain.remove(0, 1);
        }
    }
    cookie.setDomain(domain);
    cookie.setPath(QString::fromUtf8(pathBytes));
    cookie.setSecure(secureBytes.trimmed().toUpper() == "TRUE");
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
                                                 const QList<QNetworkCookie> &cookies,
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

    CURL *easy = curl_easy_init();
    if (!easy) {
        if (error) {
            *error = QStringLiteral("curl_easy_init 失败");
        }
        return false;
    }

    std::unique_ptr<CURL, decltype(&curl_easy_cleanup)> easyGuard(easy, &curl_easy_cleanup);
    curl_easy_setopt(easy, CURLOPT_SHARE, context->share);
    curl_easy_setopt(easy, CURLOPT_COOKIEFILE, "");

    for (const QNetworkCookie &raw : cookies) {
        QNetworkCookie c = raw;
        if (c.domain().isEmpty() && !originUrl.host().isEmpty()) {
            c.setDomain(originUrl.host());
        }
        if (c.path().isEmpty()) {
            c.setPath(QStringLiteral("/"));
        }

        if (c.domain().isEmpty()) {
            continue;
        }

        const QByteArray domainBytes = c.domain().toUtf8();
        const QByteArray pathBytes   = c.path().toUtf8();

        const bool includeSubdomains            = domainBytes.startsWith('.');
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

std::optional<QList<QNetworkCookie>> QCCurlMultiManager::exportCookiesForManager(
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

    CURL *easy = curl_easy_init();
    if (!easy) {
        if (error) {
            *error = QStringLiteral("curl_easy_init 失败");
        }
        return std::nullopt;
    }

    std::unique_ptr<CURL, decltype(&curl_easy_cleanup)> easyGuard(easy, &curl_easy_cleanup);
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

    QList<QNetworkCookie> out;
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

    CURL *easy = curl_easy_init();
    if (!easy) {
        if (error) {
            *error = QStringLiteral("curl_easy_init 失败");
        }
        return false;
    }

    std::unique_ptr<CURL, decltype(&curl_easy_cleanup)> easyGuard(easy, &curl_easy_cleanup);
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
