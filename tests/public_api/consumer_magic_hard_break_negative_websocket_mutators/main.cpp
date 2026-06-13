// This target is expected to FAIL after the magic-number hard break.
// It intentionally uses removed QCWebSocket direct mutators.

#include <QCWebSocket.h>
#include <QCWebSocketCompressionConfig.h>
#include <QCWebSocketReconnectPolicy.h>
#include <QUrl>

int main()
{
    QCurl::QCWebSocket socket(QUrl(QStringLiteral("wss://example.invalid")),
                              QCurl::QCWebSocketOptions{});
    socket.setAutoPongEnabled(false);
    socket.setCompressionConfig(QCurl::QCWebSocketCompressionConfig::defaultConfig());
    socket.setReconnectPolicy(QCurl::QCWebSocketReconnectPolicy::standardReconnect());
    return socket.isValid() ? 0 : 1;
}
