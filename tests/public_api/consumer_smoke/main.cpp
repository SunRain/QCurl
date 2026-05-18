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
#include <QCNetworkLogger.h>
#include <QCNetworkMemoryCache.h>
#include <QCNetworkMiddleware.h>
#include <QCNetworkMockHandler.h>
#include <QCNetworkBody.h>
#include <QCNetworkMultipartBody.h>
#include <QCMultipartFormData.h>
#include <QCNetworkProxyConfig.h>
#include <QCNetworkResumableDownloadJob.h>
#include <QCNetworkRequest.h>
#include <QCNetworkReply.h>
#include <QCNetworkRetryPolicy.h>
#include <QCNetworkRequestScheduler.h>
#include <QCNetworkSslConfig.h>
#include <QCNetworkTimeoutConfig.h>
#include <QBuffer>
#include <QCoreApplication>
#include <QDateTime>
#include <QElapsedTimer>
#include <QJsonObject>
#include <QList>
#include <QPair>
#include <QUrl>

#include <chrono>
#include <optional>

static std::optional<QByteArray> findHeaderValue(
    const QList<QPair<QByteArray, QByteArray>> &headers,
    const QByteArray &nameLower)
{
    for (const auto &header : headers) {
        if (header.first.trimmed().toLower() == nameLower) {
            return header.second;
        }
    }
    return std::nullopt;
}

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

