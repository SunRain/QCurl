cmake_minimum_required(VERSION 3.16)

if(NOT DEFINED QCURL_SOURCE_DIR OR NOT DEFINED QCURL_TEST_BINARY_ROOT)
    message(FATAL_ERROR "static testing configure 测试缺少源码或输出目录")
endif()

set(_qcurl_build_dir "${QCURL_TEST_BINARY_ROOT}/static-testing-unsupported")
set(_qcurl_expected_diagnostic
    "QCURL_STATIC_TESTING_UNSUPPORTED：静态构建不支持测试")

file(REMOVE_RECURSE "${_qcurl_build_dir}")
file(MAKE_DIRECTORY "${_qcurl_build_dir}")

execute_process(
    COMMAND "${CMAKE_COMMAND}"
            -S "${QCURL_SOURCE_DIR}"
            -B "${_qcurl_build_dir}"
            -DBUILD_EXAMPLES=OFF
            -DBUILD_BENCHMARKS=OFF
            -DBUILD_TESTING=ON
            -DQCURL_BUILD_SHARED_LIBS=OFF
    RESULT_VARIABLE _qcurl_result
    OUTPUT_VARIABLE _qcurl_stdout
    ERROR_VARIABLE _qcurl_stderr
)

if(_qcurl_result EQUAL 0)
    message(FATAL_ERROR "static testing 组合仍然配置成功")
endif()

set(_qcurl_output "${_qcurl_stdout}\n${_qcurl_stderr}")
string(FIND "${_qcurl_output}" "${_qcurl_expected_diagnostic}" _qcurl_diagnostic_index)
if(_qcurl_diagnostic_index EQUAL -1)
    message(FATAL_ERROR
        "static testing configure 未输出固定诊断：\n${_qcurl_output}")
endif()
