# Network Features Demo - QCurl v2.17-v2.19 新功能演示

cmake_minimum_required(VERSION 3.16)

project(NetworkFeaturesDemo LANGUAGES CXX)

# 查找 Qt6
find_package(Qt6 REQUIRED COMPONENTS Core Network)

# 启用 Qt 自动工具
set(CMAKE_AUTOMOC ON)

# 创建可执行文件
add_executable(NetworkFeaturesDemo
    main.cpp
)

# 链接库
target_link_libraries(NetworkFeaturesDemo
    PRIVATE
        Qt6::Core
        Qt6::Network
        QCurl
        CURL::libcurl
)

# 包含目录
target_include_directories(NetworkFeaturesDemo
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_BINARY_DIR}/src
)

# 设置输出目录
set_target_properties(NetworkFeaturesDemo PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/examples/NetworkFeaturesDemo"
)

# 安装目标
install(TARGETS NetworkFeaturesDemo
    RUNTIME DESTINATION bin
)
