cmake_minimum_required(VERSION 3.21)
project(qcurl_trace_consumer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
add_compile_definitions(QT_NO_KEYWORDS)

set(QCURL_STAGE_PREFIX "" CACHE PATH "Staged QCurl install prefix")
find_package(Qt6 REQUIRED COMPONENTS Core Network Test)
find_package(CURL 8.14.1 REQUIRED)
if(NOT UNIX)
    message(FATAL_ERROR "trace consumer 需要 dl_iterate_phdr 验证 stage 加载来源")
endif()
find_package(QCurl 2.2.0 CONFIG REQUIRED COMPONENTS OtherExtras
    PATHS "${QCURL_STAGE_PREFIX}" NO_DEFAULT_PATH)

add_executable(qcurl_trace_consumer main.cpp trace_requests.cpp trace_verification.cpp trace_contract.h
    "${CMAKE_CURRENT_SOURCE_DIR}/../../qcurl/qcurl_http_script_server.h")
target_link_libraries(qcurl_trace_consumer PRIVATE QCurl::OtherExtras Qt6::Test Qt6::Network CURL::libcurl)
target_compile_options(qcurl_trace_consumer PRIVATE -Wall -Wextra -Wpedantic)
target_compile_definitions(qcurl_trace_consumer PRIVATE
    QCURL_EXPECTED_CORE="$<TARGET_FILE:QCurl::QCurl>"
    QCURL_EXPECTED_EXTRAS="$<TARGET_FILE:QCurl::OtherExtras>")
if(CMAKE_DL_LIBS)
    target_link_libraries(qcurl_trace_consumer PRIVATE ${CMAKE_DL_LIBS})
endif()
