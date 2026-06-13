// This target is expected to FAIL after the magic-number hard break.
// It intentionally passes raw integer close codes to the typed reconnect policy.

#include <QCWebSocketReconnectPolicy.h>
#include <QSet>

int main()
{
    QCurl::QCWebSocketReconnectPolicy policy;
    policy.setRetriableCloseCodes(QSet<int>{1001, 1006, 1011});
    return policy.shouldRetry(1001, 1) ? 0 : 1;
}
