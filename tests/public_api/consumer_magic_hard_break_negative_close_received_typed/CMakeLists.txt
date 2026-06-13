cmake_minimum_required(VERSION 3.21)
project(consumer_magic_hard_break_negative_close_received_typed LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(QCURL_STAGE_PREFIX "" CACHE PATH "Staged QCurl install prefix")

find_package(Qt6 REQUIRED COMPONENTS Core Network)
find_package(QCurl CONFIG REQUIRED COMPONENTS OtherExtras
    PATHS "${QCURL_STAGE_PREFIX}"
    NO_DEFAULT_PATH
)

add_executable(consumer_magic_hard_break_negative_close_received_typed main.cpp)
target_link_libraries(consumer_magic_hard_break_negative_close_received_typed
    PRIVATE
        QCurl::OtherExtras
        Qt6::Core
        Qt6::Network
)
