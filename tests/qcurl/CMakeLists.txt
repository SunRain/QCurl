# QCurl QtTest（C++）与单侧集成测试

# 测试辅助函数：添加单个测试
function(add_qcurl_test test_name)
    set(test_sources ${ARGN})

    add_executable(${test_name} ${test_sources})

    target_link_libraries(${test_name}
        PRIVATE
            Qt6::Test
            QCurl
            CURL::libcurl # 添加 libcurl 链接（某些测试需要直接调用 curl API）
    )

    target_include_directories(${test_name}
        PRIVATE
            ${CMAKE_SOURCE_DIR}/src
            ${CMAKE_BINARY_DIR}/src
    )

    add_test(NAME ${test_name} COMMAND ${test_name})

    # -----------------------------------------------------------------------------
    # 取证式门禁：QSKIP 视为失败（避免 “ctest 全绿≠已覆盖” 的误读）
    #
    # 背景：
    # - QtTest 的 QSKIP 进程返回码仍为 0，ctest 默认会将该 test 计为 Passed。
    # - 在门禁与证据口径下，“跳过=未执行=无证据”，应显式失败。
    #
    # 机制：
    # - QtTest 输出的跳过行形如：`SKIP   : TestClass::testCase() <reason>`
    # - CTest 的 FAIL_REGULAR_EXPRESSION 可对测试输出做正则匹配并判定失败。
    # -----------------------------------------------------------------------------
    set_tests_properties(${test_name} PROPERTIES
        FAIL_REGULAR_EXPRESSION "SKIP\\s*:"
    )

    set_target_properties(${test_name} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests"
    )
endfunction()

# 添加测试文件
add_qcurl_test(tst_QCNetworkRequest tst_QCNetworkRequest.cpp)
add_qcurl_test(tst_QCNetworkError tst_QCNetworkError.cpp)
add_qcurl_test(tst_CurlFeatureProbe tst_CurlFeatureProbe.cpp)
add_qcurl_test(tst_QCNetworkReply tst_QCNetworkReply.cpp)
target_compile_definitions(tst_QCNetworkReply PRIVATE QCURL_ENABLE_TEST_HOOKS)
add_qcurl_test(tst_QCNetworkRetry tst_QCNetworkRetry.cpp) # 重试机制测试
add_qcurl_test(tst_QCNetworkRetryOffline tst_QCNetworkRetryOffline.cpp) # 离线重试/限流门禁（MockHandler）
add_qcurl_test(tst_QCNetworkResponseHeadersOffline tst_QCNetworkResponseHeadersOffline.cpp) # 离线门禁：响应头 unfold/原始头观测
add_qcurl_test(tst_QCNetworkRequestWarningsOffline tst_QCNetworkRequestWarningsOffline.cpp) # 离线门禁：请求配置冲突可诊断（warnings）
add_qcurl_test(tst_QCNetworkHttpErrorMappingOffline tst_QCNetworkHttpErrorMappingOffline.cpp) # 离线门禁：HTTP 错误映射与终态语义
add_qcurl_test(tst_QCNetworkHttp2 tst_QCNetworkHttp2.cpp) # HTTP/2 功能测试
add_qcurl_test(tst_QCNetworkScheduler tst_QCNetworkScheduler.cpp) # 请求优先级调度测试
add_qcurl_test(tst_QCNetworkActorThreadModel tst_QCNetworkActorThreadModel.cpp) # Actor/跨线程提交线程模型回归
add_qcurl_test(tst_QCNetworkProxy tst_QCNetworkProxy.cpp) # 代理配置测试
add_qcurl_test(tst_QCNetworkRequestCanonicalApi tst_QCNetworkRequestCanonicalApi.cpp) # canonical API: QCNetworkRequest + send* 语义回归
add_qcurl_test(tst_QCNetworkRequestConfigCanonicalApi tst_QCNetworkRequestConfigCanonicalApi.cpp) # canonical API: request config 语义回归
add_qcurl_test(tst_QCMultipartFormData tst_QCMultipartFormData.cpp) # Multipart/form-data 测试
add_qcurl_test(tst_QCNetworkFileTransfer tst_QCNetworkFileTransfer.cpp) # 文件传输 API 测试
add_qcurl_test(tst_QCNetworkIODeviceLifetime tst_QCNetworkIODeviceLifetime.cpp) # QIODevice 生命周期契约测试
add_qcurl_test(tst_QCNetworkStreamUpload tst_QCNetworkStreamUpload.cpp) # 流式上传契约/分支覆盖测试
add_qcurl_test(tst_QCNetworkCache tst_QCNetworkCache.cpp) # 缓存机制测试
add_qcurl_test(tst_QCNetworkCacheIntegration tst_QCNetworkCacheIntegration.cpp) # 缓存集成测试
add_qcurl_test(tst_QCNetworkConnectionPool tst_QCNetworkConnectionPool.cpp) # 连接池测试
add_qcurl_test(tst_QCNetworkNetworkPath tst_QCNetworkNetworkPath.cpp) # 网络路径与 DNS 控制（M4）
add_qcurl_test(tst_QCNetworkShareHandle tst_QCNetworkShareHandle.cpp) # multi share handle（M6+）

