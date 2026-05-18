"""Consumer smoke fixture validators for the public API gate."""

from __future__ import annotations

from pathlib import Path
import re


def _fixture_source(source_dir: Path) -> str:
    fixture = source_dir / "main.cpp"
    if not fixture.is_file():
        raise RuntimeError(f"missing consumer smoke fixture source: {fixture}")
    return fixture.read_text(encoding="utf-8")


def _require_snippets(source: str, snippets: list[str], message: str) -> None:
    missing = [snippet for snippet in snippets if snippet not in source]
    if missing:
        raise RuntimeError(message + ": " + ", ".join(missing))


def _find_direct_field_usage(source: str, field_names: list[str]) -> list[str]:
    violations: list[str] = []
    for field_name in field_names:
        pattern = re.compile(rf"\.\s*{field_name}\b(?!\s*\()")
        if pattern.search(source):
            violations.append(field_name)
    return sorted(set(violations))


def _find_present_snippets(source: str, snippets: list[str]) -> list[str]:
    return [snippet for snippet in snippets if snippet in source]


def validate_scheduler_core_contract_fixture(source_dir: Path) -> None:
    """Ensure consumer smoke keeps scheduler Core contract coverage."""

    source = _fixture_source(source_dir)
    _require_snippets(
        source,
        [
            "#include <QCNetworkRequestScheduler.h>",
            "#include <QCNetworkProxyConfig.h>",
            "#include <QCNetworkRetryPolicy.h>",
            "#include <QCNetworkSslConfig.h>",
            "#include <QCNetworkTimeoutConfig.h>",
            "manager.scheduler()",
            "manager.schedulerOnOwnerThread()",
            ".setMaxConcurrentRequests(",
            ".maxConcurrentRequests()",
            ".setWeight(",
            ".weight()",
            "QCNetworkProxyConfig::ProxyTlsConfig",
            ".setTlsConfig({})",
            ".setTlsConfig(",
            ".clearTlsConfig(",
            ".setPinnedPublicKey(",
            ".setConnectTimeout(",
            ".setTotalTimeout(",
            ".setRetryHttpStatusErrorsForGetOnly(",
            ".setHttpAuth(",
            ".httpAuth()",
            "QCurl::QCNetworkAccessManager::ShareHandleConfig shareConfig",
            "shareConfig.setShareDnsCache(true)",
            "shareConfig.setShareCookies(true)",
            "shareConfig.setShareSslSession(true)",
            "manager.setShareHandleConfig(shareConfig)",
            "manager.shareHandleConfig()",
            "savedShareConfig.shareDnsCache()",
            "savedShareConfig.shareCookies()",
            "savedShareConfig.shareSslSession()",
            "QCurl::QCNetworkAccessManager::HstsAltSvcCacheConfig cacheConfig",
            "cacheConfig.setHstsFilePath(",
            "cacheConfig.setAltSvcFilePath(",
            "manager.setHstsAltSvcCacheConfig(cacheConfig)",
            "manager.hstsAltSvcCacheConfig()",
            "savedCacheConfig.hstsFilePath()",
            "savedCacheConfig.altSvcFilePath()",
        ],
        "consumer smoke fixture is missing required scheduler contract coverage",
    )

    fallback_pattern = re.compile(
        r"=\s*[A-Za-z_][A-Za-z_0-9]*\s*!=\s*nullptr\s*\?\s*[A-Za-z_][A-Za-z_0-9]*\s*:\s*[A-Za-z_][A-Za-z_0-9]*\s*;"
    )
    if fallback_pattern.search(source):
        raise RuntimeError(
            "consumer smoke fixture must use explicit scheduler contract assertions, "
            "not ternary fallback logic"
        )

    violations = _find_direct_field_usage(
        source,
        [
            "maxConcurrentRequests",
            "maxRequestsPerHost",
            "maxBandwidthBytesPerSec",
            "enableThrottling",
            "weight",
            "quantum",
            "reservedGlobal",
            "reservedPerHost",
            "shareDnsCache",
            "shareCookies",
            "shareSslSession",
            "hstsFilePath",
            "altSvcFilePath",
        ],
    )
    if violations:
        raise RuntimeError(
            "consumer smoke fixture must use accessor API only; found direct field usage: "
            + ", ".join(violations)
        )


