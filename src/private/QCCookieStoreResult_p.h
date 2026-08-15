/**
 * @file
 * @brief 声明共享 cookie store 操作的内部事务结果。
 */

#ifndef QCCOOKIESTORERESULT_P_H
#define QCCOOKIESTORERESULT_P_H

#include "QCCookie.h"

#include <QByteArray>
#include <QList>
#include <QString>

#include <curl/curl.h>

namespace QCurl::Internal {

enum class CookieStoreStatus {
    Applied,
    RejectedBeforeMutation,
    RolledBack,
    PersistenceFailed,
    StorePoisoned,
};

/** Cookie 同步入口、日志与 public async bridge 共用的结果。 */
struct CookieStoreResult
{
    CookieStoreStatus status = CookieStoreStatus::RejectedBeforeMutation;
    CURLcode curlCode        = CURLE_OK;
    CURLSHcode shareCode     = CURLSHE_OK;
    QByteArray optionName;
    QString policyCode;
    QString message;
    QList<QCCookie> cookies;

    [[nodiscard]] bool isSuccess() const noexcept { return status == CookieStoreStatus::Applied; }
};

} // namespace QCurl::Internal

#endif // QCCOOKIESTORERESULT_P_H
