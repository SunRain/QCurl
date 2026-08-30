"""Explicit direct-field authorization manifest for QCurl internal types."""

from __future__ import annotations

from dataclasses import dataclass


@dataclass(frozen=True)
class DirectFieldAuthorization:
    """Records the reviewed reason an internal type exposes direct state fields."""

    relative_path: str
    type_name: str
    kind: str
    public_type: str | None = None


def _pimpl(
    relative_path: str, *type_names: str
) -> tuple[DirectFieldAuthorization, ...]:
    return tuple(
        DirectFieldAuthorization(
            relative_path=relative_path,
            type_name=type_name,
            kind="pimpl",
            public_type=type_name.removesuffix("Private"),
        )
        for type_name in type_names
    )


def _entries(
    kind: str, relative_path: str, *type_names: str
) -> tuple[DirectFieldAuthorization, ...]:
    return tuple(
        DirectFieldAuthorization(
            relative_path=relative_path, type_name=type_name, kind=kind
        )
        for type_name in type_names
    )


PIMPL_AUTHORIZATIONS = (
    *_pimpl("src/QCurlRuntime.cpp", "QCurlRuntimePrivate"),
    *_pimpl("src/QCNetworkAccessManager_p.h", "QCNetworkAccessManagerPrivate"),
    *_pimpl("src/QCNetworkCancelToken.cpp", "QCNetworkCancelTokenPrivate"),
    *_pimpl(
        "src/QCNetworkConnectionPoolManager.cpp",
        "QCNetworkConnectionPoolManagerPrivate",
    ),
    *_pimpl("src/QCNetworkDefaultLogger.cpp", "QCNetworkDefaultLoggerPrivate"),
    *_pimpl("src/QCNetworkDiskCache.cpp", "QCNetworkDiskCachePrivate"),
    *_pimpl(
        "src/QCNetworkDownloadToDeviceJob.cpp", "QCNetworkDownloadToDeviceJobPrivate"
    ),
    *_pimpl("src/QCNetworkLogger.cpp", "QCNetworkLoggerHandlePrivate"),
    *_pimpl("src/QCNetworkMemoryCache.cpp", "QCNetworkMemoryCachePrivate"),
    *_pimpl("src/QCNetworkMiddleware.cpp", "QCNetworkMiddlewarePrivate"),
    *_pimpl("src/QCNetworkMockHandler_p.h", "QCNetworkMockHandlerPrivate"),
    *_pimpl("src/QCNetworkMultipartBody.cpp", "QCNetworkMultipartBodyPrivate"),
    *_pimpl("src/QCNetworkReply_p.h", "QCNetworkReplyPrivate"),
    *_pimpl(
        "src/QCNetworkResumableDownloadJob.cpp", "QCNetworkResumableDownloadJobPrivate"
    ),
    *_pimpl("src/QCNetworkTransferJob.cpp", "QCNetworkTransferJobPrivate"),
    *_pimpl("src/QCWebSocket_p.h", "QCWebSocketPrivate"),
    *_pimpl("src/private/QCWebSocketPoolPrivate_p.h", "QCWebSocketPoolPrivate"),
)

RECORD_AUTHORIZATIONS = (
    *_entries("record", "src/QCMultipartFormData.cpp", "QCMultipartField"),
    *_entries("record", "src/QCNetworkAccessManager_p.h", "MiddlewareEntry"),
    *_entries(
        "record",
        "src/QCNetworkMockHandler_p.h",
        "QCNetworkMockSequence",
    ),
    *_entries(
        "record",
        "src/private/QCNetworkMockProvider_p.h",
        "QCNetworkMockData",
    ),
    *_entries(
        "record",
        "src/QCCurlMultiManager.h",
        "AddReplyResult",
        "FinishedTransfer",
    ),
    *_entries(
        "record",
        "src/private/QCCurlMultiManagerShareState_p.h",
        "QCCurlMultiManagerShareConfig",
        "QCCurlMultiManagerShareContext",
    ),
    *_entries(
        "record",
        "src/private/QCCurlMultiManagerSocketInfo_p.h",
        "SocketInfo",
    ),
    *_entries(
        "record",
        "src/private/QCRequestPipeline_p.h",
        "RequestBody",
        "NormalizedRequest",
        "CurlPlan",
    ),
    *_entries(
        "record",
        "src/private/QCBlockingResponseSink_p.h",
        "QCBlockingHeaderSink",
    ),
    *_entries(
        "record",
        "src/private/QCBlockingCurlAdapter.cpp",
        "BlockingExecution",
    ),
    *_entries(
        "record",
        "src/private/QCBlockingCurlRequestSetup_p.h",
        "RequestOptionStorage",
    ),
    *_entries(
        "record",
        "src/private/QCNetworkResumableDownloadWriter.cpp",
        "ContentRangeInfo",
    ),
    *_entries(
        "record",
        "src/private/QCNetworkRequestSchedulerQueue_p.h",
        "ReplySnapshot",
        "ReplyOutcome",
        "FinalizeResult",
        "ReplyProgressState",
        "QueuedRequest",
    ),
)

