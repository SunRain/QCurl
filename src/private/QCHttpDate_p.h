/**
 * @file
 * @brief HTTP-date 解析的内部统一入口。
 */

#ifndef QCHTTPDATE_P_H
#define QCHTTPDATE_P_H

#include <QByteArray>
#include <QByteArrayView>
#include <QDateTime>

namespace QCurl::Internal {

[[nodiscard]] inline QDateTime parseHttpDate(QByteArrayView value)
{
    QByteArray normalized = value.toByteArray().trimmed();
    constexpr QByteArrayView gmtSuffix(" GMT");
    if (normalized.endsWith(gmtSuffix)) {
        normalized.chop(gmtSuffix.size());
        normalized.append(QByteArrayLiteral(" +0000"));
    }

    const QDateTime parsed = QDateTime::fromString(QString::fromLatin1(normalized), Qt::RFC2822Date);
    return parsed.isValid() ? parsed.toUTC() : QDateTime();
}

} // namespace QCurl::Internal

#endif // QCHTTPDATE_P_H
