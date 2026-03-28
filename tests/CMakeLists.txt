# QCurl tests
#
# 目录结构：
# - tests/qcurl/：QCurl 本身的 QtTest（C++）与本地依赖
# - tests/public_api/：public header/install/export/consumer 边界门禁
# - tests/libcurl_consistency/：QCurl ↔ libcurl 一致性门禁（pytest + baseline clients + gate deps）

find_package(Qt6 REQUIRED COMPONENTS Test)
find_package(Python3 REQUIRED COMPONENTS Interpreter)

enable_testing()
set(CMAKE_AUTOMOC ON)

add_subdirectory(public_api)
add_subdirectory(qcurl)
add_subdirectory(libcurl_consistency)

add_test(
    NAME qcurl_deprecated_curl_api_guard
    COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_SOURCE_DIR}/scripts/check_deprecated_curl_apis.py"
            --curl-header "${CMAKE_SOURCE_DIR}/curl/include/curl/curl.h"
            --scan-root "${CMAKE_SOURCE_DIR}/src"
            --scan-root "${CMAKE_SOURCE_DIR}/tests"
)
set_tests_properties(qcurl_deprecated_curl_api_guard PROPERTIES
    LABELS "offline;source_guard"
)
