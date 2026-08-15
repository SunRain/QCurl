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

/**
 * @brief 保存 QCNetworkRequest 的隐式共享配置快照。
 *
 * 所有字段均为自持有值，保证请求复制、排队或跨线程传递时不依赖借用参数生命周期。
 */
class QCNetworkRequestPrivate : public QSharedData
{
public:
    QCNetworkRequestPrivate()
        : sslConfig(QCNetworkSslConfig::defaultConfig())
        , timeoutConfig(QCNetworkTimeoutConfig::defaultConfig())
        , retryPolicy(QCNetworkRetryPolicy::noRetry())
        , lane(QCNetworkLaneKey::defaultLane())
    {}

    int rangeStart = -1;
    int rangeEnd   = -1;
    QMap<QByteArray, QByteArray> rawHeaderMap;
    QUrl requestUrl;

    QCNetworkRedirectConfig redirectConfig;
    QCNetworkTransferConfig transferConfig;

#ifdef QCURL_ENABLE_ADVANCED_REQUEST_NETWORK_PATH_API
    /// 以下空值均表示沿用 libcurl 或系统默认网络路径策略。
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
    std::optional<QCNetworkProxyConfig> proxyConfig; ///< 空值表示不覆盖默认代理策略。
    QCNetworkTimeoutConfig timeoutConfig;
    QCNetworkHttpVersion httpVersion = QCNetworkHttpVersion::Http1_1;
    bool httpVersionExplicit         = false; ///< 区分显式 HTTP/1.1 与默认回退值。

    QCNetworkRetryPolicy retryPolicy;
    bool retryPolicyExplicit = false; ///< 区分显式 no-retry 与默认策略。

    std::optional<QCNetworkHttpAuthConfig> httpAuthConfig;
    QCNetworkLaneKey lane;
    QCNetworkRequestPriority requestPriority = QCNetworkRequestPriority::Normal;
    QCNetworkCachePolicy cachePolicy         = QCNetworkCachePolicy::PreferCache;
    QByteArray cachePartitionKey;
};

} // namespace QCurl

#endif // QCNETWORKREQUESTPRIVATE_P_H
