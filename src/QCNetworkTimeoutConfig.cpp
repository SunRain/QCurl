#include "QCNetworkTimeoutConfig.h"

#include <QSharedData>

namespace QCurl {

class QCNetworkTimeoutConfigData : public QSharedData
{
public:
    std::optional<std::chrono::milliseconds> connectTimeout = std::nullopt;
    std::optional<std::chrono::milliseconds> totalTimeout = std::nullopt;
    std::optional<std::chrono::seconds> lowSpeedTime = std::nullopt;
    std::optional<long> lowSpeedLimit = std::nullopt;
};

QCNetworkTimeoutConfig::QCNetworkTimeoutConfig()
    : d(new QCNetworkTimeoutConfigData)
{
}

QCNetworkTimeoutConfig::QCNetworkTimeoutConfig(const QCNetworkTimeoutConfig &other) = default;

QCNetworkTimeoutConfig::QCNetworkTimeoutConfig(QCNetworkTimeoutConfig &&other) = default;

QCNetworkTimeoutConfig::~QCNetworkTimeoutConfig() = default;

QCNetworkTimeoutConfig &QCNetworkTimeoutConfig::operator=(const QCNetworkTimeoutConfig &other)
    = default;

QCNetworkTimeoutConfig &QCNetworkTimeoutConfig::operator=(QCNetworkTimeoutConfig &&other)
    = default;

std::optional<std::chrono::milliseconds> QCNetworkTimeoutConfig::connectTimeout() const
{
    return d->connectTimeout;
}

void QCNetworkTimeoutConfig::setConnectTimeout(
    const std::optional<std::chrono::milliseconds> &timeout)
{
    d->connectTimeout = timeout;
}

std::optional<std::chrono::milliseconds> QCNetworkTimeoutConfig::totalTimeout() const
{
    return d->totalTimeout;
}

void QCNetworkTimeoutConfig::setTotalTimeout(
    const std::optional<std::chrono::milliseconds> &timeout)
{
    d->totalTimeout = timeout;
}

std::optional<std::chrono::seconds> QCNetworkTimeoutConfig::lowSpeedTime() const
{
    return d->lowSpeedTime;
}

void QCNetworkTimeoutConfig::setLowSpeedTime(const std::optional<std::chrono::seconds> &timeout)
{
    d->lowSpeedTime = timeout;
}

std::optional<long> QCNetworkTimeoutConfig::lowSpeedLimit() const
{
    return d->lowSpeedLimit;
}

void QCNetworkTimeoutConfig::setLowSpeedLimit(const std::optional<long> &bytesPerSecond)
{
    d->lowSpeedLimit = bytesPerSecond;
}

QCNetworkTimeoutConfig QCNetworkTimeoutConfig::defaultConfig()
{
    // 默认配置：所有选项都是 std::nullopt
    return QCNetworkTimeoutConfig();
}

} // namespace QCurl
