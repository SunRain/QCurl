cmake_minimum_required(VERSION 3.21)
project(qcurl_public_api_consumer_metatype_smoke LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(QCURL_STAGE_PREFIX "" CACHE PATH "Staged QCurl install prefix")
include(GNUInstallDirs)

find_package(Qt6 REQUIRED COMPONENTS Core)
find_package(QCurl CONFIG REQUIRED
    PATHS "${QCURL_STAGE_PREFIX}"
    NO_DEFAULT_PATH
)

add_executable(qcurl_public_api_consumer_metatype_smoke
    main.cpp
)
target_link_libraries(qcurl_public_api_consumer_metatype_smoke
    PRIVATE
        QCurl::QCurl
        Qt6::Core
)
set_target_properties(qcurl_public_api_consumer_metatype_smoke PROPERTIES
    BUILD_RPATH "${QCURL_STAGE_PREFIX}/${CMAKE_INSTALL_LIBDIR}"
)
