#include "QCWebSocketCompressionConfig.h"

#ifdef QCURL_WEBSOCKET_SUPPORT

#include <QSharedData>
#include <QStringList>

namespace QCurl {

/// WebSocket 压缩配置的共享负载；不保存运行时 zlib 状态。
class QCWebSocketCompressionConfigData : public QSharedData
{
public:
    bool enabled = false;
    int clientMaxWindowBits = 15;
    int serverMaxWindowBits = 15;
    bool clientNoContextTakeover = false;
    bool serverNoContextTakeover = false;
    int compressionLevel = 6;
};

QCWebSocketCompressionConfig::QCWebSocketCompressionConfig()
    : d(new QCWebSocketCompressionConfigData)
{
}

QCWebSocketCompressionConfig::QCWebSocketCompressionConfig(
    const QCWebSocketCompressionConfig &other) = default;

QCWebSocketCompressionConfig::QCWebSocketCompressionConfig(
    QCWebSocketCompressionConfig &&other) noexcept = default;

QCWebSocketCompressionConfig::~QCWebSocketCompressionConfig() = default;

QCWebSocketCompressionConfig &QCWebSocketCompressionConfig::operator=(
    const QCWebSocketCompressionConfig &other) = default;

QCWebSocketCompressionConfig &QCWebSocketCompressionConfig::operator=(
    QCWebSocketCompressionConfig &&other) noexcept = default;

bool QCWebSocketCompressionConfig::enabled() const noexcept
{
    return d->enabled;
}

void QCWebSocketCompressionConfig::setEnabled(bool enabled) noexcept
{
    d->enabled = enabled;
}

int QCWebSocketCompressionConfig::clientMaxWindowBits() const noexcept
{
    return d->clientMaxWindowBits;
}

void QCWebSocketCompressionConfig::setClientMaxWindowBits(int bits) noexcept
{
    d->clientMaxWindowBits = bits;
}

int QCWebSocketCompressionConfig::serverMaxWindowBits() const noexcept
{
    return d->serverMaxWindowBits;
}

void QCWebSocketCompressionConfig::setServerMaxWindowBits(int bits) noexcept
{
    d->serverMaxWindowBits = bits;
}

bool QCWebSocketCompressionConfig::clientNoContextTakeover() const noexcept
{
    return d->clientNoContextTakeover;
}

void QCWebSocketCompressionConfig::setClientNoContextTakeover(bool enabled) noexcept
{
    d->clientNoContextTakeover = enabled;
}

bool QCWebSocketCompressionConfig::serverNoContextTakeover() const noexcept
{
    return d->serverNoContextTakeover;
}

void QCWebSocketCompressionConfig::setServerNoContextTakeover(bool enabled) noexcept
{
    d->serverNoContextTakeover = enabled;
}

int QCWebSocketCompressionConfig::compressionLevel() const noexcept
{
    return d->compressionLevel;
}

void QCWebSocketCompressionConfig::setCompressionLevel(int level) noexcept
{
    d->compressionLevel = level;
}

QString QCWebSocketCompressionConfig::toExtensionHeader() const
{
    if (!d->enabled) {
        return QString();
    }

    QStringList parts;
    parts << QStringLiteral("permessage-deflate");

    // 客户端窗口位数
    if (d->clientMaxWindowBits < 15) {
        parts << QStringLiteral("client_max_window_bits=%1").arg(d->clientMaxWindowBits);
    }

    if (d->serverMaxWindowBits < 15) {
        parts << QStringLiteral("server_max_window_bits=%1").arg(d->serverMaxWindowBits);
    }

    if (d->clientNoContextTakeover) {
        parts << QStringLiteral("client_no_context_takeover");
    }

    if (d->serverNoContextTakeover) {
        parts << QStringLiteral("server_no_context_takeover");
    }

    return parts.join(QStringLiteral("; "));
}

QCWebSocketCompressionConfig QCWebSocketCompressionConfig::fromExtensionHeader(const QString &header)
{
    QCWebSocketCompressionConfig config;

    if (header.isEmpty()
        || !header.contains(QStringLiteral("permessage-deflate"), Qt::CaseInsensitive)) {
        config.setEnabled(false);
        return config;
    }

    config.setEnabled(true);

    // 解析参数
    QStringList parts = header.split(QLatin1Char(';'), Qt::SkipEmptyParts);

    for (const QString &part : parts) {
        QString trimmed = part.trimmed();

        if (trimmed.startsWith(QStringLiteral("client_max_window_bits"), Qt::CaseInsensitive)) {
            int eqPos = trimmed.indexOf(QLatin1Char('='));
            if (eqPos > 0) {
                bool ok;
                int bits = trimmed.mid(eqPos + 1).trimmed().toInt(&ok);
                if (ok && bits >= 8 && bits <= 15) {
                    config.setClientMaxWindowBits(bits);
                }
            }
        } else if (trimmed.startsWith(QStringLiteral("server_max_window_bits"),
                                      Qt::CaseInsensitive)) {
            int eqPos = trimmed.indexOf(QLatin1Char('='));
            if (eqPos > 0) {
                bool ok;
                int bits = trimmed.mid(eqPos + 1).trimmed().toInt(&ok);
                if (ok && bits >= 8 && bits <= 15) {
                    config.setServerMaxWindowBits(bits);
                }
            }
        } else if (trimmed.contains(QStringLiteral("client_no_context_takeover"),
                                    Qt::CaseInsensitive)) {
            config.setClientNoContextTakeover(true);
        } else if (trimmed.contains(QStringLiteral("server_no_context_takeover"),
                                    Qt::CaseInsensitive)) {
            config.setServerNoContextTakeover(true);
        }
    }

    return config;
}

QCWebSocketCompressionConfig QCWebSocketCompressionConfig::defaultConfig()
{
    QCWebSocketCompressionConfig config;
    config.setEnabled(true);
    config.setClientMaxWindowBits(15);
    config.setServerMaxWindowBits(15);
    config.setClientNoContextTakeover(false);
    config.setServerNoContextTakeover(false);
    config.setCompressionLevel(6);
    return config;
}

QCWebSocketCompressionConfig QCWebSocketCompressionConfig::lowMemoryConfig()
{
    QCWebSocketCompressionConfig config;
    config.setEnabled(true);
    config.setClientMaxWindowBits(9);
    config.setServerMaxWindowBits(9);
    config.setClientNoContextTakeover(true);
    config.setServerNoContextTakeover(true);
    config.setCompressionLevel(3);
    return config;
}

QCWebSocketCompressionConfig QCWebSocketCompressionConfig::maxCompressionConfig()
{
    QCWebSocketCompressionConfig config;
    config.setEnabled(true);
    config.setClientMaxWindowBits(15);
    config.setServerMaxWindowBits(15);
    config.setClientNoContextTakeover(false);
    config.setServerNoContextTakeover(false);
    config.setCompressionLevel(9);
    return config;
}

} // namespace QCurl

#endif // QCURL_WEBSOCKET_SUPPORT
