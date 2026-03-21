#include "QCNetworkProxyConfig.h"

namespace QCurl {

bool QCNetworkProxyConfig::isValid() const noexcept
{
    if (type == ProxyType::None) {
        return true;
    }

    // 需要代理时，检查主机名和端口
    return !hostName.isEmpty() && port > 0;
}

} // namespace QCurl
