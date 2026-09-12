cmake_minimum_required(VERSION 3.16)

project(QCurlBenchmarks)

# 启用 Qt 自动 MOC
set(CMAKE_AUTOMOC ON)

# 查找依赖
find_package(Qt6 REQUIRED COMPONENTS Core Network Test)

# 启用测试
enable_testing()

# 基准测试辅助函数
function(add_qcurl_benchmark test_name source_file)
    add_executable(${test_name} ${source_file} ${ARGN})
    qcurl_enable_qt_no_keywords(${test_name})
    
    target_link_libraries(${test_name}
        PRIVATE
            Qt6::Core
            Qt6::Network
            Qt6::Test
            QCurl
            CURL::libcurl
    )
    
    # 设置输出目录
    set_target_properties(${test_name} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/benchmarks"
    )

    message(STATUS "  ${test_name}")
endfunction()

message(STATUS "配置性能基准测试套件")

# WebSocket 基准测试（需要 WebSocket 支持）
if(QCURL_WEBSOCKET_SUPPORT)
    add_qcurl_benchmark(benchmark_websocket_pool benchmark_websocket_pool.cpp)
    target_link_libraries(benchmark_websocket_pool PRIVATE QCurlOtherExtras)
    add_qcurl_benchmark(benchmark_websocket_eventdriven benchmark_websocket_eventdriven.cpp)
    target_link_libraries(benchmark_websocket_eventdriven PRIVATE QCurlOtherExtras)
endif()

# HTTP/2 基准测试（需要 HTTP/2 支持）
if(QCURL_HTTP2_SUPPORT)
    add_qcurl_benchmark(benchmark_http2 benchmark_http2.cpp)
endif()

# HTTP/3 基准测试
add_qcurl_benchmark(benchmark_http3 benchmark_http3.cpp)

# 调度器基准测试
# 该基准使用 QCNetworkMockHandler 与 TestSupport::setMockHandler 构造确定性调度场景，
# 因此必须显式链接 QCurlTestSupport；仅链接 QCurl 会在链接期缺失这些符号。
add_qcurl_benchmark(benchmark_scheduler benchmark_scheduler.cpp benchmark_scheduler_helpers.h)
target_link_libraries(benchmark_scheduler PRIVATE QCurlTestSupport)

# 连接池基准测试
add_qcurl_benchmark(benchmark_connectionpool benchmark_connectionpool.cpp)

message(STATUS "基准测试目标")
message(STATUS "  示例: ./build/benchmarks/benchmark_websocket_pool")
