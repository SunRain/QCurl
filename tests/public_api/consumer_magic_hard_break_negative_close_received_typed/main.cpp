// This target is expected to FAIL after the magic-number hard break.
// It intentionally connects to the removed typed closeReceived signal shape.

#include <QCWebSocket.h>
#include <QObject>
#include <QString>
#include <QUrl>

int main()
{
    QCurl::QCWebSocket socket(QUrl(QStringLiteral("wss://example.invalid")),
                              QCurl::QCWebSocketOptions{});
    QObject::connect(&socket,
                     &QCurl::QCWebSocket::closeReceived,
                     &socket,
                     [](QCurl::QCWebSocket::CloseCode, const QString &) {});
    return 0;
}
