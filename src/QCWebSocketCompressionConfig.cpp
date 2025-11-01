#include "QCWebSocketCompressionConfig.h"

#ifdef QCURL_WEBSOCKET_SUPPORT

#include <QStringList>

QT_BEGIN_NAMESPACE

namespace QCurl {

QString QCWebSocketCompressionConfig::toExtensionHeader() const
{
    if (!enabled) {
        return QString();
    }
    
    QStringList parts;
    parts << "permessage-deflate";
    
    // 客户端窗口位数
    if (clientMaxWindowBits < 15) {
        parts << QString("client_max_window_bits=%1").arg(clientMaxWindowBits);
    }
    
    // 服务器窗口位数
    if (serverMaxWindowBits < 15) {
        parts << QString("server_max_window_bits=%1").arg(serverMaxWindowBits);
    }
    
    // 客户端无上下文接管
    if (clientNoContextTakeover) {
        parts << "client_no_context_takeover";
    }
    
    // 服务器无上下文接管
    if (serverNoContextTakeover) {
        parts << "server_no_context_takeover";
    }
    
    return parts.join("; ");
}

QCWebSocketCompressionConfig QCWebSocketCompressionConfig::fromExtensionHeader(const QString &header)
{
    QCWebSocketCompressionConfig config;
    
    if (header.isEmpty() || !header.contains("permessage-deflate", Qt::CaseInsensitive)) {
        config.enabled = false;
        return config;
    }
    
    config.enabled = true;
    
    // 解析参数
    QStringList parts = header.split(';', Qt::SkipEmptyParts);
    
    for (const QString &part : parts) {
        QString trimmed = part.trimmed();
        
        if (trimmed.startsWith("client_max_window_bits", Qt::CaseInsensitive)) {
            int eqPos = trimmed.indexOf('=');
            if (eqPos > 0) {
                bool ok;
                int bits = trimmed.mid(eqPos + 1).trimmed().toInt(&ok);
                if (ok && bits >= 8 && bits <= 15) {
                    config.clientMaxWindowBits = bits;
                }
            }
        } else if (trimmed.startsWith("server_max_window_bits", Qt::CaseInsensitive)) {
            int eqPos = trimmed.indexOf('=');
            if (eqPos > 0) {
                bool ok;
                int bits = trimmed.mid(eqPos + 1).trimmed().toInt(&ok);
                if (ok && bits >= 8 && bits <= 15) {
                    config.serverMaxWindowBits = bits;
                }
            }
        } else if (trimmed.contains("client_no_context_takeover", Qt::CaseInsensitive)) {
            config.clientNoContextTakeover = true;
        } else if (trimmed.contains("server_no_context_takeover", Qt::CaseInsensitive)) {
            config.serverNoContextTakeover = true;
        }
    }
    
    return config;
}

QCWebSocketCompressionConfig QCWebSocketCompressionConfig::defaultConfig()
{
    QCWebSocketCompressionConfig config;
    config.enabled = true;
    config.clientMaxWindowBits = 15;
    config.serverMaxWindowBits = 15;
    config.clientNoContextTakeover = false;
    config.serverNoContextTakeover = false;
    config.compressionLevel = 6;
    return config;
}

QCWebSocketCompressionConfig QCWebSocketCompressionConfig::lowMemoryConfig()
{
    QCWebSocketCompressionConfig config;
    config.enabled = true;
    config.clientMaxWindowBits = 9;   // 512B 窗口
    config.serverMaxWindowBits = 9;
    config.clientNoContextTakeover = true;  // 不保留状态
    config.serverNoContextTakeover = true;
    config.compressionLevel = 3;      // 较低压缩级别
    return config;
}

QCWebSocketCompressionConfig QCWebSocketCompressionConfig::maxCompressionConfig()
{
    QCWebSocketCompressionConfig config;
    config.enabled = true;
    config.clientMaxWindowBits = 15;  // 32KB 窗口
    config.serverMaxWindowBits = 15;
    config.clientNoContextTakeover = false;  // 保留状态提升压缩率
    config.serverNoContextTakeover = false;
    config.compressionLevel = 9;      // 最高压缩级别
    return config;
}

} // namespace QCurl

QT_END_NAMESPACE

#endif // QCURL_WEBSOCKET_SUPPORT
