// This target is expected to FAIL after the magic-number hard break.
// It intentionally uses removed diagnostics overloads without options.

#include <QCNetworkDiagnostics.h>
#include <QString>
#include <QUrl>

int main()
{
    auto dns        = QCurl::QCNetworkDiagnostics::resolveDNS(QStringLiteral("example.com"));
    auto connection = QCurl::QCNetworkDiagnostics::testConnection(QStringLiteral("example.com"),
                                                                  443);
    auto ssl        = QCurl::QCNetworkDiagnostics::checkSSL(QStringLiteral("example.com"));
    auto http = QCurl::QCNetworkDiagnostics::probeHTTP(QUrl(QStringLiteral("https://example.com")));
    auto diagnostics = QCurl::QCNetworkDiagnostics::diagnose(
        QUrl(QStringLiteral("https://example.com")));
    auto ping  = QCurl::QCNetworkDiagnostics::ping(QStringLiteral("example.com"), 4, 1000);
    auto trace = QCurl::QCNetworkDiagnostics::traceroute(QStringLiteral("example.com"), 30, 1000);

    return dns.success() || connection.success() || ssl.success() || http.success()
                   || diagnostics.success() || ping.success() || trace.success()
               ? 0
               : 1;
}
