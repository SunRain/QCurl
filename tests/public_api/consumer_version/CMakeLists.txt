cmake_minimum_required(VERSION 3.21)
project(qcurl_installed_version_contract LANGUAGES CXX)
add_compile_definitions(QT_NO_KEYWORDS)

set(QCURL_REQUEST_VERSION "2.1.0" CACHE STRING "Minimum QCurl version")
set(_exact)
if(QCURL_REQUEST_EXACT)
    set(_exact EXACT)
endif()
find_package(QCurl ${QCURL_REQUEST_VERSION} ${_exact} CONFIG REQUIRED
    PATHS "${QCURL_STAGE_PREFIX}" NO_DEFAULT_PATH)
if(NOT QCurl_VERSION VERSION_EQUAL QCURL_EXPECTED_VERSION)
    message(FATAL_ERROR "Selected unexpected QCurl version: ${QCurl_VERSION}")
endif()
