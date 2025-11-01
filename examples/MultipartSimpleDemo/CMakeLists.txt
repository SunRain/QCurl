# MultipartSimpleDemo - Multipart/form-data 简单示例

cmake_minimum_required(VERSION 3.16)

project(MultipartSimpleDemo LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 查找 Qt6
find_package(Qt6 REQUIRED COMPONENTS Core)

# 源文件
set(SOURCES
    main.cpp
)

# 创建可执行文件
add_executable(MultipartSimpleDemo ${SOURCES})

# 链接库
target_link_libraries(MultipartSimpleDemo
    PRIVATE
        Qt6::Core
        QCurl  # 链接 QCurl 库
)

# 设置输出目录
set_target_properties(MultipartSimpleDemo PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

message(STATUS "✅ MultipartSimpleDemo 示例程序已配置")
