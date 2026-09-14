# QCurl QtTest（C++）与单侧集成测试

include(CMakeParseArguments)

set(QCURL_QTTEST_CORE_TARGET QCurl)
set(QCURL_QTTEST_BLOCKING_EXTRAS_TARGET QCurlBlockingExtras)
set(QCURL_QTTEST_OTHER_EXTRAS_TARGET QCurlOtherExtras)
if(TARGET QCurlTestInternals)
    set(QCURL_QTTEST_CORE_TARGET QCurlTestSupportTestInternals)
    set(QCURL_QTTEST_BLOCKING_EXTRAS_TARGET QCurlTestInternals)
    set(QCURL_QTTEST_OTHER_EXTRAS_TARGET QCurlOtherExtrasTestInternals)
endif()

set(_qcurl_sanitizer_link_options)
if(CMAKE_EXE_LINKER_FLAGS MATCHES "-fsanitize="
   AND NOT CMAKE_EXE_LINKER_FLAGS MATCHES "-fsanitize=thread")
    list(APPEND _qcurl_sanitizer_link_options -no-pie)
endif()

find_package(Threads REQUIRED)
add_executable(qcurl_qt_tsan_control EXCLUDE_FROM_ALL qt_tsan_controls.cpp)
qcurl_enable_qt_no_keywords(qcurl_qt_tsan_control)
target_link_libraries(qcurl_qt_tsan_control PRIVATE Qt6::Core Threads::Threads)
set_target_properties(qcurl_qt_tsan_control PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests")

add_library(qcurl_qttest_paths INTERFACE)
qcurl_enable_qt_no_keywords(qcurl_qttest_paths)
target_compile_definitions(qcurl_qttest_paths INTERFACE
    QCURL_TEST_SOURCE_ROOT="${PROJECT_SOURCE_DIR}"
)
target_include_directories(qcurl_qttest_paths INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

include("${CMAKE_CURRENT_LIST_DIR}/QCurlQtTestHelpers.cmake")

# 添加测试文件
add_qcurl_test(tst_QCNetworkRequest tst_QCNetworkRequest.cpp)
add_qcurl_test(tst_QCNetworkError tst_QCNetworkError.cpp)
add_qcurl_test(tst_CurlFeatureProbe tst_CurlFeatureProbe.cpp)
target_compile_definitions(tst_CurlFeatureProbe PRIVATE QCURL_ENABLE_TEST_HOOKS)
add_qcurl_test(tst_CurlGlobalInitialization tst_CurlGlobalInitialization.cpp)
target_compile_definitions(tst_CurlGlobalInitialization PRIVATE QCURL_ENABLE_TEST_HOOKS)
target_link_libraries(tst_CurlGlobalInitialization
    PRIVATE ${QCURL_QTTEST_BLOCKING_EXTRAS_TARGET})
add_qcurl_test(tst_QCurlRuntime tst_QCurlRuntime.cpp)
target_compile_definitions(tst_QCurlRuntime PRIVATE QCURL_ENABLE_TEST_HOOKS)
add_executable(tst_QCCurlMultiDetachOwnership tst_QCCurlMultiDetachOwnership.cpp)
qcurl_enable_qt_no_keywords(tst_QCCurlMultiDetachOwnership)
target_link_libraries(tst_QCCurlMultiDetachOwnership
    PRIVATE
        Qt6::Test
        Qt6::Network
        ${QCURL_QTTEST_CORE_TARGET}
        CURL::libcurl
        qcurl_qttest_paths
)
if(_qcurl_sanitizer_link_options)
    target_link_options(tst_QCCurlMultiDetachOwnership PRIVATE ${_qcurl_sanitizer_link_options})
endif()
target_include_directories(tst_QCCurlMultiDetachOwnership PRIVATE
    ${PROJECT_SOURCE_DIR}/src
    ${PROJECT_BINARY_DIR}/src
)
target_compile_definitions(tst_QCCurlMultiDetachOwnership PRIVATE QCURL_ENABLE_TEST_HOOKS)
set_target_properties(tst_QCCurlMultiDetachOwnership PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests"
)
set(_qcurl_multi_poison_cases
    internal
    bad-easy
    bad-handle
    unknown-done
    concurrent-quarantine
)
set(_qcurl_multi_share_cases
    detach-failure
    rollback-failure
    cleanup-in-use
    shutdown-cleanup-failure
)
set(_qcurl_multi_poison_lsan_options
    "print_suppressions=1:suppressions=${CMAKE_CURRENT_SOURCE_DIR}/multi_ownership_lsan.supp"
)
foreach(_ownership_case IN ITEMS recursive-once owner-thread-rejection ${_qcurl_multi_poison_cases}
                                  ${_qcurl_multi_share_cases})
    set(_ownership_env "QCURL_TEST_MULTI_OWNERSHIP_CASE=${_ownership_case}")
    if(NOT _ownership_case STREQUAL "unknown-done"
       AND NOT _ownership_case STREQUAL "concurrent-quarantine"
       AND NOT _ownership_case STREQUAL "shutdown-cleanup-failure")
        list(APPEND _ownership_env "QCURL_TEST_FORCE_MULTI_REMOVE_ERROR=${_ownership_case}")
    endif()
    if(_ownership_case IN_LIST _qcurl_multi_poison_cases)
        list(APPEND _ownership_env "LSAN_OPTIONS=${_qcurl_multi_poison_lsan_options}")
    endif()
    if(_ownership_case STREQUAL "detach-failure")
        list(APPEND _ownership_env
            "QCURL_TEST_FORCE_COOKIE_OPTION_ERROR=rollback:CURLOPT_SHARE")
    elseif(_ownership_case STREQUAL "rollback-failure")
        list(APPEND _ownership_env
            "QCURL_TEST_FORCE_COOKIE_OPTION_ERROR=setup:CURLOPT_COOKIEFILE,rollback:CURLOPT_SHARE")
    elseif(_ownership_case STREQUAL "cleanup-in-use")
        list(APPEND _ownership_env "QCURL_TEST_FORCE_SHARE_CLEANUP_ERROR=cleanup-in-use")
    elseif(_ownership_case STREQUAL "shutdown-cleanup-failure")
        list(APPEND _ownership_env
            "QCURL_TEST_FORCE_SHARE_CLEANUP_ERROR=shutdown-cleanup-failure")
    endif()
    add_test(
        NAME tst_QCCurlMultiDetachOwnership_${_ownership_case}
        COMMAND ${CMAKE_COMMAND} -E env ${_ownership_env}
                $<TARGET_FILE:tst_QCCurlMultiDetachOwnership>
    )
    set_tests_properties(tst_QCCurlMultiDetachOwnership_${_ownership_case} PROPERTIES
        FAIL_REGULAR_EXPRESSION "SKIP *:"
        LABELS "offline;multi_ownership"
    )
endforeach()
add_qcurl_test_with_preflight(
    tst_QCBlockingNetworkClient
    REQUIRE_LOCAL_PORT
    SOURCES
    tst_QCBlockingNetworkClient.cpp
    qcblocking_upload_echo_server.cpp
)
target_link_libraries(tst_QCBlockingNetworkClient PRIVATE ${QCURL_QTTEST_BLOCKING_EXTRAS_TARGET})
add_qcurl_test_with_preflight(
    tst_QCBlockingRequestConfig
    REQUIRE_LOCAL_PORT
    SOURCES
    tst_QCBlockingRequestConfig.cpp
    qcblocking_upload_echo_server.cpp
)
target_link_libraries(tst_QCBlockingRequestConfig PRIVATE ${QCURL_QTTEST_BLOCKING_EXTRAS_TARGET})
target_compile_definitions(tst_QCBlockingRequestConfig PRIVATE
    QCURL_ENABLE_TEST_HOOKS
    QCURL_ENABLE_ADVANCED_REQUEST_NETWORK_PATH_API
)

add_executable(qcurl_http2_capability_probe qcurl_http2_capability_probe.cpp)
qcurl_enable_qt_no_keywords(qcurl_http2_capability_probe)
target_link_libraries(qcurl_http2_capability_probe
    PRIVATE
        Qt6::Core
        QCurl
        CURL::libcurl
)
if(_qcurl_sanitizer_link_options)
    target_link_options(qcurl_http2_capability_probe PRIVATE ${_qcurl_sanitizer_link_options})
endif()
target_include_directories(qcurl_http2_capability_probe
    PRIVATE
        ${PROJECT_SOURCE_DIR}/src
        ${PROJECT_BINARY_DIR}/src
)
set_target_properties(qcurl_http2_capability_probe PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests"
)
add_qcurl_test_with_preflight(
    tst_QCNetworkReply
    REQUIRE_HTTPBIN
    REQUIRE_LOCAL_PORT
    SOURCES
    tst_QCNetworkReply.cpp
)
target_compile_definitions(tst_QCNetworkReply PRIVATE QCURL_ENABLE_TEST_HOOKS)
add_qcurl_test_with_preflight(
    tst_QCNetworkRetry
    REQUIRE_HTTPBIN
    SOURCES
    tst_QCNetworkRetry.cpp
) # 重试机制测试
target_compile_definitions(tst_QCNetworkRetry PRIVATE QCURL_ENABLE_TEST_HOOKS)
add_qcurl_test(tst_QCNetworkRetryOffline tst_QCNetworkRetryOffline.cpp) # 离线重试/限流门禁（MockHandler）
add_qcurl_test(tst_QCNetworkResponseHeadersOffline tst_QCNetworkResponseHeadersOffline.cpp) # 离线门禁：响应头 unfold/原始头观测
target_compile_definitions(tst_QCNetworkResponseHeadersOffline PRIVATE QCURL_ENABLE_TEST_HOOKS)
add_qcurl_test(tst_QCNetworkRequestWarningsOffline tst_QCNetworkRequestWarningsOffline.cpp) # 离线门禁：请求配置冲突可诊断（warnings）
target_compile_definitions(tst_QCNetworkRequestWarningsOffline PRIVATE QCURL_ENABLE_TEST_HOOKS)
add_qcurl_test(tst_QCNetworkHttpErrorMappingOffline tst_QCNetworkHttpErrorMappingOffline.cpp) # 离线门禁：HTTP 错误映射与终态语义
target_compile_definitions(tst_QCNetworkHttpErrorMappingOffline PRIVATE QCURL_ENABLE_TEST_HOOKS)
add_qcurl_test_with_preflight(
    tst_QCNetworkHttp2
    REQUIRE_HTTP2_SUITE
    HTTP2_PROBE_BIN "$<TARGET_FILE:qcurl_http2_capability_probe>"
    SOURCES
    tst_QCNetworkHttp2.cpp
) # HTTP/2 功能测试
add_qcurl_test(tst_QCNetworkSchedulerPolicy tst_QCNetworkSchedulerPolicy.cpp) # lane key / scheduler policy 值类型测试
add_qcurl_test(tst_QCNetworkAdmissionCore tst_QCNetworkAdmissionCore.cpp)
set_tests_properties(tst_QCNetworkAdmissionCore PROPERTIES LABELS "offline")
add_qcurl_test(tst_QCNetworkScheduler
    qcnetwork_scheduler_test.h
    tst_QCNetworkScheduler.cpp
    tst_QCNetworkSchedulerSelection.cpp
    tst_QCNetworkSchedulerCommands.cpp
    tst_QCNetworkSchedulerLifecycle.cpp
    tst_QCNetworkSchedulerNotifications.cpp
    tst_QCNetworkSchedulerPrivate.cpp
) # 请求优先级调度测试
target_compile_definitions(tst_QCNetworkScheduler PRIVATE QCURL_ENABLE_TEST_HOOKS)
add_qcurl_test(tst_QCNetworkSchedulerContract tst_QCNetworkSchedulerContract.cpp)
target_compile_definitions(tst_QCNetworkSchedulerContract PRIVATE QCURL_ENABLE_TEST_HOOKS)
set_tests_properties(tst_QCNetworkSchedulerContract PROPERTIES LABELS "offline")
add_qcurl_test_with_preflight(tst_QCNetworkSchedulerTransport
    REQUIRE_LOCAL_PORT
    SOURCES tst_QCNetworkSchedulerTransport.cpp qcurl_http_script_server.h)
set_tests_properties(tst_QCNetworkSchedulerTransport PROPERTIES LABELS "env;local_port")
add_qcurl_test_with_preflight(
    tst_QCNetworkActorThreadModel
    REQUIRE_LOCAL_PORT
    SOURCES
    tst_QCNetworkActorThreadModel.cpp
) # Actor/跨线程提交线程模型回归
target_compile_definitions(tst_QCNetworkActorThreadModel PRIVATE QCURL_ENABLE_TEST_HOOKS)
add_qcurl_test_with_preflight(
    tst_QCNetworkProxy
    REQUIRE_LOCAL_PORT
    SOURCES
    tst_QCNetworkProxy.cpp
) # 代理配置测试
add_qcurl_test(tst_QCNetworkRequestCanonicalApi tst_QCNetworkRequestCanonicalApi.cpp) # canonical API: QCNetworkRequest + send* 语义回归
add_qcurl_test(tst_QCNetworkRequestConfigCanonicalApi tst_QCNetworkRequestConfigCanonicalApi.cpp) # canonical API: request config 语义回归
add_qcurl_test(tst_QCNetworkBody tst_QCNetworkBody.cpp) # request body helper 测试
add_qcurl_test(tst_QCMultipartFormData tst_QCMultipartFormData.cpp) # Multipart/form-data 测试
add_qcurl_test_with_preflight(
    tst_QCNetworkFileTransfer
    REQUIRE_HTTPBIN
    REQUIRE_LOCAL_PORT
    SOURCES
    tst_QCNetworkFileTransfer.cpp
) # 文件传输 API 测试
add_qcurl_test(tst_QCNetworkDownloadToDeviceJob tst_QCNetworkDownloadToDeviceJob.cpp) # Download-to-device job 显式 start/fail-fast 合同测试
target_compile_definitions(tst_QCNetworkDownloadToDeviceJob PRIVATE QCURL_ENABLE_TEST_HOOKS)
add_qcurl_test_with_preflight(
    tst_QCNetworkFileResumableOffline
    REQUIRE_LOCAL_PORT
    SOURCES
    tst_QCNetworkFileResumableOffline.cpp
) # QCNetworkResumableDownloadJob 离线门禁：416 成功语义 + start 校验/覆盖重下
target_compile_definitions(tst_QCNetworkFileResumableOffline PRIVATE QCURL_ENABLE_TEST_HOOKS)
add_qcurl_test_with_preflight(tst_QCNetworkDownloadIntegrity
    REQUIRE_LOCAL_PORT
    SOURCES tst_QCNetworkDownloadIntegrity.cpp qcurl_http_script_server.h)
set_tests_properties(tst_QCNetworkDownloadIntegrity PROPERTIES LABELS "env;local_port")
add_qcurl_test_with_preflight(tst_QCNetworkStreamingCache
    REQUIRE_LOCAL_PORT
    SOURCES tst_QCNetworkStreamingCache.cpp qcurl_http_script_server.h)
target_compile_definitions(tst_QCNetworkStreamingCache PRIVATE QCURL_ENABLE_TEST_HOOKS)
set_tests_properties(tst_QCNetworkStreamingCache PROPERTIES LABELS "env;local_port")
add_qcurl_test_with_preflight(tst_QCNetworkBuiltinLogRedaction
    REQUIRE_LOCAL_PORT
    SOURCES tst_QCNetworkBuiltinLogRedaction.cpp qcurl_http_script_server.h)
set_tests_properties(tst_QCNetworkBuiltinLogRedaction PROPERTIES LABELS "env;local_port")
add_qcurl_test_with_preflight(tst_QCNetworkPoolContract
    REQUIRE_LOCAL_PORT
    SOURCES tst_QCNetworkPoolContract.cpp qcurl_http_script_server.h)
set_tests_properties(tst_QCNetworkPoolContract PROPERTIES LABELS "env;local_port")
add_qcurl_test_with_preflight(tst_QCNetworkHttpSemantics
    REQUIRE_LOCAL_PORT
    SOURCES tst_QCNetworkHttpSemantics.cpp qcurl_http_script_server.h)
target_link_libraries(tst_QCNetworkHttpSemantics PRIVATE ${QCURL_QTTEST_BLOCKING_EXTRAS_TARGET})
set_tests_properties(tst_QCNetworkHttpSemantics PROPERTIES LABELS "env;local_port")
add_qcurl_test_with_preflight(tst_QCNetworkCompletionContract
    REQUIRE_LOCAL_PORT
    SOURCES tst_QCNetworkCompletionContract.cpp qcurl_http_script_server.h)
target_compile_definitions(tst_QCNetworkCompletionContract PRIVATE QCURL_ENABLE_TEST_HOOKS)
set_tests_properties(tst_QCNetworkCompletionContract PROPERTIES LABELS "env;local_port")
add_qcurl_test_with_preflight(tst_QCNetworkNativeDiagnostics
    REQUIRE_LOCAL_PORT
    SOURCES
    tst_QCNetworkNativeDiagnostics.cpp
    tst_QCNetworkNativeDiagnostics.h
    tst_QCNetworkNativeDiagnosticsRetry.cpp
    tst_QCNetworkNativeDiagnosticsState.cpp
    qcurl_http_script_server.h
    qcurl_tls_test_server.h)
target_compile_definitions(tst_QCNetworkNativeDiagnostics PRIVATE QCURL_ENABLE_TEST_HOOKS)
target_link_libraries(tst_QCNetworkNativeDiagnostics PRIVATE ${QCURL_QTTEST_BLOCKING_EXTRAS_TARGET})
set_tests_properties(tst_QCNetworkNativeDiagnostics PROPERTIES LABELS "env;local_port")
add_qcurl_test_with_preflight(
    tst_QCNetworkIODeviceLifetime
    REQUIRE_LOCAL_PORT
    SOURCES
    tst_QCNetworkIODeviceLifetime.cpp
) # QIODevice 生命周期契约测试
add_qcurl_test_with_preflight(
    tst_QCNetworkStreamUpload
    REQUIRE_LOCAL_PORT
    SOURCES
    tst_QCNetworkStreamUpload.cpp
) # 流式上传契约/分支覆盖测试
add_qcurl_test_with_preflight(
    tst_QCNetworkMultipartUpload
    REQUIRE_LOCAL_PORT
    SOURCES
    tst_QCNetworkMultipartUpload.cpp
) # QCNetworkMultipartBody streaming 合同/回归测试
add_qcurl_test(tst_QCNetworkCache tst_QCNetworkCache.cpp) # 缓存机制测试
target_compile_definitions(tst_QCNetworkCache PRIVATE QCURL_ENABLE_TEST_HOOKS)
add_library(qcurl_cache_extras_header_self_compile OBJECT
    cache_extras_first_include_QCNetworkCache.cpp
    cache_extras_first_include_QCNetworkCachePolicy.cpp
    cache_extras_first_include_QCNetworkDiskCache.cpp
    cache_extras_first_include_QCNetworkMemoryCache.cpp
)
qcurl_enable_qt_no_keywords(qcurl_cache_extras_header_self_compile)
target_link_libraries(qcurl_cache_extras_header_self_compile
    PRIVATE
        Qt6::Core
        QCurl
)
target_include_directories(qcurl_cache_extras_header_self_compile
    PRIVATE
        ${PROJECT_SOURCE_DIR}/src
        ${PROJECT_BINARY_DIR}/src
)
add_qcurl_test_with_preflight(
    tst_QCNetworkCacheIntegration
    REQUIRE_HTTPBIN
    SOURCES
    tst_QCNetworkCacheIntegration.cpp
) # 缓存集成测试
add_qcurl_test_with_preflight(
    tst_QCNetworkConnectionPool
    REQUIRE_HTTPBIN
    SOURCES
    tst_QCNetworkConnectionPool.cpp
) # 连接池测试
add_qcurl_test(tst_QCNetworkNetworkPath tst_QCNetworkNetworkPath.cpp) # 网络路径与 DNS 控制（M4）
target_compile_definitions(tst_QCNetworkNetworkPath PRIVATE
    QCURL_ENABLE_TEST_HOOKS
    QCURL_ENABLE_ADVANCED_REQUEST_NETWORK_PATH_API
)
target_compile_definitions(${QCURL_QTTEST_CORE_TARGET}
    PRIVATE QCURL_ENABLE_ADVANCED_REQUEST_NETWORK_PATH_API)
add_qcurl_test_with_preflight(
    tst_QCNetworkShareHandle
    REQUIRE_LOCAL_PORT
    REQUIRE_PYTHON
    SOURCES
    tst_QCNetworkShareHandle.cpp
) # multi share handle（M6+）

# Logger / middleware / cookie / cancel token 相关测试
add_qcurl_test(tst_QCNetworkLogger tst_QCNetworkLogger.cpp) # Logger 系统测试
add_qcurl_test(tst_QCNetworkMiddleware tst_QCNetworkMiddleware.cpp) # 中间件测试
add_qcurl_test(tst_QCNetworkMiddlewareIntegration tst_QCNetworkMiddlewareIntegration.cpp) # middleware 自动接入执行链路（离线门禁）
target_compile_definitions(tst_QCNetworkMiddlewareIntegration PRIVATE QCURL_ENABLE_TEST_HOOKS)
add_qcurl_test(tst_QCNetworkUnifiedPolicyMiddlewareOffline tst_QCNetworkUnifiedPolicyMiddlewareOffline.cpp) # 统一重试/脱敏日志/观测埋点（离线门禁）
target_link_libraries(tst_QCNetworkUnifiedPolicyMiddlewareOffline
    PRIVATE ${QCURL_QTTEST_OTHER_EXTRAS_TARGET})
add_qcurl_test(tst_QCNetworkCookieBridge tst_QCNetworkCookieBridge.cpp) # cookie bridge（share cookie store）离线门禁
target_compile_definitions(tst_QCNetworkCookieBridge PRIVATE QCURL_ENABLE_TEST_HOOKS)
add_qcurl_test(tst_QCNetworkCancelToken tst_QCNetworkCancelToken.cpp) # 取消令牌测试
add_qcurl_test(tst_QCNetworkRequestCanonicalFlowApi tst_QCNetworkRequestCanonicalFlowApi.cpp) # canonical API: request handoff/send flow 语义回归
add_qcurl_test(tst_QCNetworkRequestPipeline tst_QCNetworkRequestPipeline.cpp) # internal D-lite: normalize/compile digest 白盒测试
target_compile_definitions(tst_QCNetworkRequestPipeline PRIVATE QCURL_ENABLE_TEST_HOOKS)
add_qcurl_test(tst_QCNetworkMockHandler tst_QCNetworkMockHandler.cpp) # Mock 工具测试

# HTTP/3 支持测试
add_qcurl_test(tst_QCNetworkHttp3 tst_QCNetworkHttp3.cpp) # HTTP/3 功能测试
target_compile_definitions(tst_QCNetworkHttp3 PRIVATE QCURL_ENABLE_TEST_HOOKS)
target_link_libraries(tst_QCNetworkHttp3 PRIVATE ${QCURL_QTTEST_BLOCKING_EXTRAS_TARGET})

# 网络诊断工具测试
add_qcurl_test(tst_QCNetworkDiagnostics tst_QCNetworkDiagnostics.cpp) # 网络诊断功能测试
target_link_libraries(tst_QCNetworkDiagnostics PRIVATE ${QCURL_QTTEST_OTHER_EXTRAS_TARGET})
add_qcurl_test_with_preflight(
    tst_QCNetworkDiagnosticsLocal
    REQUIRE_LOCAL_PORT
    SOURCES
    tst_QCNetworkDiagnosticsLocal.cpp
    qcurl_tls_test_server.h
) # 本地 deterministic diagnostics gate
target_link_libraries(tst_QCNetworkDiagnosticsLocal
    PRIVATE ${QCURL_QTTEST_OTHER_EXTRAS_TARGET})

# WebSocket 测试（条件编译）
if(QCURL_WEBSOCKET_SUPPORT)
    add_qcurl_test(
        tst_QCWebSocketSendQueue
        tst_QCWebSocketSendQueue.cpp
        ${PROJECT_SOURCE_DIR}/src/private/QCWebSocketSendQueue.cpp
    )
    add_qcurl_test_with_preflight(
        tst_QCWebSocket
        REQUIRE_LOCAL_PORT
        REQUIRE_NODE
        SOURCES
        tst_QCWebSocket.cpp
    )
    target_link_libraries(tst_QCWebSocket PRIVATE ${QCURL_QTTEST_OTHER_EXTRAS_TARGET})
    target_compile_definitions(tst_QCWebSocket PRIVATE QCURL_ENABLE_TEST_HOOKS)
    add_qcurl_test_with_preflight(
        tst_QCWebSocketPool
        REQUIRE_LOCAL_PORT
        REQUIRE_NODE
        SOURCES
        tst_QCWebSocketPool.cpp
    ) # WebSocket 连接池测试
    target_link_libraries(tst_QCWebSocketPool PRIVATE ${QCURL_QTTEST_OTHER_EXTRAS_TARGET})
endif()

add_qcurl_test_with_preflight(
    tst_Integration
    REQUIRE_HTTPBIN
    REQUIRE_LOCAL_PORT
    REQUIRE_NODE
    SOURCES
    tst_Integration.cpp
)
add_qcurl_test(tst_LargeFileDownload tst_LargeFileDownload.cpp)

# -----------------------------------------------------------------------------
# CTest Labels（取证式门禁分组）
#
# 目标：
# - `offline`：默认门禁集合；不应依赖外部网络/服务，且不应触发 QtTest `QSKIP`。
# - `env`：需要额外环境/能力（网络、httpbin、端口绑定、python、特定 libcurl feature 等）；应在具备依赖的 job 中单独跑（同样建议 skip=fail）。
#
# 用法示例：
# - 仅跑离线门禁：ctest -L offline
# - 仅跑环境依赖：ctest -L env
# -----------------------------------------------------------------------------

include("${CMAKE_CURRENT_LIST_DIR}/QCurlQtTestLabels.cmake")