# Logger / middleware / cookie / cancel token 相关测试
add_qcurl_test(tst_QCNetworkLogger tst_QCNetworkLogger.cpp) # Logger 系统测试
add_qcurl_test(tst_QCNetworkMiddleware tst_QCNetworkMiddleware.cpp) # 中间件测试
add_qcurl_test(tst_QCNetworkMiddlewareIntegration tst_QCNetworkMiddlewareIntegration.cpp) # middleware 自动接入执行链路（离线门禁）
add_qcurl_test(tst_QCNetworkUnifiedPolicyMiddlewareOffline tst_QCNetworkUnifiedPolicyMiddlewareOffline.cpp) # 统一重试/脱敏日志/观测埋点（离线门禁）
add_qcurl_test(tst_QCNetworkCookieBridge tst_QCNetworkCookieBridge.cpp) # cookie bridge（share cookie store）离线门禁
add_qcurl_test(tst_QCNetworkCancelToken tst_QCNetworkCancelToken.cpp) # 取消令牌测试
add_qcurl_test(tst_QCNetworkRequestCanonicalFlowApi tst_QCNetworkRequestCanonicalFlowApi.cpp) # canonical API: request handoff/send flow 语义回归
add_qcurl_test(tst_QCNetworkRequestPipeline tst_QCNetworkRequestPipeline.cpp) # internal D-lite: normalize/compile digest 白盒测试
add_qcurl_test(tst_QCNetworkMockHandler tst_QCNetworkMockHandler.cpp) # Mock 工具测试

# HTTP/3 支持测试
add_qcurl_test(tst_QCNetworkHttp3 tst_QCNetworkHttp3.cpp) # HTTP/3 功能测试

# 网络诊断工具测试
add_qcurl_test(tst_QCNetworkDiagnostics tst_QCNetworkDiagnostics.cpp) # 网络诊断功能测试

# WebSocket 测试（条件编译）
if(QCURL_WEBSOCKET_SUPPORT)
    add_qcurl_test(tst_QCWebSocket tst_QCWebSocket.cpp)
    add_qcurl_test(tst_QCWebSocketPool tst_QCWebSocketPool.cpp) # WebSocket 连接池测试
    add_qcurl_test(tst_QCWebSocketCompression tst_QCWebSocketCompression.cpp) # WebSocket 压缩测试
endif()

add_qcurl_test(tst_Integration tst_Integration.cpp)
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

