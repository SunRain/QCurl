# WebSocket 示例程序

cmake_minimum_required(VERSION 3.16)

project(WebSocketDemo VERSION 1.0 LANGUAGES CXX)

# 查找 Qt6
find_package(Qt6 REQUIRED COMPONENTS Core)

# 启用 Qt 自动工具
set(CMAKE_AUTOMOC ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 创建可执行文件
add_executable(WebSocketDemo main.cpp)

# 链接库
target_link_libraries(WebSocketDemo
    PRIVATE
        Qt6::Core
        QCurl
)

message(STATUS "WebSocketDemo 示例程序已配置")
