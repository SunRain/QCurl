#include "QCNetworkCache.h"
#include <QStringList>

namespace QCurl {

QDateTime QCNetworkCache::parseExpirationDate(const QMap<QByteArray, QByteArray> &headers)
{
    // 1. 检查 Cache-Control: max-age
    if (headers.contains("cache-control") || headers.contains("Cache-Control")) {
        QByteArray cacheControl = headers.value("cache-control");
        if (cacheControl.isEmpty()) {
            cacheControl = headers.value("Cache-Control");
        }

        // 解析 max-age=秒数
        QString ccStr = QString::fromLatin1(cacheControl);
        QStringList directives = ccStr.split(',', Qt::SkipEmptyParts);
        for (const QString &directive : directives) {
            QString trimmed = directive.trimmed();
            if (trimmed.startsWith("max-age=", Qt::CaseInsensitive)) {
                bool ok = false;
                int maxAge = trimmed.mid(8).toInt(&ok);
                if (ok && maxAge > 0) {
                    return QDateTime::currentDateTime().addSecs(maxAge);
                }
            }
        }
    }

    // 2. 检查 Expires 头
    if (headers.contains("expires") || headers.contains("Expires")) {
        QByteArray expires = headers.value("expires");
        if (expires.isEmpty()) {
            expires = headers.value("Expires");
        }

        // 解析 HTTP 日期格式（RFC 2616）
        QDateTime expiresDate = QDateTime::fromString(
            QString::fromLatin1(expires), Qt::RFC2822Date);
        if (expiresDate.isValid()) {
            return expiresDate;
        }
    }

    // 3. 默认：无过期时间（永久缓存）
    return QDateTime();
}

bool QCNetworkCache::isCacheable(const QMap<QByteArray, QByteArray> &headers)
{
    // 检查 Cache-Control: no-store 或 no-cache
    if (headers.contains("cache-control") || headers.contains("Cache-Control")) {
        QByteArray cacheControl = headers.value("cache-control");
        if (cacheControl.isEmpty()) {
            cacheControl = headers.value("Cache-Control");
        }

        QString ccStr = QString::fromLatin1(cacheControl).toLower();
        if (ccStr.contains("no-store") || ccStr.contains("no-cache")) {
            return false;
        }
    }

    // 检查 Pragma: no-cache（HTTP/1.0 兼容）
    if (headers.contains("pragma") || headers.contains("Pragma")) {
        QByteArray pragma = headers.value("pragma");
        if (pragma.isEmpty()) {
            pragma = headers.value("Pragma");
        }

        if (QString::fromLatin1(pragma).toLower().contains("no-cache")) {
            return false;
        }
    }

    return true;
}

} // namespace QCurl
