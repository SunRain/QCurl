# LoggingDemo - Logger 系统演示程序
# v2.15.0

cmake_minimum_required(VERSION 3.16)
project(LoggingDemo)

# 查找 Qt6
find_package(Qt6 REQUIRED COMPONENTS Core)

# 启用自动工具
set(CMAKE_AUTOMOC ON)

# 创建可执行文件
add_executable(LoggingDemo
    main.cpp
)

# 链接库
target_link_libraries(LoggingDemo
    PRIVATE
        Qt6::Core
        QCurl
)

# 设置包含目录
target_include_directories(LoggingDemo
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_BINARY_DIR}/src
)

# 输出目录
set_target_properties(LoggingDemo PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/examples/LoggingDemo"
)

message(STATUS "  ✅ LoggingDemo 示例已配置")
