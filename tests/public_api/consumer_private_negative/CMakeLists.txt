cmake_minimum_required(VERSION 3.21)
project(qcurl_public_api_consumer_private_negative LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(QCURL_STAGE_PREFIX "" CACHE PATH "Staged QCurl install prefix")

find_package(Qt6 REQUIRED COMPONENTS Core Network)
find_package(QCurl CONFIG REQUIRED
    PATHS "${QCURL_STAGE_PREFIX}"
    NO_DEFAULT_PATH
)

add_executable(qcurl_public_api_consumer_private_negative
    main.cpp
    connection_pool_manager_private.cpp
)
target_link_libraries(qcurl_public_api_consumer_private_negative
    PRIVATE
        QCurl::QCurl
        Qt6::Core
        Qt6::Network
)
