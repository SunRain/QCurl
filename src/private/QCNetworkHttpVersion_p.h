/**
 * @file
 * @brief 声明 HTTP 版本内部辅助接口。
 */

#ifndef QCNETWORKHTTPVERSION_P_H
#define QCNETWORKHTTPVERSION_P_H

#include "QCNetworkHttpVersion.h"

#include <QString>

namespace QCurl::detail {

[[nodiscard]] long toCurlHttpVersion(QCNetworkHttpVersion version) noexcept;

/// 共享 Core 与 Blocking 的 HTTP/3 能力判定；仅 Http3 允许协商降级。
[[nodiscard]] bool resolveHttpVersion(QCNetworkHttpVersion requested,
                                      QCNetworkHttpVersion *effective,
                                      QString *error,
                                      QString *warning);

} // namespace QCurl::detail

#endif // QCNETWORKHTTPVERSION_P_H
