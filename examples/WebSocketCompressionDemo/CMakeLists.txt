# WebSocketCompressionDemo - WebSocket 压缩扩展演示
# v2.18.0

cmake_minimum_required(VERSION 3.16)

# 创建可执行文件
add_executable(WebSocketCompressionDemo main.cpp)

# 链接库
target_link_libraries(WebSocketCompressionDemo
    PRIVATE
        Qt6::Core
        QCurl
)

# 包含目录
target_include_directories(WebSocketCompressionDemo
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../../src
)