SHARED_DATA_AUTHORIZATIONS = (
    *_entries(
        "shared_data",
        "src/QCBlockingCookieStore.cpp",
        "QCCookieSnapshotData",
        "QCCookieDeltaData",
    ),
    *_entries(
        "shared_data", "src/QCBlockingNetworkClient.cpp", "QCBlockingNetworkClientData"
    ),
    *_entries(
        "shared_data",
        "src/QCBlockingNetworkClientOptions.cpp",
        "QCBlockingNetworkClientOptionsData",
        "QCBlockingRequestOptionsData",
        "QCTransferProgressData",
    ),
    *_entries(
        "shared_data", "src/QCBlockingNetworkResult.cpp", "QCBlockingNetworkResultData"
    ),
    *_entries("shared_data", "src/QCCookie.cpp", "QCCookieData"),
    *_entries(
        "shared_data",
        "src/QCCookieAsyncResult.cpp",
        "QCCookieOperationResultData",
        "QCCookieExportResultData",
    ),
    *_entries("shared_data", "src/QCMultipartFormData.cpp", "QCMultipartFormDataData"),
    *_entries(
        "shared_data",
        "src/QCNetworkAccessManagerConfig.cpp",
        "ShareHandleConfigData",
        "HstsAltSvcCacheConfigData",
    ),
    *_entries(
        "shared_data", "src/QCNetworkCacheRequestKey.cpp", "QCNetworkCacheRequestKeyData"
    ),
    *_entries("shared_data", "src/QCNetworkBody.cpp", "QCNetworkBodyData"),
    *_entries(
        "shared_data",
        "src/QCNetworkCache.cpp",
        "QCNetworkCacheMetadataData",
        "QCNetworkCacheLookupResultData",
        "QCNetworkCacheClearResultData",
    ),
    *_entries(
        "shared_data",
        "src/QCNetworkCapturedRequest.cpp",
        "QCNetworkCapturedRequestData",
    ),
    *_entries(
        "shared_data",
        "src/QCNetworkConnectionPoolConfig.cpp",
        "QCNetworkConnectionPoolConfigData",
    ),
    *_entries(
        "shared_data",
        "src/QCNetworkConnectionPoolStatistics.cpp",
        "QCNetworkConnectionPoolStatisticsData",
    ),
    *_entries(
        "shared_data",
        "src/QCNetworkDiagnosticsValues.cpp",
        "QCNetworkDiagnosticsOptionsData",
        "DiagResultData",
    ),
    *_entries(
        "shared_data", "src/QCNetworkHttpAuthConfig.cpp", "QCNetworkHttpAuthConfigData"
    ),
    *_entries(
        "shared_data",
        "src/QCNetworkLaneCancelResult.cpp",
        "QCNetworkLaneCancelResultData",
    ),
    *_entries("shared_data", "src/QCNetworkLaneKey.cpp", "QCNetworkLaneKeyData"),
    *_entries(
        "shared_data",
        "src/QCNetworkLogger.cpp",
        "NetworkLogEntryData",
        "QCNetworkLogResultData",
    ),
    *_entries(
        "shared_data",
        "src/QCNetworkProxyConfig.cpp",
        "QCNetworkProxyTlsConfigData",
        "QCNetworkProxyConfigData",
    ),
    *_entries(
        "shared_data", "src/QCNetworkRedirectConfig.cpp", "QCNetworkRedirectConfigData"
    ),
    *_entries(
        "shared_data",
        "src/private/QCNetworkRequestPrivate_p.h",
        "QCNetworkRequestPrivate",
    ),
    *_entries(
        "shared_data", "src/QCNetworkRetryPolicy.cpp", "QCNetworkRetryPolicyData"
    ),
    *_entries(
        "shared_data",
        "src/QCNetworkSchedulerPolicy.cpp",
        "QCNetworkSchedulerStatisticsData",
        "QCNetworkSchedulerPolicyLaneConfigData",
        "QCNetworkSchedulerPolicyData",
    ),
    *_entries("shared_data", "src/QCNetworkSslConfig.cpp", "QCNetworkSslConfigData"),
    *_entries(
        "shared_data", "src/QCNetworkTimeoutConfig.cpp", "QCNetworkTimeoutConfigData"
    ),
    *_entries(
        "shared_data", "src/QCNetworkTransferConfig.cpp", "QCNetworkTransferConfigData"
    ),
    *_entries("shared_data", "src/QCWebSocketOptions.cpp", "QCWebSocketOptionsData"),
    *_entries(
        "shared_data",
        "src/QCWebSocketPoolValues.cpp",
        "QCWebSocketAcquireResultData",
        "QCWebSocketPreWarmResultData",
        "QCWebSocketPoolConfigData",
        "QCWebSocketPoolStatsData",
    ),
    *_entries(
        "shared_data",
        "src/QCWebSocketReconnectPolicy.cpp",
        "QCWebSocketReconnectPolicyData",
    ),
    *_entries(
        "shared_data",
        "src/private/QCNetworkRequestSchedulerValues.cpp",
        "QCNetworkRequestSchedulerConfigData",
        "QCNetworkRequestSchedulerStatisticsData",
        "QCNetworkRequestSchedulerLaneConfigData",
    ),
)

DIRECT_FIELD_AUTHORIZATIONS = (
    *PIMPL_AUTHORIZATIONS,
    *RECORD_AUTHORIZATIONS,
    *SHARED_DATA_AUTHORIZATIONS,
)
