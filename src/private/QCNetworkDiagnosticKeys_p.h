/**
 * @file
 * @brief 定义诊断实现共用的明细键，不承诺稳定公共 schema。
 */

#ifndef QCNETWORKDIAGNOSTICKEYS_P_H
#define QCNETWORKDIAGNOSTICKEYS_P_H

#include <QString>

namespace QCurl::Internal::diagnostickeys {

inline const QString kTarget      = QStringLiteral("target");
inline const QString kHost        = QStringLiteral("host");
inline const QString kResolvedIp  = QStringLiteral("resolvedIP");
inline const QString kErrorString = QStringLiteral("errorString");

} // namespace QCurl::Internal::diagnostickeys

#endif // QCNETWORKDIAGNOSTICKEYS_P_H
