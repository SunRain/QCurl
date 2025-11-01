cmake_minimum_required(VERSION 3.16)

project(Http3Demo)

# 查找 Qt6
find_package(Qt6 REQUIRED COMPONENTS Core)

# 启用 Qt 自动工具
set(CMAKE_AUTOMOC ON)

# 创建可执行文件
add_executable(Http3Demo main.cpp)

# 链接库
target_link_libraries(Http3Demo
    PRIVATE
        Qt6::Core
        QCurl
        CURL::libcurl
)

# 包含目录
target_include_directories(Http3Demo
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_BINARY_DIR}/src
)

# 设置输出目录
set_target_properties(Http3Demo PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/examples/Http3Demo"
)

# 安装
install(TARGETS Http3Demo
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}/examples
)
