#include "contract_probes.h"

#include <QCCookieAsyncResult.h>

#include <QList>
#include <QNetworkCookie>
#include <QString>

int runCookieAsyncResultProbe()
{
    const auto cookieImportSuccess = QCurl::QCCookieOperationResult::success();
    const auto cookieImportFailure = QCurl::QCCookieOperationResult::failure(
        QStringLiteral("consumer cookie failure"));
    QList<QNetworkCookie> exportedCookies{
        QNetworkCookie(QByteArrayLiteral("session"), QByteArrayLiteral("value"))};
    const auto cookieExportSuccess = QCurl::QCCookieExportResult::success(exportedCookies);
    const auto cookieExportFailure = QCurl::QCCookieExportResult::failure(
        QStringLiteral("consumer export failure"));

    if (!cookieImportSuccess.isSuccess() || cookieImportFailure.isSuccess()
        || cookieImportFailure.error() != QStringLiteral("consumer cookie failure")
        || !cookieExportSuccess.isSuccess() || cookieExportSuccess.cookies().size() != 1
        || cookieExportSuccess.cookies().first().name() != QByteArrayLiteral("session")
        || cookieExportFailure.isSuccess()
        || cookieExportFailure.error() != QStringLiteral("consumer export failure")) {
        return 26;
    }

    return 0;
}