/// 验证 QCNetworkMiddleware 可由安装头声明并实例化的 smoke 类型。
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

    request.setCachePolicy(QCurl::QCNetworkCachePolicy::OnlyNetwork);
    if (request.cachePolicy() != QCurl::QCNetworkCachePolicy::OnlyNetwork) {
        return 8;
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
    QCurl::QCNetworkRequest bodyRequest(request.url());
    QCurl::QCNetworkMockHandler bodyMockHandler;
    bodyMockHandler.setCaptureEnabled(true);
    bodyMockHandler.setCaptureBodyPreviewLimit(256);
    bodyMockHandler.mockResponse(QCurl::HttpMethod::Post,
                                 bodyRequest.url(),
                                 QByteArrayLiteral("body-ok"));
    manager.setMockHandler(&bodyMockHandler);
    auto *bodyReply = manager.sendPost(bodyRequest, formBody);
    if (bodyReply == nullptr) {
        return 10;
    }
    QElapsedTimer bodyReplyTimer;
    bodyReplyTimer.start();
    while (!bodyReply->isFinished() && bodyReplyTimer.elapsed() < 2000) {
        QCoreApplication::processEvents();
    }
    if (!bodyReply->isFinished()) {
        bodyReply->deleteLater();
        return 10;
    }
    const auto bodyCaptured = bodyMockHandler.takeCapturedRequests();
    const auto bodyContentType =
        bodyCaptured.size() == 1
            ? findHeaderValue(bodyCaptured.first().headers(), QByteArrayLiteral("content-type"))
            : std::optional<QByteArray>{};
    const bool bodyOverloadValid =
        bodyReply->error() == QCurl::NetworkError::NoError && bodyCaptured.size() == 1
        && bodyCaptured.first().method() == QCurl::HttpMethod::Post
        && bodyCaptured.first().bodyPreview()
               == QByteArrayLiteral("name=core%20value&name=second%20value")
        && bodyContentType.has_value()
        && bodyContentType.value() == QByteArrayLiteral("application/x-www-form-urlencoded");
    bodyReply->deleteLater();
    manager.setMockHandler(nullptr);
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

    const int cancelledPending = ownerThreadScheduler->cancelLaneRequests(
        QStringLiteral("Control"), QCurl::QCNetworkRequestScheduler::CancelLaneScope::PendingOnly);
    const int cancelledAll = ownerThreadScheduler->cancelLaneRequests(
        QStringLiteral("Control"),
        QCurl::QCNetworkRequestScheduler::CancelLaneScope::PendingAndRunning);

    ConsumerSmokeLogger logger;
    manager.setLogger(&logger);
    if (manager.logger() != &logger) {
        return 15;
    }

    manager.setDebugTraceEnabled(true);
    if (!manager.debugTraceEnabled()) {
        return 16;
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
        return 17;
    }

    logger.log(entry);
    if (logger.count != 1 || logger.lastEntry.category() != QStringLiteral("ConsumerSmoke")
        || logger.lastEntry.message() != QStringLiteral("manager logger contract")) {
        return 18;
    }

    QCurl::QCNetworkDefaultLogger defaultLogger;
    defaultLogger.enableConsoleOutput(false);
    defaultLogger.setMinLogLevel(QCurl::NetworkLogLevel::Warning);
    defaultLogger.clear();
    manager.setLogger(&defaultLogger);
    if (manager.logger() != &defaultLogger
        || defaultLogger.minLogLevel() != QCurl::NetworkLogLevel::Warning) {
        return 19;
    }

    defaultLogger.log(entry);
    if (defaultLogger.entries().size() != 1) {
        return 20;
    }

    QCurl::QCNetworkCancelToken cancelToken;
    QCurl::QCNetworkReply *replyToCancel = nullptr;
    QList<QCurl::QCNetworkReply *> repliesToCancel;
    cancelToken.attach(replyToCancel);
    cancelToken.attachMultiple(repliesToCancel);
    cancelToken.setAutoTimeout(0);
    if (cancelToken.attachedCount() != 0 || cancelToken.isCancelled()) {
        return 21;
    }

    cancelToken.cancel();
    if (!cancelToken.isCancelled()) {
        return 22;
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
        return 23;
    }

    auto *poolManager = QCurl::QCNetworkConnectionPoolManager::instance();
    poolManager->setConfig(poolConfig);
    const auto savedPoolConfig = poolManager->config();
    if (savedPoolConfig.maxConnectionsPerHost() != 4
        || savedPoolConfig.maxTotalConnections() != 12) {
        return 24;
    }

    const auto poolStats = poolManager->statistics();
    if (poolStats.totalRequests() < 0 || poolStats.reusedConnections() < 0
        || poolStats.reuseRate() < 0.0 || poolStats.activeConnections() < 0
        || poolStats.idleConnections() < 0) {
        return 25;
    }
    poolManager->setConfig(QCurl::QCNetworkConnectionPoolConfig());

    ConsumerSmokeMiddleware middleware;
    manager.addMiddleware(&middleware);
    if (manager.middlewares().size() != 1 || manager.middlewares().first() != &middleware
        || middleware.name() != QStringLiteral("ConsumerSmokeMiddleware")) {
        return 26;
    }
    manager.removeMiddleware(&middleware);
    if (!manager.middlewares().isEmpty()) {
        return 27;
    }

    QCurl::QCNetworkMockHandler mockHandler;
    mockHandler.setCaptureEnabled(true);
    mockHandler.setCaptureBodyPreviewLimit(5);
    QCurl::QCNetworkCapturedRequest capturedRequest;
    capturedRequest.setUrl(request.url());
    capturedRequest.setMethod(QCurl::HttpMethod::Post);
    capturedRequest.addHeader(QByteArrayLiteral("X-Test"), QByteArrayLiteral("mock"));
    capturedRequest.setBodySize(7);
    capturedRequest.setBodyPreview(QByteArrayLiteral("payload").left(5));
    mockHandler.recordRequest(capturedRequest);

    const auto capturedRequests = mockHandler.takeCapturedRequests();
    if (capturedRequests.size() != 1 || capturedRequests.first().url() != request.url()
        || capturedRequests.first().method() != QCurl::HttpMethod::Post
        || capturedRequests.first().headers().size() != 1
        || capturedRequests.first().bodySize() != 7
        || capturedRequests.first().bodyPreview() != QByteArrayLiteral("paylo")) {
        return 28;
    }

    mockHandler.mockResponse(QCurl::HttpMethod::Get, request.url(), QByteArrayLiteral("mock-body"), 201);
    int mockStatus = 0;
    if (!mockHandler.hasMock(QCurl::HttpMethod::Get, request.url())
        || mockHandler.getMockResponse(QCurl::HttpMethod::Get, request.url(), mockStatus) != QByteArrayLiteral("mock-body")
        || mockStatus != 201) {
        return 29;
    }
    manager.setMockHandler(&mockHandler);
    if (manager.mockHandler() != &mockHandler) {
        return 30;
    }
    manager.setMockHandler(nullptr);

    manager.setLogger(nullptr);
    manager.setDebugTraceEnabled(false);

    return (cancelledPending >= 0 && cancelledAll >= 0) ? 0 : 31;
}
