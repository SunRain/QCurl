cmake_minimum_required(VERSION 3.16)

project(WebSocketPoolDemo)

# 启用 Qt 自动 MOC
set(CMAKE_AUTOMOC ON)

# 查找依赖
find_package(Qt6 REQUIRED COMPONENTS Core Network)

# 源文件
set(SOURCES
    main.cpp
    PoolDemo.cpp
    PerformanceTest.cpp
)

set(HEADERS
    PoolDemo.h
    PerformanceTest.h
)

# 创建可执行文件
add_executable(WebSocketPoolDemo ${SOURCES} ${HEADERS})

# 链接库
target_link_libraries(WebSocketPoolDemo
    PRIVATE
        Qt6::Core
        Qt6::Network
        QCurl
)

# 设置输出目录
set_target_properties(WebSocketPoolDemo PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

# 安装规则
install(TARGETS WebSocketPoolDemo
    RUNTIME DESTINATION bin
)

message(STATUS "✅ WebSocketPoolDemo 示例程序已配置")
