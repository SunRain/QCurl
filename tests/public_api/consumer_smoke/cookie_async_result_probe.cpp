#include "contract_probes.h"

#include <QCCookie.h>
#include <QCCookieAsyncResult.h>
#include <QList>
#include <QString>

int runCookieAsyncResultProbe()
{
    const auto cookieImportSuccess = QCurl::QCCookieOperationResult::success();
    const QCurl::QCCookieOperationResult defaultFailure;
    const auto cookieImportFailure
        = QCurl::QCCookieOperationResult::failure(QCurl::QCCookieAsyncError::BusinessError,
                                                  QStringLiteral("consumer cookie failure"),
                                                  QStringLiteral("CookieImportRejected"));
    QCurl::QCCookie exportedCookie;
    exportedCookie.setName(QByteArrayLiteral("session"));
    exportedCookie.setValue(QByteArrayLiteral("value"));
    QList<QCurl::QCCookie> exportedCookies{exportedCookie};
    const auto cookieExportSuccess = QCurl::QCCookieExportResult::success(exportedCookies);
    const auto cookieExportFailure
        = QCurl::QCCookieExportResult::failure(QCurl::QCCookieAsyncError::ManagerDestroyed,
                                               QStringLiteral("consumer export failure"));

    if (!cookieImportSuccess.isSuccess()
        || cookieImportSuccess.errorCode() != QCurl::QCCookieAsyncError::None
        || defaultFailure.isSuccess()
        || defaultFailure.errorCode() != QCurl::QCCookieAsyncError::BusinessError
        || cookieImportFailure.isSuccess()
        || cookieImportFailure.error() != QStringLiteral("consumer cookie failure")
        || cookieImportFailure.errorCode() != QCurl::QCCookieAsyncError::BusinessError
        || cookieImportFailure.policyCode() != QStringLiteral("CookieImportRejected")
        || !cookieExportSuccess.isSuccess() || cookieExportSuccess.cookies().size() != 1
        || cookieExportSuccess.cookies().first().name() != QByteArrayLiteral("session")
        || cookieExportFailure.isSuccess()
        || cookieExportFailure.errorCode() != QCurl::QCCookieAsyncError::ManagerDestroyed
        || cookieExportFailure.error() != QStringLiteral("consumer export failure")) {
        return 26;
    }

    return 0;
}
