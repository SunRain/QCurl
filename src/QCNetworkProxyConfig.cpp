#include "QCNetworkProxyConfig.h"

QT_BEGIN_NAMESPACE

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
QT_END_NAMESPACE
