/**
 * @file
 * @brief 声明 cookie async bridge 的值结果。
 */

#ifndef QCCOOKIEASYNCRESULT_H
#define QCCOOKIEASYNCRESULT_H

#include "QCCookie.h"
#include "QCGlobal.h"

#include <QList>
#include <QSharedDataPointer>
#include <QString>

namespace QCurl {

class QCCookieExportResultData;
class QCCookieOperationResultData;

/** 异步 manager cookie 修改操作的结果值类型。 */
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
    [[nodiscard]] static QCCookieOperationResult failure(const QString &error);

    [[nodiscard]] bool isSuccess() const noexcept;
    [[nodiscard]] QString error() const;

private:
    QSharedDataPointer<QCCookieOperationResultData> d;
};

/** 异步 manager cookie 导出操作的结果值类型。 */
class QCURL_EXPORT QCCookieExportResult
{
public:
    QCCookieExportResult();
    QCCookieExportResult(const QCCookieExportResult &other);
    QCCookieExportResult(QCCookieExportResult &&other) noexcept;
    ~QCCookieExportResult();

    QCCookieExportResult &operator=(const QCCookieExportResult &other);
    QCCookieExportResult &operator=(QCCookieExportResult &&other) noexcept;

    [[nodiscard]] static QCCookieExportResult success(const QList<QCCookie> &cookies);
    [[nodiscard]] static QCCookieExportResult failure(const QString &error);

    [[nodiscard]] bool isSuccess() const noexcept;
    [[nodiscard]] QList<QCCookie> cookies() const;
    [[nodiscard]] QString error() const;

private:
    QSharedDataPointer<QCCookieExportResultData> d;
};

} // namespace QCurl

Q_DECLARE_METATYPE(QCurl::QCCookieOperationResult)
Q_DECLARE_METATYPE(QCurl::QCCookieExportResult)

#endif // QCCOOKIEASYNCRESULT_H
