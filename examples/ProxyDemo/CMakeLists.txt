cmake_minimum_required(VERSION 3.16)

project(ProxyDemo)

# 设置 C++ 标准
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 启用 Qt 自动 MOC
set(CMAKE_AUTOMOC ON)

# 查找 Qt6
find_package(Qt6 REQUIRED COMPONENTS Core)

# 添加可执行文件
add_executable(ProxyDemo main.cpp)

# 链接库
target_link_libraries(ProxyDemo
    PRIVATE
        Qt6::Core
        QCurl
)

# 设置输出目录
set_target_properties(ProxyDemo PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/examples/ProxyDemo"
)

# 安装规则（可选）
install(TARGETS ProxyDemo
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}/examples
)

message(STATUS "  ✅ ProxyDemo 示例已配置")
