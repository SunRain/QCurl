#ifndef QCNETWORKREQUESTPRIVATE_P_H
#define QCNETWORKREQUESTPRIVATE_P_H

#include "QCNetworkCachePolicy.h"
#include "QCNetworkHttpVersion.h"
#include "QCNetworkProxyConfig.h"
#include "QCNetworkRequest.h"
#include "QCNetworkRequestPriority.h"
#include "QCNetworkRetryPolicy.h"
#include "QCNetworkSslConfig.h"
#include "QCNetworkTimeoutConfig.h"

#include <QByteArray>
#include <QMap>
#include <QSharedData>
#include <QString>
#include <QStringList>
#include <QUrl>

#include <chrono>
#include <optional>

namespace QCurl {

class QCNetworkRequestPrivate : public QSharedData
{
public:
    QCNetworkRequestPrivate()
        : sslConfig(QCNetworkSslConfig::defaultConfig())
        , timeoutConfig(QCNetworkTimeoutConfig::defaultConfig())
        , retryPolicy(QCNetworkRetryPolicy::noRetry())
    {}

    int rangeStart = -1;
    int rangeEnd = -1;
    QMap<QByteArray, QByteArray> rawHeaderMap;
    QUrl reqUrl;

    QCNetworkRedirectConfig redirectConfig;
    QCNetworkTransferConfig transferConfig;

#ifdef QCURL_ENABLE_ADVANCED_REQUEST_NETWORK_PATH_API
    std::optional<std::chrono::milliseconds> happyEyeballsTimeout;
    std::optional<QString> networkInterface;
    std::optional<int> localPort;
    std::optional<int> localPortRange;
    std::optional<QStringList> resolveOverride;
    std::optional<QStringList> connectTo;
    std::optional<QStringList> dnsServers;
    std::optional<QUrl> dohUrl;
#endif

    QCNetworkSslConfig sslConfig;
    std::optional<QCNetworkProxyConfig> proxyConfig;
    QCNetworkTimeoutConfig timeoutConfig;
    QCNetworkHttpVersion httpVersion = QCNetworkHttpVersion::Http1_1;
    bool httpVersionExplicit = false;

    QCNetworkRetryPolicy retryPolicy;
    bool retryPolicyExplicit = false;

    std::optional<QCNetworkHttpAuthConfig> httpAuthConfig;
    QString lane;
    QCNetworkRequestPriority requestPriority = QCNetworkRequestPriority::Normal;
    QCNetworkCachePolicy cachePolicy = QCNetworkCachePolicy::PreferCache;
};

} // namespace QCurl

#endif // QCNETWORKREQUESTPRIVATE_P_H
