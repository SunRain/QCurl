/**
 * @file
 * @brief 声明 Blocking Extras 的 cookie 快照值类型。
 */

#ifndef QCBLOCKINGCOOKIESTORE_H
#define QCBLOCKINGCOOKIESTORE_H

#include "QCCookie.h"
#include "QCGlobal.h"

#include <QList>
#include <QSharedDataPointer>

namespace QCurl {

class QCCookieDeltaData;
class QCCookieSnapshotData;

/** Blocking request 入口使用的 cookie 快照值类型。 */
class QCURL_EXPORT QCCookieSnapshot
{
public:
    QCCookieSnapshot();
    explicit QCCookieSnapshot(const QList<QCCookie> &cookies);
    QCCookieSnapshot(const QCCookieSnapshot &other);
    QCCookieSnapshot(QCCookieSnapshot &&other) noexcept;
    ~QCCookieSnapshot();

    QCCookieSnapshot &operator=(const QCCookieSnapshot &other);
    QCCookieSnapshot &operator=(QCCookieSnapshot &&other) noexcept;

    [[nodiscard]] QList<QCCookie> cookies() const;
    void setCookies(const QList<QCCookie> &cookies);

private:
    QSharedDataPointer<QCCookieSnapshotData> d;
};

/** Blocking response 返回的 cookie 增量值类型。 */
class QCURL_EXPORT QCCookieDelta
{
public:
    QCCookieDelta();
    explicit QCCookieDelta(const QList<QCCookie> &cookies);
    QCCookieDelta(const QCCookieDelta &other);
    QCCookieDelta(QCCookieDelta &&other) noexcept;
    ~QCCookieDelta();

    QCCookieDelta &operator=(const QCCookieDelta &other);
    QCCookieDelta &operator=(QCCookieDelta &&other) noexcept;

    [[nodiscard]] QList<QCCookie> cookies() const;
    void setCookies(const QList<QCCookie> &cookies);
    [[nodiscard]] bool isEmpty() const;

private:
    QSharedDataPointer<QCCookieDeltaData> d;
};

} // namespace QCurl

#endif // QCBLOCKINGCOOKIESTORE_H
