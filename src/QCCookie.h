/**
 * @file
 * @brief 声明 QCurl Core cookie 值类型。
 */

#ifndef QCCOOKIE_H
#define QCCOOKIE_H

#include "QCGlobal.h"

#include <QByteArray>
#include <QDateTime>
#include <QMetaType>
#include <QSharedDataPointer>
#include <QString>

namespace QCurl {

class QCCookieData;

/** QCurl 自有 cookie 数据模型，不暴露 QtNetwork 或 libcurl 表示。 */
class QCURL_EXPORT QCCookie
{
public:
    QCCookie();
    QCCookie(const QByteArray &name, const QByteArray &value);
    QCCookie(const QCCookie &other);
    QCCookie(QCCookie &&other) noexcept;
    ~QCCookie();

    QCCookie &operator=(const QCCookie &other);
    QCCookie &operator=(QCCookie &&other) noexcept;

    [[nodiscard]] QByteArray name() const;
    void setName(const QByteArray &name);

    [[nodiscard]] QByteArray value() const;
    void setValue(const QByteArray &value);

    [[nodiscard]] QString domain() const;
    void setDomain(const QString &domain);

    [[nodiscard]] QString path() const;
    void setPath(const QString &path);

    [[nodiscard]] QDateTime expirationDate() const;
    void setExpirationDate(const QDateTime &expirationDate);

    [[nodiscard]] bool isSecure() const noexcept;
    void setSecure(bool secure) noexcept;

    [[nodiscard]] bool isHttpOnly() const noexcept;
    void setHttpOnly(bool httpOnly) noexcept;

    [[nodiscard]] bool isHostOnly() const noexcept;
    void setHostOnly(bool hostOnly) noexcept;

private:
    QSharedDataPointer<QCCookieData> d;
};

} // namespace QCurl

Q_DECLARE_METATYPE(QCurl::QCCookie)

#endif // QCCOOKIE_H
