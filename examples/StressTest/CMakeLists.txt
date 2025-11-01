# StressTest 示例程序 - 调度器压力测试

cmake_minimum_required(VERSION 3.16)

# 创建可执行文件
add_executable(StressTest main.cpp)

# 启用 Qt 自动工具
set_target_properties(StressTest PROPERTIES
    AUTOMOC ON
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
)

# 链接库
target_link_libraries(StressTest
    PRIVATE
        Qt6::Core
        Qt6::Network
        QCurl
)

# 包含目录
target_include_directories(StressTest
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_BINARY_DIR}/src
)

# 安装（可选）
install(TARGETS StressTest
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}/examples
        COMPONENT Examples)

message(STATUS "✅ StressTest 压力测试程序已配置")
