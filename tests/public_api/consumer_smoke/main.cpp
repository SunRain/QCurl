#include <QCNetworkAccessManager.h>
#include <QCNetworkHttpMethod.h>
#include <QCNetworkLogger.h>
#include <QCNetworkProxyConfig.h>
#include <QCNetworkRequest.h>
#include <QCNetworkRetryPolicy.h>
#include <QCNetworkRequestScheduler.h>
#include <QCNetworkSslConfig.h>
#include <QCNetworkTimeoutConfig.h>
#include <QCoreApplication>
#include <QDateTime>
#include <QUrl>

#include <chrono>

class ConsumerSmokeLogger : public QCurl::QCNetworkLogger
{
public:
    int count = 0;
    QCurl::NetworkLogEntry lastEntry;

    void log(const QCurl::NetworkLogEntry &entry) override
    {
        ++count;
        lastEntry = entry;
    }
};

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    QCurl::QCNetworkAccessManager manager;
    QCurl::QCNetworkRequest request(QUrl(QStringLiteral("https://example.invalid")));
    request.setFollowLocation(true);
    request.setLane(QStringLiteral("Control"));

    const auto method = QCurl::HttpMethod::Get;
    auto *ownerThreadScheduler = manager.scheduler();
    auto *ownerThreadAccessor  = manager.schedulerOnOwnerThread();

    if (method != QCurl::HttpMethod::Get || ownerThreadScheduler == nullptr
        || ownerThreadAccessor == nullptr || ownerThreadScheduler != ownerThreadAccessor) {
        return 1;
    }

    QCurl::QCNetworkRequestScheduler::Config config = ownerThreadScheduler->config();
    config.setMaxConcurrentRequests(6);
    config.setMaxRequestsPerHost(2);
    config.setMaxBandwidthBytesPerSec(0);
    config.setEnableThrottling(false);
    ownerThreadScheduler->setConfig(config);

    const auto appliedConfig = ownerThreadScheduler->config();
    if (appliedConfig.maxConcurrentRequests() < 1 || appliedConfig.maxRequestsPerHost() < 1) {
        return 2;
    }

    QCurl::QCNetworkRequestScheduler::LaneConfig lane;
    lane.setWeight(3);
    lane.setQuantum(1);
    lane.setReservedGlobal(1);
    lane.setReservedPerHost(1);
    ownerThreadScheduler->setLaneConfig(QStringLiteral("Control"), lane);

    const auto appliedLane = ownerThreadScheduler->laneConfig(QStringLiteral("Control"));
    if (appliedLane.weight() < 1 || appliedLane.quantum() < 1) {
        return 3;
    }

    QCurl::QCNetworkSslConfig sslConfig;
    sslConfig.setVerifyPeer(true);
    sslConfig.setVerifyHost(true);
    sslConfig.setPinnedPublicKey(QStringLiteral("sha256//consumer-smoke"));
    request.setSslConfig(sslConfig);
    if (!request.sslConfig().verifyPeer() || request.sslConfig().pinnedPublicKey().isEmpty()) {
        return 4;
    }

    QCurl::QCNetworkProxyConfig proxyConfig;
    proxyConfig.setType(QCurl::QCNetworkProxyConfig::ProxyType::Https);
    proxyConfig.setHostName(QStringLiteral("proxy.example.invalid"));
    proxyConfig.setPort(443);
    proxyConfig.setTlsConfig({});

    QCurl::QCNetworkProxyConfig::ProxyTlsConfig proxyTls;
    proxyTls.setVerifyPeer(true);
    proxyTls.setVerifyHost(true);
    proxyTls.setCipherList(QStringLiteral("TLS_AES_128_GCM_SHA256"));
    proxyConfig.setTlsConfig(proxyTls);
    request.setProxyConfig(proxyConfig);
    if (!request.proxyConfig().has_value() || !request.proxyConfig()->tlsConfig().has_value()) {
        return 5;
    }

    QCurl::QCNetworkTimeoutConfig timeoutConfig;
    timeoutConfig.setConnectTimeout(std::chrono::seconds(3));
    timeoutConfig.setTotalTimeout(std::chrono::seconds(15));
    request.setTimeoutConfig(timeoutConfig);
    if (!request.timeoutConfig().connectTimeout().has_value()
        || !request.timeoutConfig().totalTimeout().has_value()) {
        return 6;
    }

    QCurl::QCNetworkRetryPolicy retryPolicy(3, std::chrono::milliseconds(250));
    retryPolicy.setRetryHttpStatusErrorsForGetOnly(true);
    request.setRetryPolicy(retryPolicy);
    if (request.retryPolicy().maxRetries() != 3
        || !request.retryPolicy().retryHttpStatusErrorsForGetOnly()) {
        return 7;
    }

    QCurl::QCNetworkHttpAuthConfig authConfig;
    authConfig.setUserName(QStringLiteral("demo"));
    authConfig.setPassword(QStringLiteral("secret"));
    authConfig.setMethod(QCurl::QCNetworkHttpAuthMethod::AnySafe);
    request.setHttpAuth(authConfig);
    if (!request.httpAuth().has_value()
        || request.httpAuth()->method() != QCurl::QCNetworkHttpAuthMethod::AnySafe) {
        return 8;
    }

    request.clearHttpAuth();
    proxyConfig.clearTlsConfig();
    request.setProxyConfig(proxyConfig);
    if (request.httpAuth().has_value() || proxyConfig.tlsConfig().has_value()
        || !request.proxyConfig().has_value() || request.proxyConfig()->tlsConfig().has_value()) {
        return 9;
    }

    const int cancelledPending = ownerThreadScheduler->cancelLaneRequests(
        QStringLiteral("Control"), QCurl::QCNetworkRequestScheduler::CancelLaneScope::PendingOnly);
    const int cancelledAll = ownerThreadScheduler->cancelLaneRequests(
        QStringLiteral("Control"),
        QCurl::QCNetworkRequestScheduler::CancelLaneScope::PendingAndRunning);

    ConsumerSmokeLogger logger;
    manager.setLogger(&logger);
    if (manager.logger() != &logger) {
        return 10;
    }

    manager.setDebugTraceEnabled(true);
    if (!manager.debugTraceEnabled()) {
        return 11;
    }

    const QDateTime timestampUtc = QDateTime::currentDateTimeUtc();
    QCurl::NetworkLogEntry entry(
        QCurl::NetworkLogLevel::Warning,
        QStringLiteral("ConsumerSmoke"),
        QStringLiteral("manager logger contract"),
        timestampUtc);

    if (entry.level() != QCurl::NetworkLogLevel::Warning
        || entry.category() != QStringLiteral("ConsumerSmoke")
        || entry.message() != QStringLiteral("manager logger contract")
        || entry.timestampUtc().offsetFromUtc() != 0
        || entry.timestampUtc().toMSecsSinceEpoch() != timestampUtc.toMSecsSinceEpoch()) {
        return 12;
    }

    logger.log(entry);
    if (logger.count != 1 || logger.lastEntry.category() != QStringLiteral("ConsumerSmoke")
        || logger.lastEntry.message() != QStringLiteral("manager logger contract")) {
        return 13;
    }

    manager.setLogger(nullptr);
    manager.setDebugTraceEnabled(false);

    return (cancelledPending >= 0 && cancelledAll >= 0) ? 0 : 14;
}
