#include "QCNetworkTimeoutConfig.h"

namespace QCurl {

QCNetworkTimeoutConfig QCNetworkTimeoutConfig::defaultConfig()
{
    // 默认配置：所有选项都是 std::nullopt
    return QCNetworkTimeoutConfig{};
}

} // namespace QCurl
