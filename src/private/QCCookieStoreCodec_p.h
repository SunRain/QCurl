/**
 * @file
 * @brief 声明 libcurl Cookie list 的校验与编码辅助函数。
 */

#ifndef QCCOOKIESTORECODEC_P_H
#define QCCOOKIESTORECODEC_P_H

#include "QCCookie.h"
#include "QCCookieStoreResult_p.h"

#include <QByteArray>
#include <QList>
#include <QUrl>

#include <optional>

namespace QCurl::Internal {

[[nodiscard]] CookieStoreResult prepareCookieImport(const QList<QCCookie> &cookies,
                                                    const QUrl &originUrl,
                                                    QList<QByteArray> *lines);
[[nodiscard]] std::optional<QCCookie> parseCurlCookieLine(const QByteArray &line);
[[nodiscard]] bool cookieMatchesUrl(const QCCookie &cookie, const QUrl &filterUrl);

} // namespace QCurl::Internal

#endif // QCCOOKIESTORECODEC_P_H
