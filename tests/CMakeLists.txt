# QCurl 测试套件

# 查找 Qt6 Test 模块
find_package(Qt6 REQUIRED COMPONENTS Test)

# 启用测试
enable_testing()

# 启用 Qt 自动工具
set(CMAKE_AUTOMOC ON)

# 测试辅助函数：添加单个测试
function(add_qcurl_test test_name)
    set(test_sources ${ARGN})

    add_executable(${test_name} ${test_sources})

    target_link_libraries(${test_name}
        PRIVATE
            Qt6::Test
            QCurl
            CURL::libcurl  # 添加 libcurl 链接（某些测试需要直接调用 curl API）
    )

    target_include_directories(${test_name}
        PRIVATE
            ${CMAKE_SOURCE_DIR}/src
            ${CMAKE_BINARY_DIR}/src
    )

    add_test(NAME ${test_name} COMMAND ${test_name})

    set_target_properties(${test_name} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests"
    )
endfunction()

# 添加测试文件
add_qcurl_test(tst_QCNetworkRequest tst_QCNetworkRequest.cpp)
add_qcurl_test(tst_QCNetworkError tst_QCNetworkError.cpp)
add_qcurl_test(tst_CurlFeatureProbe tst_CurlFeatureProbe.cpp)
add_qcurl_test(tst_QCNetworkReply tst_QCNetworkReply.cpp)
add_qcurl_test(tst_QCNetworkRetry tst_QCNetworkRetry.cpp)  # v2.1.0: 重试机制测试
add_qcurl_test(tst_QCNetworkRetryOffline tst_QCNetworkRetryOffline.cpp)  # v2.23.0: 离线重试/限流门禁（MockHandler）
add_qcurl_test(tst_QCNetworkResponseHeadersOffline tst_QCNetworkResponseHeadersOffline.cpp)  # 离线门禁：响应头 unfold/原始头观测
add_qcurl_test(tst_QCNetworkRequestWarningsOffline tst_QCNetworkRequestWarningsOffline.cpp)  # 离线门禁：请求配置冲突可诊断（warnings）
add_qcurl_test(tst_QCNetworkHttpErrorMappingOffline tst_QCNetworkHttpErrorMappingOffline.cpp)  # 离线门禁：HTTP 错误映射与终态语义
add_qcurl_test(tst_QCNetworkHttp2 tst_QCNetworkHttp2.cpp)  # v2.2.0: HTTP/2 功能测试
add_qcurl_test(tst_QCNetworkScheduler tst_QCNetworkScheduler.cpp)  # v2.6.0: 请求优先级调度测试
add_qcurl_test(tst_QCNetworkProxy tst_QCNetworkProxy.cpp)  # v2.2.1: 代理配置测试
add_qcurl_test(tst_QCRequest tst_QCRequest.cpp)  # v2.9.0: 流式 Request API 测试
add_qcurl_test(tst_QCRequestBuilder tst_QCRequestBuilder.cpp)  # v2.9.0: 传统 Builder API 测试
add_qcurl_test(tst_QCMultipartFormData tst_QCMultipartFormData.cpp)  # v2.11.0: Multipart/form-data 测试
add_qcurl_test(tst_QCNetworkFileTransfer tst_QCNetworkFileTransfer.cpp)  # v2.11.0+: 文件传输 API 测试
add_qcurl_test(tst_QCNetworkIODeviceLifetime tst_QCNetworkIODeviceLifetime.cpp)  # v2.20.0: QIODevice 生命周期契约测试
add_qcurl_test(tst_QCNetworkStreamUpload tst_QCNetworkStreamUpload.cpp)  # v2.20.0+: 流式上传契约/分支覆盖测试
add_qcurl_test(tst_QCNetworkCache tst_QCNetworkCache.cpp)  # v2.12.0: 缓存机制测试
add_qcurl_test(tst_QCNetworkCacheIntegration tst_QCNetworkCacheIntegration.cpp)  # v2.13.0: 缓存集成测试
add_qcurl_test(tst_QCNetworkConnectionPool tst_QCNetworkConnectionPool.cpp)  # v2.14.0: 连接池测试
add_qcurl_test(tst_QCNetworkNetworkPath tst_QCNetworkNetworkPath.cpp)  # v2.21.0: 网络路径与 DNS 控制（M4）
add_qcurl_test(tst_QCNetworkShareHandle tst_QCNetworkShareHandle.cpp)  # v2.22.0: multi share handle（M6+）

# v2.15.0 + v2.16.0: 生产级增强和开发体验优化测试
add_qcurl_test(tst_QCNetworkLogger tst_QCNetworkLogger.cpp)  # v2.15.0: Logger 系统测试
add_qcurl_test(tst_QCNetworkMiddleware tst_QCNetworkMiddleware.cpp)  # v2.15.0: 中间件测试
add_qcurl_test(tst_QCNetworkMiddlewareIntegration tst_QCNetworkMiddlewareIntegration.cpp)  # v2.24.0: middleware 自动接入执行链路（离线门禁）
add_qcurl_test(tst_QCNetworkUnifiedPolicyMiddlewareOffline tst_QCNetworkUnifiedPolicyMiddlewareOffline.cpp)  # v2.25.0: 统一重试/脱敏日志/观测埋点（离线门禁）
add_qcurl_test(tst_QCNetworkCookieBridge tst_QCNetworkCookieBridge.cpp)  # v2.26.0: cookie bridge（share cookie store）离线门禁
add_qcurl_test(tst_QCNetworkCancelToken tst_QCNetworkCancelToken.cpp)  # v2.15.0: 取消令牌测试
add_qcurl_test(tst_QCNetworkRequestBuilder tst_QCNetworkRequestBuilder.cpp)  # v2.16.0: 流式构建器测试
add_qcurl_test(tst_QCNetworkMockHandler tst_QCNetworkMockHandler.cpp)  # v2.16.0: Mock 工具测试

