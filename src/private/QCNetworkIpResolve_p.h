#ifndef QCNETWORKIPRESOLVE_P_H
#define QCNETWORKIPRESOLVE_P_H

#include "QCNetworkRequestConfig.h"

#include <curl/curl.h>

namespace QCurl::Internal {

[[nodiscard]] inline long curlIpResolveValue(QCNetworkIpResolve value) noexcept
{
    switch (value) {
        case QCNetworkIpResolve::Any:
            return CURL_IPRESOLVE_WHATEVER;
        case QCNetworkIpResolve::Ipv4:
            return CURL_IPRESOLVE_V4;
        case QCNetworkIpResolve::Ipv6:
            return CURL_IPRESOLVE_V6;
    }
    return CURL_IPRESOLVE_WHATEVER;
}

} // namespace QCurl::Internal

#endif // QCNETWORKIPRESOLVE_P_H
