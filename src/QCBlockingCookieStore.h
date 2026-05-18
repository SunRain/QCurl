/**
 * @file
 * @brief 声明 Blocking Extras 的 cookie 快照值类型。
 */

#ifndef QCBLOCKINGCOOKIESTORE_H
#define QCBLOCKINGCOOKIESTORE_H

#include "QCGlobal.h"

#include <QList>
#include <QNetworkCookie>
#include <QSharedDataPointer>

namespace QCurl {

class QCCookieDeltaData;
class QCCookieSnapshotData;

class QCURL_EXPORT QCCookieSnapshot
{
public:
    QCCookieSnapshot();
    explicit QCCookieSnapshot(QList<QNetworkCookie> cookies);
    QCCookieSnapshot(const QCCookieSnapshot &other);
    QCCookieSnapshot(QCCookieSnapshot &&other) noexcept;
    ~QCCookieSnapshot();

    QCCookieSnapshot &operator=(const QCCookieSnapshot &other);
    QCCookieSnapshot &operator=(QCCookieSnapshot &&other) noexcept;

    [[nodiscard]] QList<QNetworkCookie> cookies() const;
    void setCookies(const QList<QNetworkCookie> &cookies);

private:
    QSharedDataPointer<QCCookieSnapshotData> d;
};

class QCURL_EXPORT QCCookieDelta
{
public:
    QCCookieDelta();
    explicit QCCookieDelta(QList<QNetworkCookie> cookies);
    QCCookieDelta(const QCCookieDelta &other);
    QCCookieDelta(QCCookieDelta &&other) noexcept;
    ~QCCookieDelta();

    QCCookieDelta &operator=(const QCCookieDelta &other);
    QCCookieDelta &operator=(QCCookieDelta &&other) noexcept;

    [[nodiscard]] QList<QNetworkCookie> cookies() const;
    void setCookies(const QList<QNetworkCookie> &cookies);
    [[nodiscard]] bool isEmpty() const;

private:
    QSharedDataPointer<QCCookieDeltaData> d;
};

} // namespace QCurl

#endif // QCBLOCKINGCOOKIESTORE_H
