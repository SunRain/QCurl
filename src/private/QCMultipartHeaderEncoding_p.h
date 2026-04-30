/**
 * @file
 * @brief 提供 multipart header 参数和值的内部编码 helper。
 */

#ifndef QCMULTIPARTHEADERENCODING_P_H
#define QCMULTIPARTHEADERENCODING_P_H

#include <QByteArray>
#include <QString>
#include <QtGlobal>

namespace QCurl::Internal {

[[nodiscard]] inline QByteArray encodeMultipartHeaderQuotedString(const QString &value)
{
    QByteArray encoded;
    const QByteArray raw = value.toUtf8();
    encoded.reserve(raw.size());
    for (char ch : raw) {
        if (ch == '\r' || ch == '\n') {
            encoded.append(' ');
            continue;
        }
        if (ch == '"' || ch == '\\') {
            encoded.append('\\');
        }
        encoded.append(ch);
    }
    return encoded;
}

[[nodiscard]] inline QByteArray sanitizeMultipartHeaderValue(const QString &value,
                                                            const QByteArray &fallback)
{
    const QByteArray raw = value.toUtf8();
    const int cr = raw.indexOf('\r');
    const int lf = raw.indexOf('\n');
    int end = raw.size();
    if (cr >= 0) {
        end = qMin(end, cr);
    }
    if (lf >= 0) {
        end = qMin(end, lf);
    }

    const QByteArray sanitized = raw.left(end).trimmed();
    return sanitized.isEmpty() ? fallback : sanitized;
}

} // namespace QCurl::Internal

#endif // QCMULTIPARTHEADERENCODING_P_H
