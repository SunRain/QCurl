#include <QCNetworkAccessManager.h>
#include <QCNetworkHttpMethod.h>
#include <QCNetworkRequest.h>
#include <QCNetworkRequestScheduler.h>
#include <QUrl>

int main()
{
    QCurl::QCNetworkAccessManager manager;
    QCurl::QCNetworkRequest request(QUrl(QStringLiteral("https://example.invalid")));
    request.setFollowLocation(true);

    const auto method = QCurl::HttpMethod::Get;
    auto *scheduler   = manager.scheduler();
    return (scheduler != nullptr || method == QCurl::HttpMethod::Get) ? 0 : 1;
}
