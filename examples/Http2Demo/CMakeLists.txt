cmake_minimum_required(VERSION 3.16)

project(Http2Demo)

# 设置 C++ 标准
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 启用 Qt 自动 MOC
set(CMAKE_AUTOMOC ON)

# 查找 Qt6
find_package(Qt6 REQUIRED COMPONENTS Core)

# 添加可执行文件
add_executable(Http2Demo main.cpp)

# 链接库
target_link_libraries(Http2Demo
    PRIVATE
        Qt6::Core
        QCurl
        CURL::libcurl
)

# 设置输出目录
set_target_properties(Http2Demo PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/examples/Http2Demo"
)

# 安装规则（可选）
install(TARGETS Http2Demo
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}/examples
)

message(STATUS "  ✅ Http2Demo 示例已配置")
