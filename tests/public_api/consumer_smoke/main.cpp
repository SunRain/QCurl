#include "contract_probes.h"

#include <QCNetworkAccessManager.h>
#include <QCNetworkCache.h>
#include <QCNetworkCachePolicy.h>
#include <QCNetworkCancelToken.h>
#include <QCNetworkConnectionPoolConfig.h>
#include <QCNetworkConnectionPoolManager.h>
#include <QCNetworkDiskCache.h>
#include <QCNetworkDownloadToDeviceJob.h>
#include <QCNetworkDefaultLogger.h>
#include <QCNetworkHttpMethod.h>
#include <QCNetworkLaneCancelResult.h>
#include <QCNetworkLaneKey.h>
#include <QCNetworkLogger.h>
#include <QCNetworkMemoryCache.h>
#include <QCNetworkMiddleware.h>
#include <QCNetworkBody.h>
#include <QCNetworkMultipartBody.h>
#include <QCMultipartFormData.h>
#include <QCNetworkProxyConfig.h>
#include <QCNetworkResumableDownloadJob.h>
#include <QCNetworkRequest.h>
#include <QCNetworkReply.h>
#include <QCNetworkRetryPolicy.h>
#include <QCNetworkSchedulerPolicy.h>
#include <QCNetworkSslConfig.h>
#include <QCNetworkTimeoutConfig.h>
#include <QBuffer>
#include <QCoreApplication>
#include <QDateTime>
#include <QJsonObject>
#include <QList>
#include <QPair>
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

