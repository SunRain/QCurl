#include "contract_probes.h"

#include <QBuffer>
#include <QCMultipartFormData.h>
#include <QCNetworkAccessManager.h>
#include <QCNetworkBody.h>
#include <QCNetworkCache.h>
#include <QCNetworkCachePolicy.h>
#include <QCNetworkCacheRequestKey.h>
#include <QCNetworkCancelToken.h>
#include <QCNetworkConnectionPoolConfig.h>
#include <QCNetworkConnectionPoolManager.h>
#include <QCNetworkDefaultLogger.h>
#include <QCNetworkDiskCache.h>
#include <QCNetworkDownloadToDeviceJob.h>
#include <QCNetworkHttpMethod.h>
#include <QCNetworkLaneCancelResult.h>
#include <QCNetworkLaneKey.h>
#include <QCNetworkLogger.h>
#include <QCNetworkMemoryCache.h>
#include <QCNetworkMiddleware.h>
#include <QCNetworkMultipartBody.h>
#include <QCNetworkProxyConfig.h>
#include <QCNetworkReply.h>
#include <QCNetworkRequest.h>
#include <QCNetworkResumableDownloadJob.h>
#include <QCNetworkRetryPolicy.h>
#include <QCNetworkSchedulerPolicy.h>
#include <QCNetworkSslConfig.h>
#include <QCNetworkTimeoutConfig.h>
#include <QCoreApplication>
#include <QDateTime>
#include <QEventLoop>
#include <QJsonObject>
#include <QList>
#include <QPair>
#include <QTimer>
#include <QUrl>

#include <chrono>
#include <type_traits>
#include <utility>

static_assert(std::is_constructible_v<QCurl::QCNetworkRequest, const QUrl &>);
static_assert(!std::is_convertible_v<QUrl, QCurl::QCNetworkRequest>);
static_assert(
    std::is_same_v<decltype(std::declval<const QCurl::QCNetworkReply &>().diagnosticCurlCode()),
                   int>);
static_assert(noexcept(std::declval<const QCurl::QCNetworkReply &>().diagnosticCurlCode()));

static bool nativeDiagnosticContract(QCurl::QCNetworkAccessManager &manager)
{
    auto *reply = manager.get(QCurl::QCNetworkRequest(QUrl()));
    if (reply->diagnosticCurlCode() != 0) {
        return false;
    }
    QEventLoop loop;
    QObject::connect(reply, &QCurl::QCNetworkReply::finished, &loop, &QEventLoop::quit);
    QTimer::singleShot(1000, &loop, &QEventLoop::quit);
    loop.exec();
    return reply->isFinished() && reply->error() != QCurl::NetworkError::NoError
           && reply->diagnosticCurlCode() == 0;
}

class ConsumerSmokeLogger : public QCurl::QCNetworkLogger
{
public:
    int count = 0;
    QCurl::NetworkLogEntry lastEntry;