# v2.17.0: HTTP/3 支持测试
add_qcurl_test(tst_QCNetworkHttp3 tst_QCNetworkHttp3.cpp)  # v2.17.0: HTTP/3 功能测试

# v2.19.0: 网络诊断工具测试
add_qcurl_test(tst_QCNetworkDiagnostics tst_QCNetworkDiagnostics.cpp)  # v2.19.0: 网络诊断功能测试

# v2.3.0: WebSocket 测试（条件编译）
if(QCURL_WEBSOCKET_SUPPORT)
    add_qcurl_test(tst_QCWebSocket tst_QCWebSocket.cpp)
    add_qcurl_test(tst_QCWebSocketPool tst_QCWebSocketPool.cpp)  # v2.5.0: WebSocket 连接池测试
    add_qcurl_test(tst_QCWebSocketCompression tst_QCWebSocketCompression.cpp)  # v2.18.0: WebSocket 压缩测试
endif()

add_qcurl_test(tst_Integration tst_Integration.cpp)

# QCurl ↔ libcurl 可观测一致性门禁（仅在显式开启时构建）
if(QCURL_BUILD_LIBCURL_CONSISTENCY)
    # pytest 驱动：Qt Test 执行器（默认无 env 时会 skip）
    add_qcurl_test(tst_LibcurlConsistency tst_LibcurlConsistency.cpp)

    # baseline clients（非 Qt Test，供 pytest suite 使用）
    add_executable(qcurl_lc_ws_baseline libcurl_consistency/ws_baseline_client.cpp)
    target_link_libraries(qcurl_lc_ws_baseline PRIVATE CURL::libcurl)
    set_target_properties(qcurl_lc_ws_baseline PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests"
    )

    add_executable(qcurl_lc_range_resume_baseline libcurl_consistency/range_resume_baseline_client.cpp)
    target_link_libraries(qcurl_lc_range_resume_baseline PRIVATE CURL::libcurl)
    set_target_properties(qcurl_lc_range_resume_baseline PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests"
    )

    add_executable(qcurl_lc_postfields_binary_baseline libcurl_consistency/postfields_binary_baseline_client.cpp)
    target_link_libraries(qcurl_lc_postfields_binary_baseline PRIVATE CURL::libcurl)
    set_target_properties(qcurl_lc_postfields_binary_baseline PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests"
    )

    add_executable(qcurl_lc_multi_get4_baseline libcurl_consistency/multi_get4_baseline_client.cpp)
    target_link_libraries(qcurl_lc_multi_get4_baseline PRIVATE CURL::libcurl)
    set_target_properties(qcurl_lc_multi_get4_baseline PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests"
    )

    add_executable(qcurl_lc_http_baseline libcurl_consistency/http_baseline_client.cpp)
    target_link_libraries(qcurl_lc_http_baseline PRIVATE CURL::libcurl)
    set_target_properties(qcurl_lc_http_baseline PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests"
    )

    add_executable(qcurl_lc_pause_resume_baseline libcurl_consistency/pause_resume_baseline_client.cpp)
    target_link_libraries(qcurl_lc_pause_resume_baseline PRIVATE CURL::libcurl)
    set_target_properties(qcurl_lc_pause_resume_baseline PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests"
    )

    # h3-capable nghttpx（best-effort：不作为强依赖；失败将导致 h3 变体 skip，并由 gate 输出告警）
    add_subdirectory(libcurl_consistency/nghttpx_from_source)

    # 上游 curl/tests 最小依赖集合（随 tests/libcurl_consistency 用例演进而维护）
    add_custom_target(qcurl_lc_upstream_testdeps)
    if(TARGET testdeps)
        add_dependencies(qcurl_lc_upstream_testdeps testdeps)
    endif()
    if(TARGET curl)
        add_dependencies(qcurl_lc_upstream_testdeps curl)
    endif()

    # Gate 构建入口（run_gate.py --build 只需构建此 target）
    add_custom_target(qcurl_lc_deps)
    add_dependencies(qcurl_lc_deps
        qcurl_lc_upstream_testdeps
        tst_LibcurlConsistency
        qcurl_lc_range_resume_baseline
        qcurl_lc_postfields_binary_baseline
        qcurl_lc_http_baseline
        qcurl_lc_pause_resume_baseline
        qcurl_lc_multi_get4_baseline
        qcurl_lc_ws_baseline
    )
endif()
# add_qcurl_test(tst_QCCurlHandleManager tst_QCCurlHandleManager.cpp)  # 待实现

# 统计测试数量
set(TEST_COUNT 29)  # 不含 WebSocket 条件测试的总数
if(QCURL_WEBSOCKET_SUPPORT)
    math(EXPR TEST_COUNT "${TEST_COUNT} + 3")  # WebSocket + WebSocketPool + WebSocketCompression
endif()

message(STATUS "测试套件已配置：${TEST_COUNT} 个测试（包含单元测试、功能测试和集成测试）")