# 默认离线门禁（不包含任何源码内显式 QSKIP 的测试）
set_tests_properties(tst_QCNetworkRequest PROPERTIES LABELS "offline")
set_tests_properties(tst_QCNetworkError PROPERTIES LABELS "offline")
set_tests_properties(tst_CurlFeatureProbe PROPERTIES LABELS "offline")
set_tests_properties(tst_QCNetworkRetryOffline PROPERTIES LABELS "offline")
set_tests_properties(tst_QCNetworkResponseHeadersOffline PROPERTIES LABELS "offline")
set_tests_properties(tst_QCNetworkRequestWarningsOffline PROPERTIES LABELS "offline")
set_tests_properties(tst_QCNetworkHttpErrorMappingOffline PROPERTIES LABELS "offline")
set_tests_properties(tst_QCNetworkScheduler PROPERTIES LABELS "offline")
set_tests_properties(tst_QCNetworkActorThreadModel PROPERTIES LABELS "env;local_port")
set_tests_properties(tst_QCNetworkRequestCanonicalApi PROPERTIES LABELS "offline")
set_tests_properties(tst_QCNetworkRequestConfigCanonicalApi PROPERTIES LABELS "offline")
set_tests_properties(tst_QCMultipartFormData PROPERTIES LABELS "offline")
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
set_tests_properties(tst_QCNetworkProxy PROPERTIES LABELS "capability;proxy")
set_tests_properties(tst_QCNetworkFileTransfer PROPERTIES LABELS "env;httpbin;local_port")
set_tests_properties(tst_QCNetworkIODeviceLifetime PROPERTIES LABELS "env;local_port")
set_tests_properties(tst_QCNetworkStreamUpload PROPERTIES LABELS "env;local_port")
set_tests_properties(tst_QCNetworkCacheIntegration PROPERTIES LABELS "env;httpbin")
set_tests_properties(tst_QCNetworkConnectionPool PROPERTIES LABELS "env;httpbin;connection_pool")
set_tests_properties(tst_QCNetworkNetworkPath PROPERTIES LABELS "capability;network_path")
set_tests_properties(tst_QCNetworkShareHandle PROPERTIES LABELS "env;local_port;python")
set_tests_properties(tst_QCNetworkDiagnostics PROPERTIES LABELS "external_network;diagnostics")
set_tests_properties(tst_Integration PROPERTIES LABELS "env;httpbin")
set_tests_properties(tst_LargeFileDownload PROPERTIES LABELS "external_heavy")

# -----------------------------------------------------------------------------
# external_network：非确定性环境依赖
#
# 说明：
# - 默认回归（CI/受限网络）不应因为“外网不可达/被代理/被阻断”而失败。
# - 因此该类测试允许使用 QSKIP 来显式跳过外网探测场景。
# -----------------------------------------------------------------------------
set_tests_properties(tst_QCNetworkDiagnostics PROPERTIES
    FAIL_REGULAR_EXPRESSION "QCURL__FAIL_REGEX_NEVER_MATCH"
)
set_tests_properties(tst_QCNetworkHttp2 PROPERTIES
    FAIL_REGULAR_EXPRESSION "QCURL__FAIL_REGEX_NEVER_MATCH"
)
set_tests_properties(tst_QCNetworkActorThreadModel PROPERTIES
    FAIL_REGULAR_EXPRESSION "QCURL__FAIL_REGEX_NEVER_MATCH"
)
set_tests_properties(tst_LargeFileDownload PROPERTIES
    FAIL_REGULAR_EXPRESSION "QCURL__FAIL_REGEX_NEVER_MATCH"
)

# WebSocket（条件编译）
if(QCURL_WEBSOCKET_SUPPORT)
    set_tests_properties(tst_QCWebSocket PROPERTIES LABELS "env;local_port;websocket")
    set_tests_properties(tst_QCWebSocketPool PROPERTIES LABELS "env;local_port;websocket")
    set_tests_properties(tst_QCWebSocketCompression PROPERTIES LABELS "offline;websocket")
endif()

# 统计测试数量（以本文件 add_test 注册为准；不包含 libcurl_consistency 的 pytest 执行器）
set(_qcurl_qttest_targets
    tst_QCNetworkRequest
    tst_QCNetworkError
    tst_CurlFeatureProbe
    tst_QCNetworkReply
    tst_QCNetworkRetry
    tst_QCNetworkRetryOffline
    tst_QCNetworkResponseHeadersOffline
    tst_QCNetworkRequestWarningsOffline
    tst_QCNetworkHttpErrorMappingOffline
    tst_QCNetworkHttp2
    tst_QCNetworkScheduler
    tst_QCNetworkActorThreadModel
    tst_QCNetworkProxy
    tst_QCNetworkRequestCanonicalApi
    tst_QCNetworkRequestConfigCanonicalApi
    tst_QCMultipartFormData
    tst_QCNetworkFileTransfer
    tst_QCNetworkIODeviceLifetime
    tst_QCNetworkStreamUpload
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
    tst_Integration
    tst_LargeFileDownload
)
if(QCURL_WEBSOCKET_SUPPORT)
    list(APPEND _qcurl_qttest_targets
        tst_QCWebSocket
        tst_QCWebSocketPool
        tst_QCWebSocketCompression
    )
endif()
list(LENGTH _qcurl_qttest_targets _qcurl_qttest_count)

message(STATUS "测试套件已配置：${_qcurl_qttest_count} 个测试（包含单元测试、功能测试和集成测试）")
