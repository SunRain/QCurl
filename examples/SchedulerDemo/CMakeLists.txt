# SchedulerDemo 示例程序 - 请求优先级调度演示

cmake_minimum_required(VERSION 3.16)

# 创建可执行文件
add_executable(SchedulerDemo main.cpp)

# 启用 Qt 自动工具
set_target_properties(SchedulerDemo PROPERTIES
    AUTOMOC ON
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
)

# 链接库
target_link_libraries(SchedulerDemo
    PRIVATE
        Qt6::Core
        Qt6::Network
        QCurl
)

# 包含目录
target_include_directories(SchedulerDemo
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_BINARY_DIR}/src
)

# 安装（可选）
install(TARGETS SchedulerDemo
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}/examples
        COMPONENT Examples)

message(STATUS "✅ SchedulerDemo 示例程序已配置")