def validate_logger_core_contract_fixture(source_dir: Path) -> None:
    """Ensure consumer smoke keeps logger Core contract coverage."""

    source = _fixture_source(source_dir)
    _require_snippets(
        source,
        [
            "#include <QCNetworkLogger.h>",
            "class ConsumerSmokeLogger : public QCurl::QCNetworkLogger",
            "QCurl::NetworkLogEntry entry(",
            "manager.setLogger(&logger)",
            "manager.logger()",
            "manager.setDebugTraceEnabled(true)",
            "manager.debugTraceEnabled()",
            "entry.level()",
            "entry.category()",
            "entry.message()",
            "entry.timestampUtc()",
        ],
        "consumer smoke fixture is missing required logger contract coverage",
    )

    violations = _find_direct_field_usage(source, ["level", "category", "message", "timestampUtc"])
    if violations:
        raise RuntimeError(
            "consumer smoke fixture must use NetworkLogEntry accessor API only; found direct field usage: "
            + ", ".join(violations)
        )


def validate_default_logger_core_contract_fixture(source_dir: Path) -> None:
    """Ensure consumer smoke keeps DefaultLogger Core helper coverage."""

    _require_snippets(
        _fixture_source(source_dir),
        [
            "#include <QCNetworkDefaultLogger.h>",
            "QCurl::QCNetworkDefaultLogger defaultLogger",
            "defaultLogger.enableConsoleOutput(false)",
            "defaultLogger.setMinLogLevel(QCurl::NetworkLogLevel::Warning)",
            "manager.setLogger(&defaultLogger)",
            "defaultLogger.entries()",
        ],
        "consumer smoke fixture is missing required DefaultLogger contract coverage",
    )


def validate_cancel_token_core_contract_fixture(source_dir: Path) -> None:
    """Ensure consumer smoke keeps CancelToken reply-level coverage."""

    _require_snippets(
        _fixture_source(source_dir),
        [
            "#include <QCNetworkCancelToken.h>",
            "QCurl::QCNetworkCancelToken cancelToken",
            "QCurl::QCNetworkReply *replyToCancel",
            "QList<QCurl::QCNetworkReply *> repliesToCancel",
            "cancelToken.attach(replyToCancel)",
            "cancelToken.attachMultiple(repliesToCancel)",
            "cancelToken.setAutoTimeout(0)",
            "cancelToken.cancel()",
            "cancelToken.isCancelled()",
        ],
        "consumer smoke fixture is missing required CancelToken contract coverage",
    )


def validate_cache_policy_core_contract_fixture(source_dir: Path) -> None:
    """Ensure consumer smoke keeps CachePolicy Core type coverage."""

    _require_snippets(
        _fixture_source(source_dir),
        [
            "#include <QCNetworkCachePolicy.h>",
            "request.setCachePolicy(QCurl::QCNetworkCachePolicy::OnlyNetwork)",
            "request.cachePolicy()",
        ],
        "consumer smoke fixture is missing required CachePolicy contract coverage",
    )


