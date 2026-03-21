#ifndef QCNETWORKHTTPVERSION_P_H
#define QCNETWORKHTTPVERSION_P_H

#include "QCNetworkHttpVersion.h"

namespace QCurl::detail {

[[nodiscard]] long toCurlHttpVersion(QCNetworkHttpVersion version) noexcept;

} // namespace QCurl::detail

#endif // QCNETWORKHTTPVERSION_P_H
