/**
 * @file
 * @brief 声明 cookie async bridge 的值结果。
 */

#ifndef QCCOOKIEASYNCRESULT_H
#define QCCOOKIEASYNCRESULT_H

#include "QCGlobal.h"

#include <QList>
#include <QNetworkCookie>
#include <QSharedDataPointer>
#include <QString>

namespace QCurl {

class QCCookieExportResultData;
class QCCookieOperationResultData;

class QCURL_EXPORT QCCookieOperationResult
{
public:
    QCCookieOperationResult();
    QCCookieOperationResult(const QCCookieOperationResult &other);
    QCCookieOperationResult(QCCookieOperationResult &&other) noexcept;
    ~QCCookieOperationResult();

    QCCookieOperationResult &operator=(const QCCookieOperationResult &other);
    QCCookieOperationResult &operator=(QCCookieOperationResult &&other) noexcept;

    [[nodiscard]] static QCCookieOperationResult success();
    [[nodiscard]] static QCCookieOperationResult failure(QString error);

    [[nodiscard]] bool isSuccess() const noexcept;
    [[nodiscard]] QString error() const;

private:
    QSharedDataPointer<QCCookieOperationResultData> d;
};

class QCURL_EXPORT QCCookieExportResult
{
public:
    QCCookieExportResult();
    QCCookieExportResult(const QCCookieExportResult &other);
    QCCookieExportResult(QCCookieExportResult &&other) noexcept;
    ~QCCookieExportResult();

    QCCookieExportResult &operator=(const QCCookieExportResult &other);
    QCCookieExportResult &operator=(QCCookieExportResult &&other) noexcept;

    [[nodiscard]] static QCCookieExportResult success(QList<QNetworkCookie> cookies);
    [[nodiscard]] static QCCookieExportResult failure(QString error);

    [[nodiscard]] bool isSuccess() const noexcept;
    [[nodiscard]] QList<QNetworkCookie> cookies() const;
    [[nodiscard]] QString error() const;

private:
    QSharedDataPointer<QCCookieExportResultData> d;
};

} // namespace QCurl

Q_DECLARE_METATYPE(QCurl::QCCookieOperationResult)
Q_DECLARE_METATYPE(QCurl::QCCookieExportResult)

#endif // QCCOOKIEASYNCRESULT_H
