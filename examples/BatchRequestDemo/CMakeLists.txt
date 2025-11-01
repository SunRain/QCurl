cmake_minimum_required(VERSION 3.16)

project(BatchRequestDemo)

# 找到 Qt6
find_package(Qt6 REQUIRED COMPONENTS Core)

# 设置自动 MOC
set(CMAKE_AUTOMOC ON)

# 添加可执行文件
add_executable(BatchRequestDemo
    main.cpp
    BatchRequest.h
    BatchRequest.cpp
)

# 链接库
target_link_libraries(BatchRequestDemo
    PRIVATE
        Qt6::Core
        QCurl
)

# 包含 QCurl 头文件
target_include_directories(BatchRequestDemo
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src
)

# 设置输出目录
set_target_properties(BatchRequestDemo PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/examples/BatchRequestDemo
)
