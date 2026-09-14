# 默认离线门禁（不包含任何源码内显式 QSKIP 的测试）
set_tests_properties(tst_QCNetworkRequest PROPERTIES LABELS "offline")
set_tests_properties(tst_QCNetworkError PROPERTIES LABELS "offline")
set_tests_properties(tst_CurlFeatureProbe PROPERTIES LABELS "offline")
set_tests_properties(tst_CurlGlobalInitialization PROPERTIES LABELS "offline")
set_tests_properties(tst_QCurlRuntime PROPERTIES LABELS "offline")
set_tests_properties(tst_QCBlockingNetworkClient PROPERTIES LABELS "local_port")
set_tests_properties(tst_QCBlockingRequestConfig PROPERTIES LABELS "local_port")
set_tests_properties(tst_QCNetworkRetryOffline PROPERTIES LABELS "offline")
set_tests_properties(tst_QCNetworkResponseHeadersOffline PROPERTIES LABELS "offline")
set_tests_properties(tst_QCNetworkRequestWarningsOffline PROPERTIES LABELS "offline")
set_tests_properties(tst_QCNetworkHttpErrorMappingOffline PROPERTIES LABELS "offline")
set_tests_properties(tst_QCNetworkSchedulerPolicy PROPERTIES LABELS "offline")
set_tests_properties(tst_QCNetworkScheduler PROPERTIES LABELS "offline")
set_tests_properties(tst_QCNetworkActorThreadModel PROPERTIES LABELS "env;local_port")
set_tests_properties(tst_QCNetworkRequestCanonicalApi PROPERTIES LABELS "offline")
set_tests_properties(tst_QCNetworkRequestConfigCanonicalApi PROPERTIES LABELS "offline")
set_tests_properties(tst_QCNetworkBody PROPERTIES LABELS "offline")
set_tests_properties(tst_QCMultipartFormData PROPERTIES LABELS "offline")
set_tests_properties(tst_QCNetworkDownloadToDeviceJob PROPERTIES LABELS "offline")
set_tests_properties(tst_QCNetworkCache PROPERTIES LABELS "offline")
set_tests_properties(tst_QCNetworkLogger PROPERTIES LABELS "offline")
set_tests_properties(tst_QCNetworkMiddleware PROPERTIES LABELS "offline")
set_tests_properties(tst_QCNetworkMiddlewareIntegration PROPERTIES LABELS "offline")
set_tests_properties(tst_QCNetworkUnifiedPolicyMiddlewareOffline PROPERTIES LABELS "offline")
set_tests_properties(tst_QCNetworkCookieBridge PROPERTIES LABELS "offline")
set_tests_properties(tst_QCNetworkCancelToken PROPERTIES LABELS "offline")
set_tests_properties(tst_QCNetworkRequestCanonicalFlowApi PROPERTIES LABELS "offline")
set_tests_properties(tst_QCNetworkRequestPipeline PROPERTIES LABELS "offline")
set_tests_properties(tst_QCNetworkMockHandler PROPERTIES LABELS "offline")
set_tests_properties(tst_QCNetworkHttp3 PROPERTIES LABELS "offline")

# 环境依赖（env 门禁证据：要求在门禁环境中可稳定执行，不应触发 QSKIP）
set_tests_properties(tst_QCNetworkReply PROPERTIES LABELS "env;httpbin;local_port")
set_tests_properties(tst_QCNetworkRetry PROPERTIES LABELS "env;httpbin")
set_tests_properties(tst_QCNetworkHttp2 PROPERTIES LABELS "capability;http2;local_port")
set_tests_properties(tst_QCNetworkProxy PROPERTIES LABELS "capability;proxy;local_port")
set_tests_properties(tst_QCNetworkFileTransfer PROPERTIES LABELS "env;httpbin;local_port")
set_tests_properties(tst_QCNetworkFileResumableOffline PROPERTIES LABELS "env;local_port")
set_tests_properties(tst_QCNetworkIODeviceLifetime PROPERTIES LABELS "env;local_port")
set_tests_properties(tst_QCNetworkStreamUpload PROPERTIES LABELS "env;local_port")
set_tests_properties(tst_QCNetworkMultipartUpload PROPERTIES LABELS "env;local_port")
set_tests_properties(tst_QCNetworkCacheIntegration PROPERTIES LABELS "env;httpbin")
set_tests_properties(tst_QCNetworkConnectionPool PROPERTIES LABELS "env;httpbin;connection_pool")
set_tests_properties(tst_QCNetworkNetworkPath PROPERTIES LABELS "capability;network_path")
set_tests_properties(tst_QCNetworkShareHandle PROPERTIES LABELS "env;local_port;python")
set_tests_properties(tst_QCNetworkDiagnostics PROPERTIES LABELS "external_network;diagnostics")
set_tests_properties(tst_QCNetworkDiagnosticsLocal PROPERTIES LABELS "env;local_port;diagnostics")
set_tests_properties(tst_Integration PROPERTIES LABELS "env;httpbin;local_port;node")
set_tests_properties(tst_LargeFileDownload PROPERTIES LABELS "external_heavy")

