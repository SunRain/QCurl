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
    add_executable(${test_name} ${source_file})
    
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
    
    message(STATUS "  ✅ ${test_name} 已配置")
endfunction()

# ============================================================================
# 添加基准测试
# ============================================================================

message(STATUS "配置性能基准测试套件:")

# WebSocket 基准测试（需要 WebSocket 支持）
if(QCURL_WEBSOCKET_SUPPORT)
    add_qcurl_benchmark(benchmark_websocket_pool benchmark_websocket_pool.cpp)
    add_qcurl_benchmark(benchmark_websocket_eventdriven benchmark_websocket_eventdriven.cpp)
    add_qcurl_benchmark(benchmark_websocket_compression benchmark_websocket_compression.cpp)  # v2.19.2
endif()

# HTTP/2 基准测试（需要 HTTP/2 支持）
if(QCURL_HTTP2_SUPPORT)
    add_qcurl_benchmark(benchmark_http2 benchmark_http2.cpp)
endif()

# HTTP/3 基准测试（v2.19.2）
add_qcurl_benchmark(benchmark_http3 benchmark_http3.cpp)

# 调度器基准测试（v2.6.0）
add_qcurl_benchmark(benchmark_scheduler benchmark_scheduler.cpp)

# 连接池基准测试（v2.14.0）
add_qcurl_benchmark(benchmark_connectionpool benchmark_connectionpool.cpp)

message(STATUS "✅ 性能基准测试已配置")
message(STATUS "")
message(STATUS "运行基准测试:")
message(STATUS "  单独运行: ./build/benchmarks/benchmark_websocket_pool")
message(STATUS "  注意: 基准测试不纳入默认 ctest，用于性能评估请手动运行")
