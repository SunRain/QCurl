cmake_minimum_required(VERSION 3.16)
project(QCurl VERSION 2.0.0 LANGUAGES CXX)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

# 要求 C++17
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 构建选项
option(BUILD_EXAMPLES "Build example applications" ON)
option(BUILD_TESTING "Build tests" ON)
option(BUILD_BENCHMARKS "Build performance benchmarks" ON)
option(QCURL_BUILD_LIBCURL_CONSISTENCY "Build libcurl_consistency gate dependencies and use bundled curl (build/curl) for tests" OFF)

# 编译选项：启用所有警告
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-Wall -Wextra -Wpedantic)
elseif(MSVC)
    add_compile_options(/W4)
endif()

# 查找依赖
find_package(Qt6 REQUIRED COMPONENTS Core Network)
if(QCURL_BUILD_LIBCURL_CONSISTENCY)
    if(NOT BUILD_TESTING)
        message(FATAL_ERROR "QCURL_BUILD_LIBCURL_CONSISTENCY=ON 需要 BUILD_TESTING=ON（需要编译并运行 tests/libcurl_consistency 相关目标）")
    endif()

    # curl 子模块将被构建到 QCurl 构建目录的子目录：<qcurl_build>/curl（默认 build/curl）
    # 注意：curl 子项目也使用 BUILD_EXAMPLES 作为开关变量，因此需做隔离，避免污染 QCurl 的示例构建选项。
    set(_qcurl_saved_BUILD_EXAMPLES ${BUILD_EXAMPLES})
    set(BUILD_EXAMPLES OFF CACHE BOOL "Build libcurl examples" FORCE)

    # 只启用一致性门禁所需的最小产物；安装/文档/示例默认关闭，减少构建成本与失败面。
    set(BUILD_CURL_EXE ON CACHE BOOL "Build curl executable (required by curl testenv)" FORCE)
    set(BUILD_LIBCURL_DOCS OFF CACHE BOOL "Build libcurl man pages" FORCE)
    set(CURL_DISABLE_INSTALL ON CACHE BOOL "Disable installation targets (for bundled curl)" FORCE)
    # 使 bundled libcurl 的 ABI 形态尽量贴近系统 libcurl（避免与依赖 libcurl 的第三方库产生符号版本冲突）。
    set(CURL_LIBCURL_VERSIONED_SYMBOLS ON CACHE BOOL "Enable libcurl versioned symbols" FORCE)
    set(CURL_USE_PKGCONFIG ON CACHE BOOL "Enable pkg-config to detect dependencies (for bundled curl)" FORCE)
    set(CURL_DISABLE_WEBSOCKETS OFF CACHE BOOL "Enable WebSocket support (required by libcurl_consistency)" FORCE)

    # HTTP/3 + WebSockets 约束：
    # - 目标：在依赖满足时，构建出的 bundled curl 必须具备 HTTP/3 + WebSockets 能力；
    # - 降级：当缺少依赖时，不阻断构建（h3 变体将由 gate 自动 skip，同时记录 warning）。
    #
    # curl 的 HTTP/3 支持需要显式开启（USE_NGTCP2 / USE_QUICHE / USE_OPENSSL_QUIC）。其中 USE_NGTCP2 是最常见路径，
    # 且依赖 ngtcp2 + nghttp3。为了避免“依赖缺失导致配置直接失败”，这里先进行 best-effort 预检，按结果决定是否启用。
    list(PREPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/curl/CMake")
    set(USE_NGTCP2 OFF CACHE BOOL "Use ngtcp2+nghttp3 for HTTP/3 support" FORCE)
    set(_qcurl_lc_http3_backend "disabled")
    set(_qcurl_lc_http3_reason "")
    find_package(OpenSSL QUIET)
    if(OpenSSL_FOUND)
        # OpenSSL < 3.5 不包含 QUIC API，通常需要 quictls 变体；OpenSSL >= 3.5 可使用 ossl 变体。
        if(DEFINED OPENSSL_VERSION AND OPENSSL_VERSION VERSION_GREATER_EQUAL "3.5.0")
            find_package(NGTCP2 QUIET COMPONENTS ossl)
            find_package(NGHTTP3 QUIET)
            if(NGTCP2_FOUND AND NGHTTP3_FOUND)
                set(USE_NGTCP2 ON CACHE BOOL "Use ngtcp2+nghttp3 for HTTP/3 support" FORCE)
                set(_qcurl_lc_http3_backend "ngtcp2+nghttp3(ossl)")
            else()
                set(_qcurl_lc_http3_reason "未检测到 NGTCP2(ossl)/NGHTTP3 依赖，将自动 skip h3 变体")
            endif()
        else()
            find_package(NGTCP2 QUIET COMPONENTS quictls)
            find_package(NGHTTP3 QUIET)
            if(NGTCP2_FOUND AND NGHTTP3_FOUND)
                set(USE_NGTCP2 ON CACHE BOOL "Use ngtcp2+nghttp3 for HTTP/3 support" FORCE)
                set(_qcurl_lc_http3_backend "ngtcp2+nghttp3(quictls)")
            else()
                set(_qcurl_lc_http3_reason "未检测到 NGTCP2(quictls)/NGHTTP3 依赖，将自动 skip h3 变体")
            endif()
        endif()
    else()
        set(_qcurl_lc_http3_reason "未检测到 OpenSSL，无法启用 bundled curl 的 HTTP/3 backend；将自动 skip h3 变体")
    endif()
    message(STATUS "libcurl_consistency: curl baseline HTTP/3 backend: ${_qcurl_lc_http3_backend}")
    if(_qcurl_lc_http3_reason)
        message(WARNING "libcurl_consistency: ${_qcurl_lc_http3_reason}")
    endif()
    unset(_qcurl_lc_http3_backend)
    unset(_qcurl_lc_http3_reason)

    if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/curl/CMakeLists.txt")
        message(FATAL_ERROR "未找到 curl/ 子模块（curl/CMakeLists.txt）。请先执行：git submodule update --init --recursive")
    endif()

    add_subdirectory(curl "${CMAKE_BINARY_DIR}/curl" EXCLUDE_FROM_ALL)

    set(BUILD_EXAMPLES ${_qcurl_saved_BUILD_EXAMPLES} CACHE BOOL "Build example applications" FORCE)
    unset(_qcurl_saved_BUILD_EXAMPLES)

    # 复用 curl/include/curl/curlver.h，提取版本（用于特性判定与输出）。
    file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/curl/include/curl/curlver.h" _qcurl_lc_curlver_contents REGEX "#define LIBCURL_VERSION( |_NUM )")
    string(REGEX MATCH "#define LIBCURL_VERSION \"[^\"]*" _qcurl_lc_curlver_line "${_qcurl_lc_curlver_contents}")
    string(REGEX REPLACE "[^\"]+\"" "" QCURL_LIBCURL_VERSION_STRING "${_qcurl_lc_curlver_line}")
    unset(_qcurl_lc_curlver_contents)
    unset(_qcurl_lc_curlver_line)

    set(QCURL_LIBCURL_PROVIDER "bundled(curl submodule @ ${CMAKE_BINARY_DIR}/curl)")
    # 兼容：部分子目录仍使用 CURL_VERSION_STRING 输出信息
    set(CURL_VERSION_STRING "${QCURL_LIBCURL_VERSION_STRING}")
else()
    find_package(CURL 8.0 REQUIRED)
    set(QCURL_LIBCURL_PROVIDER "system(find_package(CURL))")
    set(QCURL_LIBCURL_VERSION_STRING "${CURL_VERSION_STRING}")
endif()

# 打印依赖信息
message(STATUS "Qt6 version: ${Qt6_VERSION}")
message(STATUS "libcurl provider: ${QCURL_LIBCURL_PROVIDER}")
message(STATUS "libcurl version: ${QCURL_LIBCURL_VERSION_STRING}")

# 检查 libcurl 版本和特性
if(QCURL_LIBCURL_VERSION_STRING VERSION_GREATER_EQUAL "7.86.0")
    set(QCURL_WEBSOCKET_SUPPORT ON)
    message(STATUS "✅ WebSocket support enabled (libcurl ${QCURL_LIBCURL_VERSION_STRING})")
else()
    set(QCURL_WEBSOCKET_SUPPORT OFF)
    message(STATUS "❌ WebSocket support disabled (requires libcurl >= 7.86.0)")
endif()

if(QCURL_LIBCURL_VERSION_STRING VERSION_GREATER_EQUAL "7.66.0")
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
if(BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

# 测试（可选）
if(BUILD_TESTING)
    enable_testing()
    add_subdirectory(tests)
endif()

if(BUILD_BENCHMARKS)
    add_subdirectory(benchmarks)
endif()

#
# install/export 规则应始终可用：
# - 方案包 7.3 需要在主 build 目录上验证 `cmake --install` + consumer smoke
# - `QCurl` 仅 PUBLIC 暴露 Qt，libcurl 为 PRIVATE 依赖，因此导出的 package
#   不需要把 bundled curl 目标一并导出给下游
#
install(TARGETS QCurl
    EXPORT QCurlTargets
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/qcurl
)

if(QCURL_BUILD_LIBCURL_CONSISTENCY)
    install(TARGETS libcurl_shared
        EXPORT QCurlTargets
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    )
endif()

# 导出 CMake 配置
install(EXPORT QCurlTargets
    FILE QCurlTargets.cmake
    NAMESPACE QCurl::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/QCurl
)

configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/QCurlConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/QCurlConfig.cmake"
    INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/QCurl"
)
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/QCurlConfigVersion.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)
install(
    FILES
        "${CMAKE_CURRENT_BINARY_DIR}/QCurlConfig.cmake"
        "${CMAKE_CURRENT_BINARY_DIR}/QCurlConfigVersion.cmake"
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/QCurl
)

# 创建 pkg-config 文件
configure_file(qcurl.pc.in qcurl.pc @ONLY)
install(FILES ${CMAKE_BINARY_DIR}/qcurl.pc
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig)

if(NOT QCURL_BUILD_LIBCURL_CONSISTENCY)
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
else()
    message(STATUS "QCURL_BUILD_LIBCURL_CONSISTENCY=ON：保留 install/export 以支持 consumer smoke，跳过 CPack 打包")
endif()

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
