#include "QCWebSocket.h"

#ifdef QCURL_WEBSOCKET_SUPPORT

#include "QCNetworkSslConfig.h"
#include "QCWebSocketReconnectPolicy.h"
#include "private/QCCurlOptionAdapter_p.h"

#include <QSharedData>

namespace QCurl {

namespace {

constexpr std::chrono::seconds kDefaultConnectTimeout{10};
constexpr std::chrono::seconds kDefaultCloseHandshakeTimeout{5};
constexpr qint64 kDefaultMaxFrameBytes         = 16 * 1024 * 1024;
constexpr qint64 kDefaultMaxMessageBytes       = 32 * 1024 * 1024;
constexpr qint64 kDefaultMaxPendingSendBytes   = 16 * 1024 * 1024;
constexpr qint64 kDefaultMaxReceiveBufferBytes = 16 * 1024 * 1024;
constexpr qint64 kMaxWebSocketBufferBytes      = 256 * 1024 * 1024;

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
    bool autoPongEnabled = true;
    qint64 maxFrameBytes                            = kDefaultMaxFrameBytes;
    qint64 maxMessageBytes                          = kDefaultMaxMessageBytes;
    qint64 maxPendingSendBytes                      = kDefaultMaxPendingSendBytes;
    qint64 maxReceiveBufferBytes                    = kDefaultMaxReceiveBufferBytes;
    std::chrono::milliseconds closeHandshakeTimeout = kDefaultCloseHandshakeTimeout;
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

bool QCWebSocketOptions::autoPongEnabled() const noexcept
{
    return d->autoPongEnabled;
}

void QCWebSocketOptions::setAutoPongEnabled(bool enabled) noexcept
{
    d->autoPongEnabled = enabled;
}

namespace {

bool validateBufferLimit(qint64 bytes, QString *error, const QString &name)
{
    if (bytes <= 0 || bytes > kMaxWebSocketBufferBytes) {
        return failOption(error,
                          QStringLiteral("%1 必须在 1 到 %2 字节之间")
                              .arg(name)
                              .arg(kMaxWebSocketBufferBytes));
    }
    return true;
}

} // namespace

qint64 QCWebSocketOptions::maxFrameBytes() const noexcept
{
    return d->maxFrameBytes;
}

bool QCWebSocketOptions::setMaxFrameBytes(qint64 bytes, QString *error)
{
    if (!validateBufferLimit(bytes, error, QStringLiteral("maxFrameBytes"))) {
        return false;
    }
    d->maxFrameBytes = bytes;
    return true;
}

qint64 QCWebSocketOptions::maxMessageBytes() const noexcept
{
    return d->maxMessageBytes;
}

bool QCWebSocketOptions::setMaxMessageBytes(qint64 bytes, QString *error)
{
    if (!validateBufferLimit(bytes, error, QStringLiteral("maxMessageBytes"))) {
        return false;
    }
    d->maxMessageBytes = bytes;
    return true;
}

qint64 QCWebSocketOptions::maxPendingSendBytes() const noexcept
{
    return d->maxPendingSendBytes;
}

bool QCWebSocketOptions::setMaxPendingSendBytes(qint64 bytes, QString *error)
{
    if (!validateBufferLimit(bytes, error, QStringLiteral("maxPendingSendBytes"))) {
        return false;
    }
    d->maxPendingSendBytes = bytes;
    return true;
}

qint64 QCWebSocketOptions::maxReceiveBufferBytes() const noexcept
{
    return d->maxReceiveBufferBytes;
}

bool QCWebSocketOptions::setMaxReceiveBufferBytes(qint64 bytes, QString *error)
{
    if (!validateBufferLimit(bytes, error, QStringLiteral("maxReceiveBufferBytes"))) {
        return false;
    }
    d->maxReceiveBufferBytes = bytes;
    return true;
}

std::chrono::milliseconds QCWebSocketOptions::closeHandshakeTimeout() const noexcept
{
    return d->closeHandshakeTimeout;
}

bool QCWebSocketOptions::setCloseHandshakeTimeout(std::chrono::milliseconds timeout, QString *error)
{
    if (timeout.count() <= 0) {
        return failOption(error, QStringLiteral("closeHandshakeTimeout 必须大于 0ms"));
    }

    long timeoutMs = 0;
    if (!Internal::CurlOptions::tryCurlMilliseconds(timeout, &timeoutMs)) {
        return failOption(error, QStringLiteral("closeHandshakeTimeout 超出可表达范围"));
    }

    d->closeHandshakeTimeout = timeout;
    return true;
}

} // namespace QCurl

#endif // QCURL_WEBSOCKET_SUPPORT
