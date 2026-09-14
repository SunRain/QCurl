# Core feature implementation and header inventory.

set(QCURL_FEATURE_SOURCES
    QCCurlHandleManager.cpp
    CurlFeatureProbe.cpp
    private/QCRequestPipeline.cpp
    private/QCNetworkReplyBodySource.cpp
    private/QCNetworkReplyCallbacks.cpp
    private/QCNetworkReplyFlowControl.cpp
    private/QCNetworkReplyResponse.cpp
    private/QCNetworkReplyRuntime.cpp
    private/QCNetworkRetryDecision.cpp
    private/QCNetworkReplyCurlOptions.cpp
    private/QCNetworkReplyCurlBaseOptions.cpp
    private/QCNetworkReplyCurlMethodOptions.cpp
    private/QCNetworkReplyCurlProxyOptions.cpp
    private/QCNetworkReplyCurlProxyTlsOptions.cpp
    private/QCNetworkReplyCurlHttpOptions.cpp
    private/QCNetworkReplyCurlTlsOptions.cpp
    private/QCNetworkReplyCurlRuntimeOptions.cpp
    private/QCNetworkProtocolPolicy.cpp
    private/QCNetworkReplyExecution.cpp
    private/QCNetworkReplyExecutionSetup.cpp
    private/QCNetworkReplyMockChaos.cpp
    private/QCNetworkReplyMockExecution.cpp
    private/QCNetworkReplyState.cpp
    private/QCNetworkReplyControls.cpp
    private/QCNetworkReplyAccessors.cpp
    private/QCNetworkAdmissionCore.cpp
    private/QCNetworkAdmissionCoreState.cpp
    private/QCNetworkAdmissionCoreSelection.cpp
    private/QCNetworkRequestSchedulerDispatch.cpp
    private/QCNetworkRequestSchedulerProgress.cpp
    private/QCNetworkRequestSchedulerControl.cpp
    private/QCNetworkRequestSchedulerQueries.cpp
    private/QCNetworkRequestSchedulerLifecycle.cpp
    private/QCNetworkRequestSchedulerFinalize.cpp
    private/QCSingleFileMultipartBodyDevice.cpp
    QCNetworkBody.cpp
    QCNetworkMultipartBody.cpp
    QCNetworkSslConfig.cpp
    QCNetworkProxyConfig.cpp
    QCNetworkTimeoutConfig.cpp
    QCNetworkHttpVersion.cpp
    QCNetworkError.cpp
    QCNetworkReply.cpp
    QCCurlMultiManager.cpp
    private/QCCurlMultiTransferRecord.cpp
    private/QCCurlMultiManagerLimits.cpp
    private/QCCurlMultiManagerEventDriver.cpp
    private/QCCurlMultiManagerTransfers.cpp
    private/QCCurlMultiManagerTest.cpp
    private/QCCurlMultiManagerShare.cpp
    private/QCCurlMultiManagerShareLifecycle.cpp
    private/QCCurlMultiManagerShareTest.cpp
    private/QCCurlMultiManagerCookieContext.cpp
    private/QCCurlMultiManagerCookies.cpp
    private/QCCurlMultiManagerCookieExport.cpp
    private/QCCurlRequiredOptionAdapter.cpp
    private/QCCookieStoreCodec.cpp
    QCNetworkRetryPolicy.cpp
    private/QCNetworkRequestScheduler.cpp
    QCMultipartFormData.cpp         # Core / Stable: Multipart 表单数据
    QCNetworkCache.cpp              # Core / Stable: 缓存基类
    QCNetworkCacheResults.cpp
    QCNetworkCacheRequestKey.cpp    # Core / Stable: 结构化缓存请求键
    QCNetworkMemoryCache.cpp        # Core / Stable: 内存缓存
    QCNetworkDiskCache.cpp          # Core / Stable: 磁盘缓存
    private/QCNetworkDiskCacheEntry.cpp
    private/QCNetworkDiskCacheEntryRead.cpp
    private/QCNetworkCacheIntegration.cpp
    private/QCNetworkReplyCache.cpp
    QCNetworkConnectionPoolConfig.cpp
    QCNetworkConnectionPoolStatistics.cpp
    QCNetworkConnectionPoolManager.cpp  # Core / Stable: 连接池管理器
    QCNetworkLogger.cpp             # Core / Stable: 日志系统
    QCNetworkDefaultLogger.cpp      # Core / Stable: 默认日志实现
    QCNetworkLogRedaction.cpp       # Core / Stable: 日志脱敏工具
    QCNetworkMiddleware.cpp         # Core / Stable: 中间件 base
    QCNetworkCancelToken.cpp        # Core / Stable: 取消令牌
    QCNetworkTransferJob.cpp
    QCNetworkDownloadToDeviceJob.cpp
    QCNetworkResumableDownloadJob.cpp
    private/QCNetworkResumableDownloadWriter.cpp
)

