# FileTransferDemo - 文件传输 API 示例

cmake_minimum_required(VERSION 3.16)

project(FileTransferDemo LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 REQUIRED COMPONENTS Core)

add_executable(FileTransferDemo
    main.cpp
)

target_link_libraries(FileTransferDemo
    PRIVATE
        Qt6::Core
        QCurl
)

set_target_properties(FileTransferDemo PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

message(STATUS "✅ FileTransferDemo 示例程序已配置")