    QCurl::QCNetworkLogResult log(const QCurl::NetworkLogEntry &entry) override
    {
        ++count;
        lastEntry = entry;
        return {};
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

    if (const int result = runSchedulerProbe(); result != 0) {
        return result;
    }

    QCurl::QCNetworkAccessManager manager;
    if (!nativeDiagnosticContract(manager)) {
        return 30;
    }
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
    policy.setAdmissionByteBudget(0);

    QCurl::QCNetworkSchedulerPolicy::LaneConfig lane;
    lane.setWeight(3);
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

    QCurl::QCNetworkSchedulerPolicy::LaneConfig appliedLane;
    if (!appliedConfig.laneConfig(QCurl::QCNetworkLaneKey::control(), &appliedLane)) {
        return 3;
    }
    if (appliedLane.weight() < 1) {
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

    QCurl::QCNetworkRetryPolicy retryPolicy;
    if (QCurl::QCNetworkRetryPolicy::tryCreate(3, std::chrono::milliseconds(250), 2.0, &retryPolicy)
            != QCurl::QCNetworkRetryPolicy::UpdateResult::Applied
        || retryPolicy.setRetryMethodPolicy(
               QCurl::QCNetworkRetryMethodPolicy::AllowExplicitIdempotencyKey)
               != QCurl::QCNetworkRetryPolicy::UpdateResult::Applied) {
        return 7;
    }
    request.setRawHeader("Idempotency-Key", "consumer-stable-1");
    request.setRetryPolicy(retryPolicy);
    if (request.retryPolicy().maxRetries() != 3
        || request.retryPolicy().retryMethodPolicy()
               != QCurl::QCNetworkRetryMethodPolicy::AllowExplicitIdempotencyKey) {
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
    QCurl::QCNetworkCacheRequestKey cacheKey(QCurl::HttpMethod::Get, request.url());
    cacheInterface->insert(cacheKey, cacheBody, cacheMetadata);

    const auto cacheLookup = cacheInterface->lookup(cacheKey,
                                                    QCurl::QCNetworkCacheReadMode::FreshOnly);
    if (cacheLookup.status() != QCurl::QCNetworkCacheLookupStatus::FreshHit
        || cacheLookup.metadata().url() != request.url() || cacheLookup.body() != cacheBody) {
        return 9;
    }
    const QCurl::QCNetworkCacheClearResult cacheClear = cacheInterface->clear();
    if (cacheClear.status() != QCurl::QCNetworkCacheClearResult::Status::Success
        || cacheClear.removedCount() != 1 || cacheClear.failedCount() != 0
        || cacheClear.remainingBytes() != 0
        || cacheClear.errorCode() != QCurl::QCNetworkCacheClearResult::ErrorCode::None
        || !cacheClear.errorMessage().isEmpty()) {
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
        || formData.size() <= 0 || !formData.toByteArray().contains(QByteArrayLiteral("payload"))) {
        return 10;
    }
    const QCurl::QCNetworkBody jsonBody = QCurl::QCNetworkBody::fromJson(
        QJsonObject{{QStringLiteral("name"), QStringLiteral("core")}});
    const QCurl::QCNetworkBody formBody = QCurl::QCNetworkBody::fromFormUrlEncoded(
        QList<QPair<QString, QString>>{{QStringLiteral("name"), QStringLiteral("core value")},
                                       {QStringLiteral("name"), QStringLiteral("second value")}});
    const bool bodyOverloadValid = jsonBody.contentType() == QByteArrayLiteral("application/json")
                                   && jsonBody.data().contains(QByteArrayLiteral("core"))
                                   && formBody.contentType()
                                          == QByteArrayLiteral("application/x-www-form-urlencoded")
                                   && formBody.data().contains(QByteArrayLiteral("core%20value"))
                                   && formBody.data().contains(
                                       QByteArrayLiteral("name=second%20value"));
    const auto multipartBody     = QCurl::QCNetworkMultipartBody::fromFormData(formData);
    QByteArray singleFileBytes   = QByteArrayLiteral("single-file-payload");
    QBuffer singleFileDevice(&singleFileBytes);
    singleFileDevice.open(QIODevice::ReadOnly);
    QString multipartError;
    auto singleFileMultipart
        = QCurl::QCNetworkMultipartBody::fromSingleFileDevice(&singleFileDevice,
                                                              QStringLiteral("file"),
                                                              QStringLiteral("payload.bin"),
                                                              QStringLiteral(
                                                                  "application/octet-stream"),
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
    QCurl::QCNetworkResumableDownloadJob
        resumableJobTypeProbe(&manager,
                              request,
                              QStringLiteral("/tmp/qcurl-consumer-smoke-resumable.bin"));
    resumableJobTypeProbe.start();
    if (jsonBody.contentType() != QByteArrayLiteral("application/json")
        || !jsonBody.data().contains(QByteArrayLiteral("core"))
        || formBody.contentType() != QByteArrayLiteral("application/x-www-form-urlencoded")
        || !formBody.data().contains(QByteArrayLiteral("core%20value"))
        || !formBody.data().contains(QByteArrayLiteral("name=second%20value")) || !bodyOverloadValid
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
        || savedCacheConfig.altSvcFilePath() != QStringLiteral("/tmp/qcurl-consumer-altsvc.txt")) {
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

    ConsumerSmokeLogger *consumerLogger = nullptr;
    auto logger = QCurl::QCNetworkLoggerHandle::createWithBorrow(&consumerLogger);
    manager.setLogger(logger);
    if (manager.logger() != logger) {
        return 16;
    }

    manager.setDebugTraceEnabled(true);
    if (!manager.debugTraceEnabled()) {
        return 17;
    }

    const QDateTime timestampUtc = QDateTime::currentDateTimeUtc();
    QCurl::NetworkLogEntry entry(QCurl::NetworkLogLevel::Warning,
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

    if (!logger->log(entry).isSuccess()) {
        return 19;
    }
    if (consumerLogger->count != 1
        || consumerLogger->lastEntry.category() != QStringLiteral("ConsumerSmoke")
        || consumerLogger->lastEntry.message() != QStringLiteral("manager logger contract")) {
        return 19;
    }

    QCurl::QCNetworkDefaultLogger *defaultLoggerImplementation = nullptr;
    auto defaultLogger = QCurl::QCNetworkLoggerHandle::createWithBorrow(
        &defaultLoggerImplementation);
    defaultLoggerImplementation->enableConsoleOutput(false);
    defaultLoggerImplementation->setMinLogLevel(QCurl::NetworkLogLevel::Warning);
    defaultLoggerImplementation->clear();
    manager.setLogger(defaultLogger);
    if (manager.logger() != defaultLogger
        || defaultLoggerImplementation->minLogLevel() != QCurl::NetworkLogLevel::Warning) {
        return 20;
    }

    if (!defaultLogger->log(entry).isSuccess()
        || defaultLoggerImplementation->entries().size() != 1) {
        return 21;
    }

    QCurl::QCNetworkCancelToken cancelToken;
    QCurl::QCNetworkReply *replyToCancel = nullptr;
    QList<QCurl::QCNetworkReply *> repliesToCancel;
    const auto nullAttachResult   = cancelToken.attach(replyToCancel);
    const auto batchAttachResults = cancelToken.attachMultiple(repliesToCancel);
    const auto timeoutResult      = cancelToken.setAutoTimeout(0);
    if (nullAttachResult != QCurl::QCNetworkCancelToken::AttachResult::NullReply
        || !batchAttachResults.isEmpty()
        || timeoutResult != QCurl::QCNetworkCancelToken::CommandResult::NoChange
        || cancelToken.attachedCount() != 0 || cancelToken.isCancelled()) {
        return 22;
    }

    if (cancelToken.cancel() != QCurl::QCNetworkCancelToken::CommandResult::Applied
        || !cancelToken.isCancelled()) {
        return 23;
    }

    QCurl::QCNetworkReply *replyForDeleteLater = nullptr;
    if (replyForDeleteLater) {
        replyForDeleteLater->deleteLater();
    }
    const QMetaObject &replyMeta = QCurl::QCNetworkReply::staticMetaObject;
    const int deleteLaterIndex   = replyMeta.indexOfSlot("deleteLater()");
    if (deleteLaterIndex < 0 || deleteLaterIndex >= replyMeta.methodOffset()) {
        return 24;
    }

    QCurl::QCNetworkConnectionPoolConfig poolConfig;
    poolConfig.setMaxIdleTime(45);
    poolConfig.setMaxConnectionLifetime(90);
    poolConfig.setMultiplexingEnabled(true);
    poolConfig.setDnsCacheEnabled(true);
    poolConfig.setDnsCacheTimeout(30);
    poolConfig.setMultiMaxTotalConnections(6);
    poolConfig.setMultiMaxHostConnections(2);
    poolConfig.setMultiMaxConcurrentStreams(8);
    poolConfig.setMultiMaxConnects(16);
    if (!poolConfig.isValid() || poolConfig.maxIdleTime() != 45
        || poolConfig.maxConnectionLifetime() != 90 || !poolConfig.multiplexingEnabled()
        || !poolConfig.dnsCacheEnabled() || poolConfig.dnsCacheTimeout() != 30
        || poolConfig.multiMaxTotalConnections().value_or(-1) != 6
        || poolConfig.multiMaxHostConnections().value_or(-1) != 2
        || poolConfig.multiMaxConcurrentStreams().value_or(-1) != 8
        || poolConfig.multiMaxConnects().value_or(-1) != 16) {
        return 24;
    }

    auto *poolManager = QCurl::QCNetworkConnectionPoolManager::instance();
    if (poolManager->setConfig(poolConfig)
        != QCurl::QCNetworkConnectionPoolManager::UpdateResult::Applied) {
        return 25;
    }
    const auto savedPoolConfig = poolManager->config();
    if (savedPoolConfig.multiMaxHostConnections().value_or(0) != 2
        || savedPoolConfig.multiMaxTotalConnections().value_or(0) != 6) {
        return 25;
    }

    const auto poolStats = poolManager->statistics();
    if (poolStats.totalRequests() < 0 || poolStats.reusedConnections() < 0
        || poolStats.reuseRate() < 0.0 || poolStats.activeRequests() < 0) {
        return 26;
    }
    if (poolManager->setConfig(QCurl::QCNetworkConnectionPoolConfig())
        != QCurl::QCNetworkConnectionPoolManager::UpdateResult::Applied) {
        return 27;
    }

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

    manager.setLogger({});
    manager.setDebugTraceEnabled(false);
    return (!cancelledPending.isSuccess() && !cancelledAll.isSuccess()) ? 0 : 29;
}