set(QCURL_FEATURE_HEADERS
    QCGlobal.h
    QCBlockingNetworkClient.h
    QCBlockingNetworkResult.h
    private/QCBlockingCurlAdapter_p.h
    private/QCBlockingCurlMethodSetup_p.h
    private/QCBlockingResponseSink_p.h
    private/QCBlockingCurlRequestSetup_p.h
    private/QCBlockingRequestBody_p.h
    QCCurlHandleManager.h
    CurlFeatureProbe.h
    private/QCMultipartHeaderEncoding_p.h
    private/QCNetworkReplyBodySource_p.h
    private/QCNetworkReplyCallbacks_p.h
    private/QCNetworkReplyExecution_p.h
    private/QCNetworkReplyFlowControl_p.h
    private/QCNetworkReplyMockChaos_p.h
    private/QCNetworkReplyResponse_p.h
    private/QCNetworkReplyRuntime_p.h
    private/QCHttpDate_p.h
    private/QCNetworkReplySignal_p.h
    private/QCNetworkReplyTransferState_p.h
    private/QCNetworkRetryDecision_p.h
    private/QCNetworkRetryPolicy_p.h
    private/QCNetworkProtocolPolicy_p.h
    private/QCNetworkReplyCurlOptions_p.h
    private/QCNetworkAdmissionCore_p.h
    private/QCSingleFileMultipartBodyDevice.h
    QCNetworkBody.h
    QCNetworkMultipartBody.h
    QCNetworkSslConfig.h
    QCNetworkProxyConfig.h
    QCNetworkTimeoutConfig.h
    QCNetworkHttpMethod.h
    QCNetworkHttpVersion.h
    QCNetworkLaneKey.h
    QCNetworkLaneCancelResult.h
    QCNetworkSchedulerPolicy.h
    QCNetworkError.h
    QCNetworkReply.h
    QCNetworkReply_p.h
    private/QCCurlMultiManagerSocketInfo_p.h
    private/QCCurlMultiManagerShareState_p.h
    private/QCCurlMultiManagerTestAccess_p.h
    private/QCCurlPersistentTransferBridge_p.h
    private/QCCurlMultiTransferRecord_p.h
    private/QCCookieStoreResult_p.h
    private/QCCurlRequiredOptionAdapter_p.h
    private/QCCookieStoreCodec_p.h
    QCCurlMultiManager.h
    QCNetworkRetryPolicy.h
    QCNetworkRequestPriority.h      # Core / Stable: 优先级枚举
    private/QCNetworkRequestScheduler_p.h
    QCMultipartFormData.h           # Core / Stable: Multipart 表单数据
    QCNetworkCachePolicy.h          # Core / Stable: 缓存策略枚举
    QCNetworkCache.h                # Core / Stable: 缓存基类
    QCNetworkCacheRequestKey.h      # Core / Stable: 结构化缓存请求键
    QCNetworkMemoryCache.h          # Core / Stable: 内存缓存
    QCNetworkDiskCache.h            # Core / Stable: 磁盘缓存
    QCNetworkConnectionPoolConfig.h # Core / Stable: 连接池配置
    QCNetworkConnectionPoolManager.h # Core / Stable: 连接池管理器
    QCNetworkConnectionPoolManager_p.h # Core / Stable: 连接池内部 companion
    QCNetworkLogger.h               # Core / Stable: 日志系统
    QCNetworkDefaultLogger.h        # Core / Stable: 默认日志实现
    private/QCNetworkLogRedaction_p.h # Core / Stable: 日志脱敏工具（内部）
    QCNetworkMiddleware.h           # Core / Stable: 中间件 base
    QCNetworkMiddlewareExtras.h     # Middleware Extras opt-in surface
    private/QCNetworkMiddlewareInternal_p.h # Core / Stable: 内部策略型 middleware
    QCNetworkCancelToken.h          # Core / Stable: 取消令牌
    QCNetworkTransferJob.h
    QCNetworkDownloadToDeviceJob.h
    QCNetworkResumableDownloadJob.h
    QCNetworkMockHandler.h          # Test Support / Explicit opt-in: Mock 工具
    QCNetworkTestSupport.h          # Test Support / Explicit opt-in: 显式 manager 绑定
    QCNetworkMockHandler_p.h        # Test Support / Explicit opt-in: Mock companion（内部）
    private/QCNetworkResumableDownloadWriter_p.h
    QCNetworkDiagnostics.h          # Other Extras（Preview）: 网络诊断工具
)
