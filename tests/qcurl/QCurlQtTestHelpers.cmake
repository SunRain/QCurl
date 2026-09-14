function(_qcurl_add_test_command test_name)
    if(QT_REQUIRE_HTTPBIN
       OR QT_REQUIRE_HTTP2_SUITE
       OR QT_REQUIRE_LOCAL_PORT
       OR QT_REQUIRE_NODE
       OR QT_REQUIRE_PYTHON
       OR QT_REQUIRE_FRAGMENT_WS)
        set(test_command
            "${Python3_EXECUTABLE}"
            "${PROJECT_SOURCE_DIR}/tests/qcurl/run_qttest_with_preflight.py"
            "--test-bin" "$<TARGET_FILE:${test_name}>"
            "--test-name" "${test_name}"
        )
        if(QT_REQUIRE_HTTPBIN)
            list(APPEND test_command "--require-httpbin")
        endif()
        if(QT_REQUIRE_HTTP2_SUITE)
            list(APPEND test_command "--require-http2-suite")
            if(QT_HTTP2_PROBE_BIN)
                list(APPEND test_command "--http2-probe-bin" "${QT_HTTP2_PROBE_BIN}")
            else()
                list(APPEND test_command "--http2-probe-bin"
                     "$<TARGET_FILE:qcurl_http2_capability_probe>")
            endif()
        endif()
        if(QT_REQUIRE_LOCAL_PORT)
            list(APPEND test_command "--require-local-port")
        endif()
        if(QT_REQUIRE_NODE)
            list(APPEND test_command "--require-node")
        endif()
        if(QT_REQUIRE_PYTHON)
            list(APPEND test_command "--require-python")
        endif()
        if(QT_REQUIRE_FRAGMENT_WS)
            list(APPEND test_command "--require-fragment-ws")
        endif()
        add_test(NAME ${test_name} COMMAND ${test_command})
    else()
        add_test(NAME ${test_name} COMMAND ${test_name})
    endif()

endfunction()

# 测试辅助函数：添加单个测试
function(_add_qcurl_test_impl test_name)
    set(options
        REQUIRE_FRAGMENT_WS
        REQUIRE_HTTPBIN
        REQUIRE_HTTP2_SUITE
        REQUIRE_LOCAL_PORT
        REQUIRE_NODE
        REQUIRE_PYTHON
    )
    set(oneValueArgs HTTP2_PROBE_BIN)
    set(multiValueArgs SOURCES)
    cmake_parse_arguments(QT "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    set(test_sources ${QT_SOURCES})
    if(NOT test_sources)
        set(test_sources ${QT_UNPARSED_ARGUMENTS})
    endif()

    add_executable(${test_name} ${test_sources})
    qcurl_enable_qt_no_keywords(${test_name})

    target_link_libraries(${test_name}
        PRIVATE
            Qt6::Test
            Qt6::Network
            ${QCURL_QTTEST_CORE_TARGET}
            CURL::libcurl # 添加 libcurl 链接（某些测试需要直接调用 curl API）
            qcurl_qttest_paths
    )
    if(_qcurl_sanitizer_link_options)
        target_link_options(${test_name} PRIVATE ${_qcurl_sanitizer_link_options})
    endif()

    target_include_directories(${test_name}
        PRIVATE
            ${PROJECT_SOURCE_DIR}/src
            ${PROJECT_BINARY_DIR}/src
            ${PROJECT_SOURCE_DIR}/tests/support
    )

    _qcurl_add_test_command(${test_name})

    # QtTest 的 QSKIP 仍返回零；由 CTest 将跳过显式判为失败。
    set_tests_properties(${test_name} PROPERTIES
        FAIL_REGULAR_EXPRESSION "SKIP *:"
    )

    set_target_properties(${test_name} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests"
    )
endfunction()

function(add_qcurl_test test_name)
    _add_qcurl_test_impl(${test_name} SOURCES ${ARGN})
endfunction()

function(add_qcurl_test_with_preflight test_name)
    _add_qcurl_test_impl(${test_name} ${ARGN})
endfunction()