class ConsumerSmokeMiddleware : public QCurl::QCNetworkMiddleware
{
public:
    QString name() const override { return QStringLiteral("ConsumerSmokeMiddleware"); }
};

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    QCurl::QCNetworkAccessManager manager;
    QCurl::QCNetworkRequest request(QUrl(QStringLiteral("https://example.invalid")));
    request.setFollowLocation(true);
    request.setLane(QCurl::QCNetworkLaneKey::control());

    const auto method = QCurl::HttpMethod::Get;
    if (method != QCurl::HttpMethod::Get) {
        return 1;
    }

    QCurl::QCNetworkSchedulerPolicy policy = QCurl::QCNetworkSchedulerPolicy::defaultPolicy();
    policy.setMaxConcurrentRequests(6);
    policy.setMaxRequestsPerHost(2);
    policy.setMaxBandwidthBytesPerSec(0);
    policy.setThrottlingEnabled(false);

    QCurl::QCNetworkSchedulerPolicy::LaneConfig lane;
    lane.setWeight(3);
    lane.setQuantum(1);
    lane.setReservedGlobal(1);
    lane.setReservedPerHost(1);
    QString laneConfigError;
    if (!policy.setLaneConfig(QCurl::QCNetworkLaneKey::control(), lane, &laneConfigError)) {
        return 2;
    }

    QString schedulerPolicyError;
    if (!manager.setSchedulerPolicy(policy, &schedulerPolicyError)) {
        return 2;
    }

    const auto appliedConfig = manager.schedulerPolicy();
    if (appliedConfig.maxConcurrentRequests() < 1 || appliedConfig.maxRequestsPerHost() < 1) {
        return 2;
    }

    const auto appliedLane = appliedConfig.laneConfig(QCurl::QCNetworkLaneKey::control());
    if (appliedLane.weight() < 1 || appliedLane.quantum() < 1) {
        return 3;
    }
    const auto schedulerStats = manager.schedulerStatistics();
    Q_UNUSED(schedulerStats);

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

    request.setCachePolicy(QCurl::QCNetworkCachePolicy::OnlyNetwork);
    if (request.cachePolicy() != QCurl::QCNetworkCachePolicy::OnlyNetwork) {
        return 8;
    }
    const int requestConfigStatus = runRequestConfigProbe(request);
    if (requestConfigStatus != 0) {
        return requestConfigStatus;
    }

    QCurl::QCNetworkMemoryCache memoryCache;
    QCurl::QCNetworkCache *cacheInterface = &memoryCache;
    QCurl::QCNetworkCacheMetadata cacheMetadata;
    cacheMetadata.setUrl(request.url());
    cacheMetadata.setExpirationDate(QDateTime::currentDateTimeUtc().addSecs(60));
    cacheMetadata.setHeader(QByteArrayLiteral("Content-Type"), QByteArrayLiteral("text/plain"));
    const QByteArray cacheBody = QByteArrayLiteral("consumer-smoke-cache");
    cacheInterface->insert(request.url(), cacheBody, cacheMetadata);

    const auto cacheLookup = cacheInterface->lookup(request.url(),
                                                    QCurl::QCNetworkCacheReadMode::FreshOnly);
    if (cacheLookup.status() != QCurl::QCNetworkCacheLookupStatus::FreshHit
        || cacheLookup.metadata().url() != request.url() || cacheLookup.body() != cacheBody) {
        return 9;
    }

    QCurl::QCNetworkDiskCache *diskCacheTypeProbe = nullptr;
    Q_UNUSED(diskCacheTypeProbe);

    QCurl::QCMultipartFormData formData;
    if (!formData.setBoundary(QStringLiteral("----QCurlConsumerSmokeBoundary"))) {
        return 10;
    }
    formData.addTextField(QStringLiteral("name"), QStringLiteral("core"));
    formData.addFileField(QStringLiteral("file"),
                          QStringLiteral("payload.txt"),
                          QByteArrayLiteral("payload"),
                          QStringLiteral("text/plain"));
    if (formData.fieldCount() != 2
        || !formData.contentType().contains(QStringLiteral("----QCurlConsumerSmokeBoundary"))
        || formData.size() <= 0
        || !formData.toByteArray().contains(QByteArrayLiteral("payload"))) {
        return 10;
    }
    const QCurl::QCNetworkBody jsonBody = QCurl::QCNetworkBody::fromJson(
        QJsonObject{{QStringLiteral("name"), QStringLiteral("core")}});
    const QCurl::QCNetworkBody formBody = QCurl::QCNetworkBody::fromFormUrlEncoded(
        QList<QPair<QString, QString>>{{QStringLiteral("name"), QStringLiteral("core value")},
                                       {QStringLiteral("name"), QStringLiteral("second value")}});
    const bool bodyOverloadValid = jsonBody.contentType() == QByteArrayLiteral("application/json")
        && jsonBody.data().contains(QByteArrayLiteral("core"))
        && formBody.contentType() == QByteArrayLiteral("application/x-www-form-urlencoded")
        && formBody.data().contains(QByteArrayLiteral("core%20value"))
        && formBody.data().contains(QByteArrayLiteral("name=second%20value"));
    const auto multipartBody = QCurl::QCNetworkMultipartBody::fromFormData(formData);
    QByteArray singleFileBytes = QByteArrayLiteral("single-file-payload");
    QBuffer singleFileDevice(&singleFileBytes);
    singleFileDevice.open(QIODevice::ReadOnly);
    QString multipartError;
    auto singleFileMultipart = QCurl::QCNetworkMultipartBody::fromSingleFileDevice(
        &singleFileDevice,
        QStringLiteral("file"),
        QStringLiteral("payload.bin"),
        QStringLiteral("application/octet-stream"),
        singleFileBytes.size(),
        &multipartError);
    if (!singleFileMultipart.has_value()) {
        return 10;
    }
    QIODevice *singleFileWrapper = singleFileMultipart->takeDevice(&app, &multipartError);
    if (singleFileWrapper == nullptr) {
        return 10;
    }
    QBuffer downloadProbeDevice;
    downloadProbeDevice.open(QIODevice::WriteOnly);
    QCurl::QCNetworkDownloadToDeviceJob downloadJobTypeProbe(&manager,
                                                             request,
                                                             &downloadProbeDevice);
    downloadJobTypeProbe.start();
    QCurl::QCNetworkResumableDownloadJob resumableJobTypeProbe(
        &manager,
        request,
        QStringLiteral("/tmp/qcurl-consumer-smoke-resumable.bin"));
    resumableJobTypeProbe.start();
    if (jsonBody.contentType() != QByteArrayLiteral("application/json")
        || !jsonBody.data().contains(QByteArrayLiteral("core"))
        || formBody.contentType() != QByteArrayLiteral("application/x-www-form-urlencoded")
        || !formBody.data().contains(QByteArrayLiteral("core%20value"))
        || !formBody.data().contains(QByteArrayLiteral("name=second%20value"))
        || !bodyOverloadValid
        || multipartBody.contentType() != formData.contentType().toUtf8()
        || multipartBody.data() != formData.toByteArray()
        || !singleFileMultipart->contentType().contains("multipart/form-data")
        || !singleFileMultipart->sizeBytes().has_value()) {
        return 10;
    }

    QCurl::QCNetworkHttpAuthConfig authConfig;
    authConfig.setUserName(QStringLiteral("demo"));
    authConfig.setPassword(QStringLiteral("secret"));
    authConfig.setMethod(QCurl::QCNetworkHttpAuthMethod::AnySafe);
    request.setHttpAuth(authConfig);
    if (!request.httpAuth().has_value()
        || request.httpAuth()->method() != QCurl::QCNetworkHttpAuthMethod::AnySafe) {
        return 11;
    }

    QCurl::QCNetworkAccessManager::ShareHandleConfig shareConfig;
    shareConfig.setShareDnsCache(true);
    shareConfig.setShareCookies(true);
    shareConfig.setShareSslSession(true);
    manager.setShareHandleConfig(shareConfig);
    const auto savedShareConfig = manager.shareHandleConfig();
    if (!savedShareConfig.enabled() || !savedShareConfig.shareDnsCache()
        || !savedShareConfig.shareCookies() || !savedShareConfig.shareSslSession()) {
        return 12;
    }

    QCurl::QCNetworkAccessManager::HstsAltSvcCacheConfig cacheConfig;
    cacheConfig.setHstsFilePath(QStringLiteral("/tmp/qcurl-consumer-hsts.txt"));
    cacheConfig.setAltSvcFilePath(QStringLiteral("/tmp/qcurl-consumer-altsvc.txt"));
    manager.setHstsAltSvcCacheConfig(cacheConfig);
    const auto savedCacheConfig = manager.hstsAltSvcCacheConfig();
    if (!savedCacheConfig.enabled()
        || savedCacheConfig.hstsFilePath() != QStringLiteral("/tmp/qcurl-consumer-hsts.txt")
        || savedCacheConfig.altSvcFilePath()
               != QStringLiteral("/tmp/qcurl-consumer-altsvc.txt")) {
        return 13;
    }

    request.clearHttpAuth();
    proxyConfig.clearTlsConfig();
    request.setProxyConfig(proxyConfig);
    if (request.httpAuth().has_value() || proxyConfig.tlsConfig().has_value()
        || !request.proxyConfig().has_value() || request.proxyConfig()->tlsConfig().has_value()) {
        return 14;
    }

    const QCurl::QCNetworkLaneCancelResult cancelledPending = manager.cancelLaneRequests(
        QCurl::QCNetworkLaneKey::control(),
        QCurl::QCNetworkAccessManager::SchedulerCancelScope::PendingOnly);
    const QCurl::QCNetworkLaneCancelResult cancelledAll = manager.cancelLaneRequests(
        QCurl::QCNetworkLaneKey::control(),
        QCurl::QCNetworkAccessManager::SchedulerCancelScope::PendingAndRunning);
    if (cancelledPending.status() != QCurl::QCNetworkLaneCancelResult::Status::SchedulerDisabled
        || cancelledAll.status() != QCurl::QCNetworkLaneCancelResult::Status::SchedulerDisabled) {
        return 15;
    }

    ConsumerSmokeLogger logger;
    manager.setLogger(&logger);
    if (manager.logger() != &logger) {
        return 16;
    }

    manager.setDebugTraceEnabled(true);
    if (!manager.debugTraceEnabled()) {
        return 17;
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
        return 18;
    }

    logger.log(entry);
    if (logger.count != 1 || logger.lastEntry.category() != QStringLiteral("ConsumerSmoke")
        || logger.lastEntry.message() != QStringLiteral("manager logger contract")) {
        return 19;
    }

    QCurl::QCNetworkDefaultLogger defaultLogger;
    defaultLogger.enableConsoleOutput(false);
    defaultLogger.setMinLogLevel(QCurl::NetworkLogLevel::Warning);
    defaultLogger.clear();
    manager.setLogger(&defaultLogger);
    if (manager.logger() != &defaultLogger
        || defaultLogger.minLogLevel() != QCurl::NetworkLogLevel::Warning) {
        return 20;
    }

    defaultLogger.log(entry);
    if (defaultLogger.entries().size() != 1) {
        return 21;
    }

    QCurl::QCNetworkCancelToken cancelToken;
    QCurl::QCNetworkReply *replyToCancel = nullptr;
    QList<QCurl::QCNetworkReply *> repliesToCancel;
    cancelToken.attach(replyToCancel);
    cancelToken.attachMultiple(repliesToCancel);
    cancelToken.setAutoTimeout(0);
    if (cancelToken.attachedCount() != 0 || cancelToken.isCancelled()) {
        return 22;
    }

    cancelToken.cancel();
    if (!cancelToken.isCancelled()) {
        return 23;
    }

    QCurl::QCNetworkConnectionPoolConfig poolConfig;
    poolConfig.setMaxConnectionsPerHost(4);
    poolConfig.setMaxTotalConnections(12);
    poolConfig.setMaxIdleTime(45);
    poolConfig.setMaxConnectionLifetime(90);
    poolConfig.setMultiplexingEnabled(true);
    poolConfig.setDnsCacheEnabled(true);
    poolConfig.setDnsCacheTimeout(30);
    poolConfig.setMultiMaxTotalConnections(6);
    poolConfig.setMultiMaxHostConnections(2);
    poolConfig.setMultiMaxConcurrentStreams(8);
    poolConfig.setMultiMaxConnects(16);
    if (!poolConfig.isValid() || poolConfig.maxConnectionsPerHost() != 4
        || poolConfig.maxTotalConnections() != 12 || poolConfig.maxIdleTime() != 45
        || poolConfig.maxConnectionLifetime() != 90 || !poolConfig.multiplexingEnabled()
        || !poolConfig.dnsCacheEnabled() || poolConfig.dnsCacheTimeout() != 30
        || poolConfig.multiMaxTotalConnections().value_or(-1) != 6
        || poolConfig.multiMaxHostConnections().value_or(-1) != 2
        || poolConfig.multiMaxConcurrentStreams().value_or(-1) != 8
        || poolConfig.multiMaxConnects().value_or(-1) != 16) {
        return 24;
    }

    auto *poolManager = QCurl::QCNetworkConnectionPoolManager::instance();
    poolManager->setConfig(poolConfig);
    const auto savedPoolConfig = poolManager->config();
    if (savedPoolConfig.maxConnectionsPerHost() != 4
        || savedPoolConfig.maxTotalConnections() != 12) {
        return 25;
    }

    const auto poolStats = poolManager->statistics();
    if (poolStats.totalRequests() < 0 || poolStats.reusedConnections() < 0
        || poolStats.reuseRate() < 0.0 || poolStats.activeConnections() < 0
        || poolStats.idleConnections() < 0) {
        return 26;
    }
    poolManager->setConfig(QCurl::QCNetworkConnectionPoolConfig());

    const int cookieStatus = runCookieAsyncResultProbe();
    if (cookieStatus != 0) {
        return cookieStatus;
    }

    ConsumerSmokeMiddleware middleware;
    manager.addMiddleware(&middleware);
    if (manager.middlewares().size() != 1 || manager.middlewares().first() != &middleware
        || middleware.name() != QStringLiteral("ConsumerSmokeMiddleware")) {
        return 27;
    }
    manager.removeMiddleware(&middleware);
    if (!manager.middlewares().isEmpty()) {
        return 28;
    }

    manager.setLogger(nullptr);
    manager.setDebugTraceEnabled(false);
    return (!cancelledPending.isSuccess() && !cancelledAll.isSuccess()) ? 0 : 29;
}
