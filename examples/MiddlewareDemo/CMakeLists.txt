cmake_minimum_required(VERSION 3.16)
project(MiddlewareDemo)

find_package(Qt6 REQUIRED COMPONENTS Core)
set(CMAKE_AUTOMOC ON)

add_executable(MiddlewareDemo main.cpp)

target_link_libraries(MiddlewareDemo PRIVATE Qt6::Core QCurl)

target_include_directories(MiddlewareDemo PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_BINARY_DIR}/src
)

set_target_properties(MiddlewareDemo PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/examples/MiddlewareDemo"
)

message(STATUS "  ✅ MiddlewareDemo 示例已配置")
