#include "QCNetworkSslConfig.h"

namespace QCurl {

QCNetworkSslConfig QCNetworkSslConfig::defaultConfig()
{
    QCNetworkSslConfig config;
    config.verifyPeer = true;
    config.verifyHost = true;
    // 其他字段使用默认值（空字符串表示使用系统默认）
    return config;
}

QCNetworkSslConfig QCNetworkSslConfig::insecureConfig()
{
    QCNetworkSslConfig config;
    config.verifyPeer = false;
    config.verifyHost = false;
    return config;
}

} // namespace QCurl
