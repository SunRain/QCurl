# QCurl 示例应用程序

# 查找 Qt6 Widgets 模块
find_package(Qt6 REQUIRED COMPONENTS Widgets)

# 启用 Qt 自动工具
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

# 源文件列表
set(EXAMPLE_SOURCES
    main.cpp
    MainWindow.cpp
    MainWindow.h
    MainWindow.ui
)

# 创建可执行文件
add_executable(QCurlExample ${EXAMPLE_SOURCES})

# 链接库
target_link_libraries(QCurlExample
    PRIVATE
        Qt6::Widgets
        QCurl
)

# 包含目录
target_include_directories(QCurlExample
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_BINARY_DIR}/src  # 用于 QCurlConfig.h
)

# 设置可执行文件输出目录
set_target_properties(QCurlExample PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

# 安装规则（可选）
install(TARGETS QCurlExample
    RUNTIME DESTINATION bin
)