def validate_cache_core_contract_fixture(source_dir: Path) -> None:
    """Ensure consumer smoke keeps concrete Cache Core coverage."""

    source = _fixture_source(source_dir)
    _require_snippets(
        source,
        [
            "#include <QCNetworkCache.h>",
            "#include <QCNetworkMemoryCache.h>",
            "#include <QCNetworkDiskCache.h>",
            "QCurl::QCNetworkMemoryCache memoryCache",
            "QCurl::QCNetworkCache *cacheInterface",
            "QCurl::QCNetworkCacheMetadata cacheMetadata",
            "cacheMetadata.setUrl(request.url())",
            "cacheMetadata.setHeader(QByteArrayLiteral(\"Content-Type\")",
            "cacheInterface->insert(request.url(), cacheBody, cacheMetadata)",
            "cacheInterface->lookup(request.url()",
            "cacheLookup.status()",
            "cacheLookup.metadata().url()",
            "cacheLookup.body()",
            "QCurl::QCNetworkDiskCache *diskCacheTypeProbe",
        ],
        "consumer smoke fixture is missing required Cache contract coverage",
    )
    present = _find_present_snippets(
        source,
        [
            ".status =",
            ".metadata =",
            ".body =",
            ".url =",
            ".headers =",
            ".expirationDate =",
            ".creationDate =",
            ".lastModified =",
        ],
    )
    if present:
        raise RuntimeError("consumer smoke fixture must use Cache accessor API only; found: " + ", ".join(present))


def validate_multipart_core_contract_fixture(source_dir: Path) -> None:
    """Ensure consumer smoke keeps Multipart Core builder coverage."""

    _require_snippets(
        _fixture_source(source_dir),
        [
            "#include <QCMultipartFormData.h>",
            "#include <QCNetworkBody.h>",
            "#include <QCNetworkMultipartBody.h>",
            "#include <QCNetworkDownloadToDeviceJob.h>",
            "#include <QCNetworkResumableDownloadJob.h>",
            "QCurl::QCMultipartFormData formData",
            "if (!formData.setBoundary(QStringLiteral(\"----QCurlConsumerSmokeBoundary\")))",
            "formData.addTextField(QStringLiteral(\"name\")",
            "formData.addFileField(QStringLiteral(\"file\")",
            "formData.contentType()",
            "formData.size()",
            "formData.toByteArray()",
            "formData.fieldCount()",
            "QCurl::QCNetworkBody::fromJson(",
            "QCurl::QCNetworkBody::fromFormUrlEncoded(",
            "manager.sendPost(bodyRequest, formBody)",
            "bodyCaptured.first().bodyPreview()",
            "bodyContentType.value()",
            "QCurl::QCNetworkMultipartBody::fromFormData(formData)",
            "QCurl::QCNetworkMultipartBody::fromSingleFileDevice(",
            "singleFileMultipart.has_value()",
            "singleFileMultipart->takeDevice(&app, &multipartError)",
            "QCurl::QCNetworkDownloadToDeviceJob downloadJobTypeProbe",
            "downloadJobTypeProbe.start()",
            "QCurl::QCNetworkResumableDownloadJob resumableJobTypeProbe",
            "resumableJobTypeProbe.start()",
            "multipartBody.contentType()",
            "multipartBody.data()",
        ],
        "consumer smoke fixture is missing required body/multipart/job contract coverage",
    )


