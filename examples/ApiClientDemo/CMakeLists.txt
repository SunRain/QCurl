cmake_minimum_required(VERSION 3.16)

project(ApiClientDemo)

# 找到 Qt6
find_package(Qt6 REQUIRED COMPONENTS Core)

# 设置自动 MOC
set(CMAKE_AUTOMOC ON)

# 添加可执行文件
add_executable(ApiClientDemo
    main.cpp
    ApiClient.h
    ApiClient.cpp
)

# 链接库
target_link_libraries(ApiClientDemo
    PRIVATE
        Qt6::Core
        QCurl
)

# 包含 QCurl 头文件
target_include_directories(ApiClientDemo
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src
)

# 设置输出目录
set_target_properties(ApiClientDemo PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/examples/ApiClientDemo
)
