cmake_minimum_required(VERSION 3.16)
project(UnifiedPolicyMiddlewareOfflineDemo)

find_package(Qt6 REQUIRED COMPONENTS Core)
set(CMAKE_AUTOMOC ON)

add_executable(UnifiedPolicyMiddlewareOfflineDemo main.cpp)
qcurl_enable_qt_no_keywords(UnifiedPolicyMiddlewareOfflineDemo)

# 该离线示例通过 QCNetworkMockHandler 与 TestSupport::setMockHandler 构造无网络场景，
# 因此必须显式链接 QCurlTestSupport。
target_link_libraries(UnifiedPolicyMiddlewareOfflineDemo PRIVATE Qt6::Core QCurlOtherExtras QCurlTestSupport)

target_include_directories(UnifiedPolicyMiddlewareOfflineDemo PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_BINARY_DIR}/src
)

set_target_properties(UnifiedPolicyMiddlewareOfflineDemo PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/examples/UnifiedPolicyMiddlewareOfflineDemo"
)

message(STATUS "  ✅ UnifiedPolicyMiddlewareOfflineDemo 示例已配置")