def validate_connection_pool_core_contract_fixture(source_dir: Path) -> None:
    """Ensure consumer smoke keeps ConnectionPool accessor-only Core coverage."""

    source = _fixture_source(source_dir)
    _require_snippets(
        source,
        [
            "#include <QCNetworkConnectionPoolConfig.h>",
            "#include <QCNetworkConnectionPoolManager.h>",
            "QCurl::QCNetworkConnectionPoolConfig poolConfig",
            "poolConfig.setMaxConnectionsPerHost(4)",
            "poolConfig.setMaxTotalConnections(12)",
            "poolConfig.setMaxIdleTime(45)",
            "poolConfig.setMaxConnectionLifetime(90)",
            "poolConfig.setMultiplexingEnabled(true)",
            "poolConfig.setDnsCacheEnabled(true)",
            "poolConfig.setDnsCacheTimeout(30)",
            "poolConfig.setMultiMaxTotalConnections(6)",
            "poolConfig.setMultiMaxHostConnections(2)",
            "poolConfig.setMultiMaxConcurrentStreams(8)",
            "poolConfig.setMultiMaxConnects(16)",
            "poolConfig.maxConnectionsPerHost()",
            "poolConfig.maxTotalConnections()",
            "poolConfig.multiMaxTotalConnections()",
            "QCurl::QCNetworkConnectionPoolManager::instance()",
            "poolManager->setConfig(poolConfig)",
            "poolManager->config()",
            "poolManager->statistics()",
            "poolStats.totalRequests()",
            "poolStats.reusedConnections()",
            "poolStats.reuseRate()",
            "poolStats.activeConnections()",
            "poolStats.idleConnections()",
        ],
        "consumer smoke fixture is missing required ConnectionPool contract coverage",
    )
    present = _find_present_snippets(
        source,
        [
            ".maxConnectionsPerHost =",
            ".maxTotalConnections =",
            ".maxIdleTime =",
            ".maxConnectionLifetime =",
            ".enablePipelining =",
            ".enableMultiplexing =",
            ".enableDnsCache =",
            ".dnsCacheTimeout =",
            ".enableConnectionWarming =",
            ".multiMaxTotalConnections =",
            ".multiMaxHostConnections =",
            ".multiMaxConcurrentStreams =",
            ".multiMaxConnects =",
            ".totalRequests =",
            ".reusedConnections =",
            ".reuseRate =",
            ".activeConnections =",
            ".idleConnections =",
        ],
    )
    if present:
        raise RuntimeError(
            "consumer smoke fixture must use ConnectionPool accessor API only; found: "
            + ", ".join(present)
        )


def validate_middleware_core_contract_fixture(source_dir: Path) -> None:
    """Ensure consumer smoke keeps Middleware Core base coverage."""

    _require_snippets(
        _fixture_source(source_dir),
        [
            "#include <QCNetworkMiddleware.h>",
            "class ConsumerSmokeMiddleware : public QCurl::QCNetworkMiddleware",
            "manager.addMiddleware(&middleware)",
            "manager.middlewares()",
            "manager.removeMiddleware(&middleware)",
            "middleware.name()",
        ],
        "consumer smoke fixture is missing required Middleware contract coverage",
    )


def validate_mock_handler_core_test_support_fixture(source_dir: Path) -> None:
    """Ensure consumer smoke keeps MockHandler Core Test Support coverage."""

    source = _fixture_source(source_dir)
    _require_snippets(
        source,
        [
            "#include <QCNetworkMockHandler.h>",
            "QCurl::QCNetworkMockHandler mockHandler",
            "QCurl::QCNetworkCapturedRequest capturedRequest",
            "capturedRequest.setUrl(request.url())",
            "capturedRequest.setMethod(QCurl::HttpMethod::Post)",
            "capturedRequest.addHeader(",
            "capturedRequest.setBodySize(",
            "capturedRequest.setBodyPreview(",
            "mockHandler.recordRequest(capturedRequest)",
            "mockHandler.takeCapturedRequests()",
            "capturedRequests.first().url()",
            "capturedRequests.first().method()",
            "capturedRequests.first().headers()",
            "capturedRequests.first().bodySize()",
            "capturedRequests.first().bodyPreview()",
            "mockHandler.mockResponse(",
            "mockHandler.hasMock(",
            "mockHandler.getMockResponse(",
            "manager.setMockHandler(&mockHandler)",
            "manager.mockHandler()",
        ],
        "consumer smoke fixture is missing required MockHandler contract coverage",
    )
    present = _find_present_snippets(
        source,
        [
            ".url =",
            ".method =",
            ".headers =",
            ".bodyPreview =",
            ".bodySize =",
            ".followLocation =",
            ".connectTimeoutMs =",
            ".totalTimeoutMs =",
        ],
    )
    if present:
        raise RuntimeError(
            "consumer smoke fixture must use CapturedRequest accessor API only; found: "
            + ", ".join(present)
        )
