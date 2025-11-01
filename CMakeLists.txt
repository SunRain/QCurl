cmake_minimum_required(VERSION 3.16)
project(QCurl VERSION 2.0.0 LANGUAGES CXX)

# 要求 C++17
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 编译选项：启用所有警告
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-Wall -Wextra -Wpedantic)
elseif(MSVC)
    add_compile_options(/W4)
endif()

# 查找依赖
find_package(Qt6 REQUIRED COMPONENTS Core Network)
find_package(CURL 8.0 REQUIRED)

# 打印依赖信息
message(STATUS "Qt6 version: ${Qt6_VERSION}")
message(STATUS "libcurl version: ${CURL_VERSION_STRING}")

# 检查 libcurl 版本和特性
if(CURL_VERSION_STRING VERSION_GREATER_EQUAL "7.86.0")
    set(QCURL_WEBSOCKET_SUPPORT ON)
    message(STATUS "✅ WebSocket support enabled (libcurl ${CURL_VERSION_STRING})")
else()
    set(QCURL_WEBSOCKET_SUPPORT OFF)
    message(STATUS "❌ WebSocket support disabled (requires libcurl >= 7.86.0)")
endif()

if(CURL_VERSION_STRING VERSION_GREATER_EQUAL "7.66.0")
    set(QCURL_HTTP2_SUPPORT ON)
    message(STATUS "✅ HTTP/2 support available")
else()
    set(QCURL_HTTP2_SUPPORT OFF)
    message(STATUS "❌ HTTP/2 support unavailable (requires libcurl >= 7.66.0)")
endif()

# 配置头文件
configure_file(src/QCurlConfig.h.in src/QCurlConfig.h)

# 子目录
add_subdirectory(src)

# 示例程序（可选）
option(BUILD_EXAMPLES "Build example applications" ON)
if(BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

# 测试（可选）
option(BUILD_TESTING "Build tests" ON)
if(BUILD_TESTING)
    enable_testing()
    add_subdirectory(tests)
endif()

# 性能基准测试（v2.5.0，可选）
option(BUILD_BENCHMARKS "Build performance benchmarks" ON)
if(BUILD_BENCHMARKS)
    add_subdirectory(benchmarks)
endif()

# 安装规则
install(TARGETS QCurl
    EXPORT QCurlTargets
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/qcurl
)

# 导出 CMake 配置
install(EXPORT QCurlTargets
    FILE QCurlTargets.cmake
    NAMESPACE QCurl::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/QCurl
)

# 创建 pkg-config 文件
configure_file(qcurl.pc.in qcurl.pc @ONLY)
install(FILES ${CMAKE_BINARY_DIR}/qcurl.pc
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig)

# CPack 配置
set(CPACK_PACKAGE_NAME "QCurl")
set(CPACK_PACKAGE_VENDOR "QCurl Project")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Qt6-based libcurl wrapper library with HTTP/2 and WebSocket support")
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_PACKAGE_CONTACT "qcurl@example.com")
set(CPACK_GENERATOR "TGZ;DEB;RPM")
set(CPACK_SOURCE_GENERATOR "TGZ;ZIP")
set(CPACK_DEBIAN_PACKAGE_DEPENDS "libqt6core6, libqt6network6, libcurl4 (>= 8.0)")
set(CPACK_RPM_PACKAGE_REQUIRES "qt6-qtbase >= 6.2, libcurl >= 8.0")
include(CPack)

# 打印配置摘要
message(STATUS "")
message(STATUS "========================================")
message(STATUS "QCurl Configuration Summary")
message(STATUS "========================================")
message(STATUS "Version: ${PROJECT_VERSION}")
message(STATUS "C++ Standard: C++${CMAKE_CXX_STANDARD}")
message(STATUS "Build Type: ${CMAKE_BUILD_TYPE}")
message(STATUS "Install Prefix: ${CMAKE_INSTALL_PREFIX}")
message(STATUS "")
message(STATUS "Dependencies:")
message(STATUS "  Qt6: ${Qt6_VERSION}")
message(STATUS "  libcurl: ${CURL_VERSION_STRING}")
message(STATUS "")
message(STATUS "Features:")
message(STATUS "  HTTP/2: ${QCURL_HTTP2_SUPPORT}")
message(STATUS "  WebSocket: ${QCURL_WEBSOCKET_SUPPORT}")
message(STATUS "")
message(STATUS "Build Options:")
message(STATUS "  Examples: ${BUILD_EXAMPLES}")
message(STATUS "  Testing: ${BUILD_TESTING}")
message(STATUS "========================================")
message(STATUS "")