# -----------------------------------------------------------------------------
# external_*：唯一允许放宽 skip=fail 的例外集合
#
# 合同（D003）：
# - 只有显式 opt-in 的 external_* 集合允许通过 QSKIP 表达“当前未取证”。
# - 其他标签（offline/env/capability/local_port/...）一律保持 skip=fail；
#   若前置条件不满足，应在对应 gate 中显式失败，而不是靠 QSKIP 假绿。
# -----------------------------------------------------------------------------
set_tests_properties(tst_QCNetworkDiagnostics PROPERTIES
    FAIL_REGULAR_EXPRESSION "QCURL__FAIL_REGEX_NEVER_MATCH"
)
set_tests_properties(tst_LargeFileDownload PROPERTIES
    FAIL_REGULAR_EXPRESSION "QCURL__FAIL_REGEX_NEVER_MATCH"
)

# WebSocket（条件编译）
if(QCURL_WEBSOCKET_SUPPORT)
    set_tests_properties(tst_QCWebSocketSendQueue PROPERTIES LABELS "offline;websocket")
    set_tests_properties(tst_QCWebSocket PROPERTIES LABELS "env;local_port;node;websocket")
    set_tests_properties(tst_QCWebSocketPool PROPERTIES LABELS "env;local_port;node;websocket")
endif()

# 统计本测试目录的主要 QtTest 目标，不包含 libcurl_consistency 的 pytest 执行器。
set(_qcurl_qttest_targets
    tst_QCStringContracts
    tst_QCNetworkRequest
    tst_QCNetworkError
    tst_CurlFeatureProbe
    tst_QCCurlMultiDetachOwnership
    tst_QCNetworkReply
    tst_QCNetworkRetry
    tst_QCNetworkRetryOffline
    tst_QCNetworkResponseHeadersOffline
    tst_QCNetworkRequestWarningsOffline
    tst_QCNetworkHttpErrorMappingOffline
    tst_QCNetworkHttp2
    tst_QCNetworkSchedulerPolicy
    tst_QCNetworkAdmissionCore
    tst_QCNetworkScheduler
    tst_QCNetworkSchedulerContract
    tst_QCNetworkSchedulerTransport
    tst_QCNetworkActorThreadModel
    tst_QCNetworkProxy
    tst_QCNetworkRequestCanonicalApi
    tst_QCNetworkRequestConfigCanonicalApi
    tst_QCNetworkBody
    tst_QCMultipartFormData
    tst_QCNetworkFileTransfer
    tst_QCNetworkDownloadToDeviceJob
    tst_QCNetworkFileResumableOffline
    tst_QCNetworkIODeviceLifetime
    tst_QCNetworkStreamUpload
    tst_QCNetworkMultipartUpload
    tst_QCNetworkCache
    tst_QCNetworkCacheIntegration
    tst_QCNetworkConnectionPool
    tst_QCNetworkNetworkPath
    tst_QCNetworkShareHandle
    tst_QCNetworkLogger
    tst_QCNetworkMiddleware
    tst_QCNetworkMiddlewareIntegration
    tst_QCNetworkUnifiedPolicyMiddlewareOffline
    tst_QCNetworkCookieBridge
    tst_QCNetworkCancelToken
    tst_QCNetworkRequestCanonicalFlowApi
    tst_QCNetworkRequestPipeline
    tst_QCNetworkMockHandler
    tst_QCNetworkHttp3
    tst_QCNetworkDiagnostics
    tst_QCNetworkDiagnosticsLocal
    tst_Integration
    tst_QCNetworkNativeDiagnostics
    tst_LargeFileDownload
)
if(QCURL_WEBSOCKET_SUPPORT)
    list(APPEND _qcurl_qttest_targets
    tst_QCWebSocket
    tst_QCWebSocketSendQueue
    tst_QCWebSocketPool
    )
endif()
list(LENGTH _qcurl_qttest_targets _qcurl_qttest_count)

message(STATUS "测试套件已配置：${_qcurl_qttest_count} 个测试（包含单元测试、功能测试和集成测试）")
