#include "QCWebSocket.h"

#ifdef QCURL_WEBSOCKET_SUPPORT

#include "QCNetworkSslConfig.h"
#include "QCWebSocketCompressionConfig.h"
#include "QCWebSocketReconnectPolicy.h"
#include "private/QCCurlOptionAdapter_p.h"

#include <QSharedData>

namespace QCurl {

namespace {

constexpr std::chrono::seconds kDefaultConnectTimeout{10};

bool failOption(QString *error, const QString &message)
{
    if (error) {
        *error = message;
    }
    return false;
}

} // namespace

/// WebSocket options 的共享负载；只保存下一次连接的配置。
class QCWebSocketOptionsData : public QSharedData
{
public:
    std::chrono::milliseconds connectTimeout = kDefaultConnectTimeout;
    QCNetworkSslConfig sslConfig;
    QCWebSocketReconnectPolicy reconnectPolicy;
    QCWebSocketCompressionConfig compressionConfig;
    bool autoPongEnabled = true;
};

QCWebSocketOptions::QCWebSocketOptions()
    : d(new QCWebSocketOptionsData)
{}

QCWebSocketOptions::QCWebSocketOptions(const QCWebSocketOptions &other) = default;

QCWebSocketOptions::QCWebSocketOptions(QCWebSocketOptions &&other) noexcept = default;

QCWebSocketOptions::~QCWebSocketOptions() = default;

QCWebSocketOptions &QCWebSocketOptions::operator=(const QCWebSocketOptions &other) = default;

QCWebSocketOptions &QCWebSocketOptions::operator=(QCWebSocketOptions &&other) noexcept = default;

std::chrono::milliseconds QCWebSocketOptions::connectTimeout() const noexcept
{
    return d->connectTimeout;
}

bool QCWebSocketOptions::setConnectTimeout(std::chrono::milliseconds timeout, QString *error)
{
    if (timeout.count() <= 0) {
        return failOption(error, QStringLiteral("connectTimeout 必须大于 0ms"));
    }

    long timeoutMs = 0;
    if (!Internal::CurlOptions::tryCurlMilliseconds(timeout, &timeoutMs)) {
        return failOption(error, QStringLiteral("connectTimeout 超出 libcurl 毫秒超时可表达范围"));
    }

    d->connectTimeout = timeout;
    return true;
}

QCNetworkSslConfig QCWebSocketOptions::sslConfig() const
{
    return d->sslConfig;
}

void QCWebSocketOptions::setSslConfig(const QCNetworkSslConfig &config)
{
    d->sslConfig = config;
}

QCWebSocketReconnectPolicy QCWebSocketOptions::reconnectPolicy() const
{
    return d->reconnectPolicy;
}

void QCWebSocketOptions::setReconnectPolicy(const QCWebSocketReconnectPolicy &policy)
{
    d->reconnectPolicy = policy;
}

QCWebSocketCompressionConfig QCWebSocketOptions::compressionConfig() const
{
    return d->compressionConfig;
}

void QCWebSocketOptions::setCompressionConfig(const QCWebSocketCompressionConfig &config)
{
    d->compressionConfig = config;
}

bool QCWebSocketOptions::autoPongEnabled() const noexcept
{
    return d->autoPongEnabled;
}

void QCWebSocketOptions::setAutoPongEnabled(bool enabled) noexcept
{
    d->autoPongEnabled = enabled;
}

} // namespace QCurl

#endif // QCURL_WEBSOCKET_SUPPORT
