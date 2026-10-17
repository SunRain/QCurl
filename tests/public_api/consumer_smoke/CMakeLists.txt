cmake_minimum_required(VERSION 3.21)
project(qcurl_public_api_consumer_smoke LANGUAGES CXX)

add_compile_definitions(QT_NO_KEYWORDS)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(QCURL_STAGE_PREFIX "" CACHE PATH "Staged QCurl install prefix")

find_package(Qt6 REQUIRED COMPONENTS Core)
find_package(QCurl 2.1.0 CONFIG REQUIRED
    PATHS "${QCURL_STAGE_PREFIX}"
    NO_DEFAULT_PATH
)

add_executable(qcurl_public_api_consumer_smoke
    main.cpp
    request_config_probe.cpp
    cookie_async_result_probe.cpp
    scheduler_probe.cpp
)
target_link_libraries(qcurl_public_api_consumer_smoke
    PRIVATE
        QCurl::QCurl
        Qt6::Core
)

# 普通已安装 Core 不得通过目标宏泄漏内部网络路径组。
foreach(_internal_api IN ITEMS connectTo dnsServers dohUrl happyEyeballsTimeout networkInterface localPort)
    try_compile(_internal_api_available
        SOURCE_FROM_CONTENT network_path_negative.cpp
            "#include <QCNetworkRequest.h>
int main() { QCurl::QCNetworkRequest request; request.${_internal_api}(); }
"
        LINK_LIBRARIES QCurl::QCurl Qt6::Core
        CXX_STANDARD 17
        OUTPUT_VARIABLE _negative_output)
    if(_internal_api_available OR NOT _negative_output MATCHES "(has no member|no member named)")
        message(FATAL_ERROR "Internal API ${_internal_api} was not rejected for the expected reason: ${_negative_output}")
    endif()
endforeach()
