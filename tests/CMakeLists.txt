# QCurl tests
#
# 目录结构：
# - tests/qcurl/：QCurl 本身的 QtTest（C++）与本地依赖
# - tests/libcurl_consistency/：QCurl ↔ libcurl 一致性门禁（pytest + baseline clients + gate deps）

find_package(Qt6 REQUIRED COMPONENTS Test)

enable_testing()
set(CMAKE_AUTOMOC ON)

add_subdirectory(qcurl)
add_subdirectory(libcurl_consistency)
